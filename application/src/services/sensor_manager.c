/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sensor Manager Implementation
 */

#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/battery.h>
#include <drivers/i2c_bus.h>
#include <drivers/lps22hh.h>
#include <drivers/qi_charger.h>
#include <drivers/sht40.h>
#include <drivers/stcc4.h>
#include <services/user_settings.h>
#include <services/ble_service.h>
#include <services/led_indicator.h>
#include <services/sensor_manager.h>

LOG_MODULE_REGISTER(sensor_mgr, CONFIG_AIRSPOT_LOG_LEVEL);

/* Defined in main.c — feeds the watchdog (no-op until the WDT is set up). Used
 * to keep the watchdog alive during the long blocking STCC4 conditioning. */
extern void watchdog_feed(void);
static void conditioning_keepalive(void) { watchdog_feed(); }

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

/* STCC4 single-shot timing */
#define STCC4_SINGLE_SHOT_WAIT_MS \
  550 /* Wait for single-shot result ~500ms + margin */

/* Work queue for sensor measurements */
static struct k_work_delayable sensor_work;
static uint32_t measurement_interval_ms = DEFAULT_INTERVAL_MS;
static bool running = false;
static bool initialized = false;
static bool use_single_shot_mode =
    false; /* Track if using single-shot for STCC4 */

/* Latest sensor data (protected by data_mutex) */
static struct k_mutex data_mutex;
static sensor_data_t latest_data;

/* Serializes the whole measurement sequence. do_measurement() can be invoked
 * both from the sensor work queue (periodic) and from the protocol thread
 * (manual refresh / reset). Without this, two measurement sequences would
 * interleave their multi-step I2C transactions on the shared sensor bus. */
static struct k_mutex meas_mutex;

/* User callback */
static sensor_data_callback_t user_callback = NULL;

/* Forward declaration */
static void sensor_work_handler(struct k_work *work);

static void apply_stcc4_asc_setting(void)
{
  uint8_t asc_enabled = user_settings_get_auto_calibrate();
  int ret;

  if (asc_enabled)
  {
    ret = stcc4_disable_testing_mode();
    if (ret)
    {
      LOG_WRN("STCC4 ASC enable failed: %d", ret);
    }
  }
  else
  {
    ret = stcc4_enable_testing_mode();
    if (ret)
    {
      LOG_WRN("STCC4 ASC disable failed: %d", ret);
    }
  }
}

int sensor_manager_init(void)
{
  int ret;

  if (initialized)
  {
    return 0;
  }

  LOG_INF("Initializing sensor manager...");

  k_mutex_init(&data_mutex);
  k_mutex_init(&meas_mutex);
  k_work_init_delayable(&sensor_work, sensor_work_handler);

  /* 1. Initialize I2C bus first */
  ret = i2c_bus_init();
  if (ret)
  {
    LOG_ERR("I2C bus init failed: %d", ret);
    return ret;
  }

  /* 2. Initialize SHT40 (MUST be before STCC4 for compensation) */
  ret = sht40_init();
  if (ret)
  {
    LOG_ERR("SHT40 init failed: %d", ret);
    return ret;
  }

  /* 3. Initialize STCC4 CO2 sensor */
  ret = stcc4_init();
  if (ret)
  {
    LOG_ERR("STCC4 init failed: %d", ret);
    return ret;
  }

#if IS_ENABLED(CONFIG_AIRSPOT_STCC4_CONDITION_ON_BOOT)
  /* Condition the STCC4 before the first measurement. Sensirion recommends
   * this whenever the sensor has been idle >3h (i.e. a cold boot); skipping it
   * leaves the thermal-conductivity reading biased high. ~22s + 2s settle. */
  LOG_INF("STCC4 conditioning (~22s, one time)...");
  /* Pulsing white = "starting up / warming up", distinct from the idle-blue and
   * the R/Y/G air-quality colors. Runs while conditioning blocks; the main loop's
   * idle indicator takes over once we stop it. Must be a FULL-brightness pulse,
   * not an analog fade: these are essentially on/off LEDs, and at the partial
   * brightness of a fade the red channel browns out first, so "white" reads as
   * cyan. A brief full-on blip each period keeps the pulse feel while rendering
   * true white. */
  led_indicator_start_flash_pulse(LED_COLOR_WHITE, 1500, 400);
  ret = stcc4_perform_conditioning(conditioning_keepalive);
  if (ret) {
    LOG_WRN("STCC4 conditioning failed: %d (continuing)", ret);
  } else {
    conditioning_keepalive();
    k_msleep(2000); /* recommended settling time after conditioning */
    conditioning_keepalive();
    LOG_INF("STCC4 conditioning complete");
  }
  led_indicator_stop();
#endif

  /* Apply ASC setting on startup (ASC enabled by default in sensor) */
  apply_stcc4_asc_setting();

  /* Default to single-shot mode for power saving.
   * Power mode can be changed via sensor_manager_set_power_mode().
   * Mode 0-2 (On-Demand, Low, Mid) use single-shot for power efficiency.
   * Mode 3 (High) uses continuous mode for real-time monitoring.
   */
  use_single_shot_mode = true;
  LOG_INF("STCC4 initialized (single-shot mode for power saving)");

  /* 4. Initialize LPS22HH pressure sensor */
  ret = lps22hh_init();
  if (ret)
  {
    LOG_ERR("LPS22HH init failed: %d", ret);
    return ret;
  }

  /* 5. Initialize battery ADC */
  ret = battery_init();
  if (ret)
  {
    LOG_WRN("Battery init failed: %d (continuing)", ret);
    /* Non-critical, continue */
  }

  /* 6. Initialize Qi charger detection */
  ret = qi_charger_init();
  if (ret)
  {
    LOG_WRN("Qi charger init failed: %d (continuing)", ret);
    /* Non-critical, continue */
  }

  initialized = true;
  LOG_INF("Sensor manager initialized successfully");

  return 0;
}

