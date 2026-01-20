/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sensor Manager Implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/battery.h>
#include <drivers/i2c_bus.h>
#include <drivers/lps22hh.h>
#include <drivers/qi_charger.h>
#include <drivers/sht40.h>
#include <drivers/stcc4.h>
#include <services/sensor_manager.h>

LOG_MODULE_REGISTER(sensor_mgr, CONFIG_AIRSPOT_LOG_LEVEL);

/* Default measurement interval */
#define DEFAULT_INTERVAL_MS 5000
#define MIN_INTERVAL_MS 1000

/* Power mode intervals - matches old firmware enum:
 * PWR_MODE_ON_DEMAND = 0 -> No automatic measurements (manual only)
 * PWR_MODE_LOW = 1       -> 3 minutes
 * PWR_MODE_MID = 2       -> 1 minute
 * PWR_MODE_HI = 3        -> 5 seconds
 */
#define POWER_MODE_ON_DEMAND_INTERVAL_MS 0 /* Manual only - no timer */
#define POWER_MODE_LOW_INTERVAL_MS 180000  /* 3 minutes */
#define POWER_MODE_MID_INTERVAL_MS 60000   /* 1 minute */
#define POWER_MODE_HI_INTERVAL_MS 5000     /* 5 seconds */

/* Work queue for sensor measurements */
static struct k_work_delayable sensor_work;
static uint32_t measurement_interval_ms = DEFAULT_INTERVAL_MS;
static bool running = false;
static bool initialized = false;

/* Latest sensor data (protected by mutex) */
static struct k_mutex data_mutex;
static sensor_data_t latest_data;

/* User callback */
static sensor_data_callback_t user_callback = NULL;

/* Forward declaration */
static void sensor_work_handler(struct k_work *work);

int sensor_manager_init(void) {
  int ret;

  if (initialized) {
    return 0;
  }

  LOG_INF("Initializing sensor manager...");

  k_mutex_init(&data_mutex);
  k_work_init_delayable(&sensor_work, sensor_work_handler);

  /* 1. Initialize I2C bus first */
  ret = i2c_bus_init();
  if (ret) {
    LOG_ERR("I2C bus init failed: %d", ret);
    return ret;
  }

  /* 2. Initialize SHT40 (MUST be before STCC4 for compensation) */
  ret = sht40_init();
  if (ret) {
    LOG_ERR("SHT40 init failed: %d", ret);
    return ret;
  }

  /* 3. Initialize STCC4 CO2 sensor */
  ret = stcc4_init();
  if (ret) {
    LOG_ERR("STCC4 init failed: %d", ret);
    return ret;
  }

  /* Start STCC4 in continuous mode - measurements available every 1 second */
  ret = stcc4_start_continuous();
  if (ret) {
    LOG_WRN("STCC4 continuous mode start failed: %d (will try single-shot)",
            ret);
  } else {
    LOG_INF("STCC4 running in continuous mode");
  }

  /* 4. Initialize LPS22HH pressure sensor */
  ret = lps22hh_init();
  if (ret) {
    LOG_ERR("LPS22HH init failed: %d", ret);
    return ret;
  }

  /* 5. Initialize battery ADC */
  ret = battery_init();
  if (ret) {
    LOG_WRN("Battery init failed: %d (continuing)", ret);
    /* Non-critical, continue */
  }

  /* 6. Initialize Qi charger detection */
  ret = qi_charger_init();
  if (ret) {
    LOG_WRN("Qi charger init failed: %d (continuing)", ret);
    /* Non-critical, continue */
  }

  initialized = true;
  LOG_INF("Sensor manager initialized successfully");

  return 0;
}