static void do_measurement(void)
{
  sensor_data_t data = {0};
  sht40_data_t sht_data;
  stcc4_data_t stcc_data;
  lps22hh_data_t lps_data;
  int ret;
  bool all_ok = true;

  /* Serialize the full sensor read sequence (periodic vs. manual). */
  k_mutex_lock(&meas_mutex, K_FOREVER);

  data.timestamp_ms = k_uptime_get_32();

  /* 1. Read SHT40 temperature and humidity (Compensation Source 1) */
  ret = sht40_measure(&sht_data, SHT40_PRECISION_HIGH);
  if (ret == 0)
  {
    data.temperature_c = sht_data.temperature;
    data.humidity_rh = sht_data.humidity;
    LOG_DBG("SHT40: %.1f°C, %.1f%%RH", (double)data.temperature_c,
            (double)data.humidity_rh);
  }
  else
  {
    LOG_WRN("SHT40 read failed: %d", ret);
    all_ok = false;
  }

  /* 2. Read LPS22HH pressure (Compensation Source 2) */
  ret = lps22hh_read(&lps_data);
  if (ret == 0)
  {
    data.pressure_hpa = lps_data.pressure;
    LOG_DBG("LPS22HH: %.1f hPa", (double)data.pressure_hpa);
  }
  else
  {
    LOG_WRN("LPS22HH read failed: %d", ret);
    /* Fallback to standard pressure if sensor fails */
    data.pressure_hpa = 1013.0f;
    all_ok = false;
  }

  /* 3. Feed Environmental Data to STCC4 for Compensation */
  /* Note: This is critical for STCC4 accuracy */
  if (!isnan(data.temperature_c) && !isnan(data.humidity_rh))
  {
    stcc4_set_ambient(data.temperature_c, data.humidity_rh);
  }
  if (data.pressure_hpa > 0)
  {
    stcc4_set_pressure((uint16_t)data.pressure_hpa);
  }

  /* 4. Read STCC4 CO2 */
  if (use_single_shot_mode)
  {
    /* Single-shot mode: trigger measurement, wait, then read */
    ret = stcc4_trigger_single_shot();
    if (ret == 0)
    {
      /* Wait for measurement to complete (~500ms + margin) */
      k_msleep(STCC4_SINGLE_SHOT_WAIT_MS);

      ret = stcc4_read(&stcc_data);
      if (ret == 0 && stcc_data.co2_ppm > 0)
      {
        data.co2_ppm = stcc_data.co2_ppm;
        LOG_DBG("STCC4 (single-shot): %u ppm CO2", data.co2_ppm);
      }
      else
      {
        /* Use last known value if read failed */
        data.co2_ppm = stcc4_get_co2();
        LOG_DBG("STCC4 single-shot read failed, using cached: %u ppm",
                data.co2_ppm);
      }
    }
    else
    {
      LOG_WRN("STCC4 single-shot trigger failed: %d", ret);
      data.co2_ppm = stcc4_get_co2();
    }
  }
  else
  {
    /* Continuous mode: data is available immediately (1Hz updates) */
    ret = stcc4_read(&stcc_data);
    if (ret == 0 && stcc_data.co2_ppm > 0)
    {
      data.co2_ppm = stcc_data.co2_ppm;
      LOG_DBG("STCC4 (continuous): %u ppm CO2", data.co2_ppm);
    }
    else
    {
      /* First read after starting continuous might fail, use cached */
      data.co2_ppm = stcc4_get_co2();
      if (data.co2_ppm == 0)
      {
        LOG_DBG("STCC4 CO2 not yet available (sensor warming up)");
      }
    }
  }

  /* 5. Read battery */
  data.battery_mv = battery_read_voltage_mv();
  data.battery_percent = battery_get_percentage();
  data.battery_low = battery_is_low();
  data.battery_critical = battery_is_critical();
  LOG_DBG("Battery: %u mV (%u%%)", data.battery_mv, data.battery_percent);

  /* Note: Update BLE Battery Service */
  ble_service_update_battery_level(data.battery_percent);

  /* 6. Read Qi charging status */
  data.is_charging = qi_charger_is_charging();

  data.valid = all_ok;

  /* Store latest data */
  k_mutex_lock(&data_mutex, K_FOREVER);
  latest_data = data;
  k_mutex_unlock(&data_mutex);

  /* Notify callback */
  if (user_callback)
  {
    user_callback(&data);
  }

  /* Log summary */
  LOG_INF("Sensors: CO2=%uppm, T=%.1f°C, H=%.0f%%, P=%.0fhPa, Batt=%u%%",
          data.co2_ppm, (double)data.temperature_c, (double)data.humidity_rh,
          (double)data.pressure_hpa, data.battery_percent);

  k_mutex_unlock(&meas_mutex);
}

static void sensor_work_handler(struct k_work *work)
{
  ARG_UNUSED(work);

  do_measurement();

  /* Schedule next measurement if still running */
  if (running)
  {
    k_work_schedule(&sensor_work, K_MSEC(measurement_interval_ms));
  }
}

/**
 * @brief Start periodic sensor measurements.
 *
 * @param interval_ms Measurement interval in milliseconds.
 * @return 0 on success, negative errno on failure.
 */
int sensor_manager_start(uint32_t interval_ms)
{
  if (!initialized)
  {
    return -ENODEV;
  }

  if (interval_ms < MIN_INTERVAL_MS)
  {
    interval_ms = MIN_INTERVAL_MS;
  }

  measurement_interval_ms = interval_ms;
  running = true;

  LOG_INF("Starting sensor measurements (interval: %u ms)", interval_ms);

  /* Do first measurement immediately */
  k_work_schedule(&sensor_work, K_NO_WAIT);

  return 0;
}

/**
 * @brief Stop periodic measurements and cancel scheduled work.
 */
void sensor_manager_stop(void)
{
  running = false;
  k_work_cancel_delayable(&sensor_work);
  LOG_INF("Sensor measurements stopped");
}

/**
 * @brief Trigger an immediate measurement cycle.
 *
 * @return 0 on success, negative errno on failure.
 */
int sensor_manager_measure_now(void)
{
  if (!initialized)
  {
    return -ENODEV;
  }

  LOG_INF("Manual measurement triggered");
  do_measurement();
  return 0;
}

/**
 * @brief Get the latest sensor data snapshot.
 *
 * @param data Output buffer for the latest reading.
 * @return 0 on success, -ENODATA if no measurement yet, negative errno on failure.
 */
int sensor_manager_get_data(sensor_data_t *data)
{
  if (!data)
  {
    return -EINVAL;
  }

  k_mutex_lock(&data_mutex, K_FOREVER);
  if (latest_data.timestamp_ms == 0)
  {
    k_mutex_unlock(&data_mutex);
    return -ENODATA;
  }
  *data = latest_data;
  k_mutex_unlock(&data_mutex);

  return 0;
}