static void do_measurement(void) {
  sensor_data_t data = {0};
  sht40_data_t sht_data;
  stcc4_data_t stcc_data;
  lps22hh_data_t lps_data;
  int ret;
  bool all_ok = true;

  data.timestamp_ms = k_uptime_get_32();

  /* 1. Read SHT40 temperature and humidity */
  ret = sht40_measure(&sht_data, SHT40_PRECISION_HIGH);
  if (ret == 0) {
    data.temperature_c = sht_data.temperature;
    data.humidity_rh = sht_data.humidity;
    LOG_DBG("SHT40: %.1f°C, %.1f%%RH", (double)data.temperature_c,
            (double)data.humidity_rh);

    /* 2. Update STCC4 with ambient conditions for compensation */
    stcc4_set_ambient(data.temperature_c, data.humidity_rh);
  } else {
    LOG_WRN("SHT40 read failed: %d", ret);
    all_ok = false;
  }

  /* 3. Read STCC4 CO2 */
  /* In continuous mode, data is available immediately */
  /* In single-shot mode, we need to trigger and wait */
  ret = stcc4_read(&stcc_data);
  if (ret == 0 && stcc_data.co2_ppm > 0) {
    data.co2_ppm = stcc_data.co2_ppm;
    LOG_DBG("STCC4: %u ppm CO2", data.co2_ppm);
  } else {
    /* Try triggering a single-shot measurement */
    LOG_DBG("STCC4 read returned %d/%u, triggering single-shot", ret,
            stcc_data.co2_ppm);
    stcc4_trigger_single_shot();
    /* Note: Result will be available at next measurement cycle */
    /* For now, use last known good value or 0 */
    data.co2_ppm = stcc4_get_co2();
    if (data.co2_ppm == 0) {
      LOG_WRN("STCC4 CO2 not available (sensor warming up?)");
    }
  }

  /* 4. Read LPS22HH pressure */
  ret = lps22hh_read(&lps_data);
  if (ret == 0) {
    data.pressure_hpa = lps_data.pressure;
    LOG_DBG("LPS22HH: %.1f hPa", (double)data.pressure_hpa);
  } else {
    LOG_WRN("LPS22HH read failed: %d", ret);
    all_ok = false;
  }

  /* 5. Read battery */
  data.battery_mv = battery_read_voltage_mv();
  data.battery_percent = battery_get_percentage();
  data.battery_low = battery_is_low();
  data.battery_critical = battery_is_critical();
  LOG_DBG("Battery: %u mV (%u%%)", data.battery_mv, data.battery_percent);

  /* 6. Read Qi charging status */
  data.is_charging = qi_charger_is_charging();

  data.valid = all_ok;

  /* Store latest data */
  k_mutex_lock(&data_mutex, K_FOREVER);
  latest_data = data;
  k_mutex_unlock(&data_mutex);

  /* Notify callback */
  if (user_callback) {
    user_callback(&data);
  }

  /* Log summary */
  LOG_INF("Sensors: CO2=%uppm, T=%.1f°C, H=%.0f%%, P=%.0fhPa, Batt=%u%%",
          data.co2_ppm, (double)data.temperature_c, (double)data.humidity_rh,
          (double)data.pressure_hpa, data.battery_percent);
}

static void sensor_work_handler(struct k_work *work) {
  ARG_UNUSED(work);

  do_measurement();

  /* Schedule next measurement if still running */
  if (running) {
    k_work_schedule(&sensor_work, K_MSEC(measurement_interval_ms));
  }
}

int sensor_manager_start(uint32_t interval_ms) {
  if (!initialized) {
    return -ENODEV;
  }

  if (interval_ms < MIN_INTERVAL_MS) {
    interval_ms = MIN_INTERVAL_MS;
  }

  measurement_interval_ms = interval_ms;
  running = true;

  LOG_INF("Starting sensor measurements (interval: %u ms)", interval_ms);

  /* Do first measurement immediately */
  k_work_schedule(&sensor_work, K_NO_WAIT);

  return 0;
}

void sensor_manager_stop(void) {
  running = false;
  k_work_cancel_delayable(&sensor_work);
  LOG_INF("Sensor measurements stopped");
}

int sensor_manager_measure_now(void) {
  if (!initialized) {
    return -ENODEV;
  }

  LOG_INF("Manual measurement triggered");
  do_measurement();
  return 0;
}

int sensor_manager_get_data(sensor_data_t *data) {
  if (!data) {
    return -EINVAL;
  }

  k_mutex_lock(&data_mutex, K_FOREVER);
  if (latest_data.timestamp_ms == 0) {
    k_mutex_unlock(&data_mutex);
    return -ENODATA;
  }
  *data = latest_data;
  k_mutex_unlock(&data_mutex);

  return 0;
}

void sensor_manager_register_callback(sensor_data_callback_t callback) {
  user_callback = callback;
}

bool sensor_manager_is_ready(void) { return initialized; }

void sensor_manager_set_power_mode(uint8_t power_mode) {
  uint32_t new_interval;

  /* Power modes match old firmware:
   * 0 = On Demand - manual measurements only (button/BLE request)
   * 1 = Low (3 min) - battery saving
   * 2 = Mid (1 min) - balanced
   * 3 = High (5 sec) - real-time monitoring
   */
  switch (power_mode) {
  case 0: /* On demand - no automatic measurements */
    new_interval = POWER_MODE_ON_DEMAND_INTERVAL_MS;
    break;
  case 1: /* Low power - 3 minutes */
    new_interval = POWER_MODE_LOW_INTERVAL_MS;
    break;
  case 2: /* Mid - 1 minute */
    new_interval = POWER_MODE_MID_INTERVAL_MS;
    break;
  case 3: /* High - 5 seconds */
    new_interval = POWER_MODE_HI_INTERVAL_MS;
    break;
  default:
    new_interval = POWER_MODE_LOW_INTERVAL_MS; /* Default to low power */
    break;
  }

  if (new_interval != measurement_interval_ms) {
    measurement_interval_ms = new_interval;

    if (new_interval == 0) {
      LOG_INF("Measurement mode: ON DEMAND (manual only)");
      /* Stop automatic measurements */
      if (running) {
        k_work_cancel_delayable(&sensor_work);
        running = false;
      }
    } else {
      LOG_INF("Measurement interval changed to %u ms (power mode %d)",
              new_interval, power_mode);

      /* Reschedule if was running, or start if switching from on-demand */
      k_work_cancel_delayable(&sensor_work);
      running = true;
      k_work_schedule(&sensor_work, K_MSEC(measurement_interval_ms));
    }
  }
}