/**
 * @brief Register a callback invoked after each measurement.
 *
 * @param callback Callback to receive new data.
 */
void sensor_manager_register_callback(sensor_data_callback_t callback)
{
  user_callback = callback;
}

/**
 * @brief Check if the sensor manager has completed initialization.
 *
 * @return true if ready, false otherwise.
 */
bool sensor_manager_is_ready(void) { return initialized; }

/**
 * @brief Set sensor power mode and measurement cadence.
 *
 * @param power_mode 0=on-demand, 1=low, 2=mid, 3=high.
 */
void sensor_manager_set_power_mode(uint8_t power_mode)
{
  uint32_t new_interval;
  bool want_single_shot;

  /* Power modes match old firmware:
   * 0 = On Demand - manual measurements only (button/BLE request)
   * 1 = Low (3 min) - battery saving
   * 2 = Mid (1 min) - balanced
   * 3 = High (5 sec) - real-time monitoring
   *
   * STCC4 Power Strategy:
   * - Modes 0-2: Single-shot for maximum power savings
   * - Mode 3: Continuous mode for real-time updates (5-sec interval)
   */
  switch (power_mode)
  {
  case 0: /* On demand - no automatic measurements */
    new_interval = POWER_MODE_ON_DEMAND_INTERVAL_MS;
    want_single_shot = true;
    break;
  case 1: /* Low power - 3 minutes */
    new_interval = POWER_MODE_LOW_INTERVAL_MS;
    want_single_shot = true;
    break;
  case 2: /* Mid - 1 minute */
    new_interval = POWER_MODE_MID_INTERVAL_MS;
    want_single_shot = true;
    break;
  case 3: /* High - 5 seconds */
    new_interval = POWER_MODE_HI_INTERVAL_MS;
    want_single_shot = false; /* Use continuous for fast updates */
    break;
  default:
    new_interval = POWER_MODE_LOW_INTERVAL_MS;
    want_single_shot = true;
    break;
  }

  /* Switch STCC4 mode if needed */
  if (want_single_shot != use_single_shot_mode)
  {
    if (want_single_shot)
    {
      /* Switch to single-shot mode */
      stcc4_stop();
      use_single_shot_mode = true;
      LOG_INF("STCC4: Switched to single-shot mode (power saving)");
    }
    else
    {
      /* Switch to continuous mode */
      int ret = stcc4_start_continuous();
      if (ret == 0)
      {
        use_single_shot_mode = false;
        LOG_INF("STCC4: Switched to continuous mode (real-time)");
      }
      else
      {
        LOG_WRN("STCC4 continuous start failed: %d, keeping single-shot", ret);
      }
    }
  }

  if (new_interval != measurement_interval_ms)
  {
    measurement_interval_ms = new_interval;

    if (new_interval == 0)
    {
      LOG_INF("Measurement mode: ON DEMAND (manual only)");
      /* Stop automatic measurements */
      if (running)
      {
        k_work_cancel_delayable(&sensor_work);
        running = false;
      }
    }
    else
    {
      LOG_INF("Measurement interval changed to %u ms (power mode %d)",
              new_interval, power_mode);

      /* Reschedule if was running, or start if switching from on-demand */
      k_work_cancel_delayable(&sensor_work);
      running = true;
      k_work_schedule(&sensor_work, K_MSEC(measurement_interval_ms));
    }
  }
}

/**
 * @brief Reset the CO2 sensor by reinitializing the driver.
 *
 * @return 0 on success, negative error code on failure.
 */
int sensor_manager_reset_co2_sensor(void)
{
  /* Reset STCC4 sensor by re-initializing it */
  LOG_INF("Resetting CO2 sensor (STCC4)...");

  int rc = user_settings_set_co2_correction(0);
  if (rc == 0)
  {
    LOG_INF("CO2 calibration correction cleared");
  }
  else
  {
    LOG_WRN("Failed to clear CO2 correction: %d", rc);
  }

  /* Perform a real sensor reset (sleep/wake) rather than stcc4_init(), which
   * is a no-op once the driver is already initialized. */
  int ret = stcc4_reset();
  if (ret == 0)
  {
    use_single_shot_mode = true;
    apply_stcc4_asc_setting();
  }
  return ret;
}
