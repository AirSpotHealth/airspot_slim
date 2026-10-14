#include <services/settling.h>
#include <services/diagnostics.h>
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
#include <services/software_asc.h>
#include <services/ble_service.h>
#include <services/led_indicator.h>
#include <services/sensor_manager.h>
#include <services/health_service.h>

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
static int64_t next_report_ms;
static bool compensation_failed_since_report;
static uint8_t compensation_error;
static bool initialized = false;
static bool use_single_shot_mode =
    false; /* Track if using single-shot for STCC4 */

/* Latest sensor data (protected by data_mutex) */
K_MUTEX_DEFINE(data_mutex);
static sensor_data_t latest_data;

/* Serializes the whole measurement sequence. do_measurement() can be invoked
 * both from the sensor work queue (periodic) and from the protocol thread
 * (manual refresh / reset). Without this, two measurement sequences would
 * interleave their multi-step I2C transactions on the shared sensor bus. */
K_MUTEX_DEFINE(meas_mutex);

/* User callback */
static sensor_data_callback_t user_callback = NULL;

/* Forward declaration */
static void sensor_work_handler(struct k_work *work);

/* Independent of the sampling timer: also repairs failed startup/on-demand
 * operation. All state is protected by meas_mutex. Never erase calibration. */
static void recovery_work_handler(struct k_work *work);
static struct k_work_delayable recovery_work;
static bool work_initialized, recovery_enabled;
static bool stcc4_conditioned;
static int64_t failed_since = -1, recovery_after;
static sensor_recovery_diagnostic_t recovery_diagnostic;

static void recovery_observe(bool valid)
{
  if (valid) {
    failed_since = -1;
    recovery_after = 0;
    recovery_diagnostic.consecutive_attempts = 0;
  } else if (failed_since < 0) {
    failed_since = k_uptime_get();
  }
}

/* Caller holds meas_mutex or is in startup before measurements are enabled. */
static sensor_asc_diagnostic_t asc_diagnostic;
static sensor_co2_diagnostic_t co2_diagnostic = {
  .error = -EAGAIN, .fault = SENSOR_FAULT_STARTING
};
void sensor_manager_get_asc_diagnostic(sensor_asc_diagnostic_t *diagnostic)
{
  k_mutex_lock(&meas_mutex, K_FOREVER);
  *diagnostic = asc_diagnostic;
  diagnostic->read = co2_diagnostic;
  k_mutex_unlock(&meas_mutex);
}

int sensor_manager_self_test(uint16_t *result)
{
  if (!result) return -EINVAL;
  *result = UINT16_MAX;
  int ret = software_asc_begin_manual();
  if (ret) return ret;
  k_mutex_lock(&meas_mutex, K_FOREVER);
  if (!initialized) {
    ret = -ENODEV;
  } else {
    bool continuous = !use_single_shot_mode;
    settling_observe(false);
    ret = stcc4_stop();
    if (!ret) {
      ret = stcc4_perform_self_test(result);
      /* Self-test neither resets the sensor nor changes its ASC policy. */
      int restore = continuous ? stcc4_start_continuous() : 0;
      if (restore) {
        ret = restore;
        /* Reject measurements until explicit sensor recovery succeeds. */
        initialized = false;
      }
    }
    k_mutex_lock(&data_mutex, K_FOREVER);
    latest_data.valid = false;
    latest_data.error_code = ret ? SENSOR_FAULT_CO2 : SENSOR_FAULT_STARTING;
    k_mutex_unlock(&data_mutex);
  }
  k_mutex_unlock(&meas_mutex);
  software_asc_end_manual();
  return ret;
}

/* Caller holds meas_mutex. Never send fabricated environmental values after a
 * failed read; the report is invalid until all compensation writes succeed. */
static void set_sensor_fault(uint8_t code)
{
  recovery_observe(false);
  k_mutex_lock(&data_mutex, K_FOREVER);
  latest_data.valid = false;
  latest_data.error_code = code;
  k_mutex_unlock(&data_mutex);
}

static int update_compensation(sensor_data_t *data)
{
  sht40_data_t ambient;
  lps22hh_data_t pressure;
  data->error_code = SENSOR_FAULT_AMBIENT;
  int ret = sht40_measure(&ambient, SHT40_PRECISION_HIGH);
  if (!ret && (!isfinite(ambient.temperature) || !isfinite(ambient.humidity))) ret = -ERANGE;
  if (!ret) {
    data->error_code = SENSOR_FAULT_PRESSURE;
    ret = lps22hh_read(&pressure);
  }
  if (!ret && (!isfinite(pressure.pressure) || pressure.pressure <= 0 ||
      pressure.pressure > UINT16_MAX)) ret = -ERANGE;
  if (!ret) {
    data->error_code = SENSOR_FAULT_COMPENSATION;
    ret = stcc4_set_ambient(ambient.temperature, ambient.humidity);
  }
  if (!ret) ret = stcc4_set_pressure((uint16_t)pressure.pressure);
  if (ret) {
    diagnostics_record(DIAG_SAMPLE_FAILED,
        ((uint32_t)data->error_code << 24) | (1U << 16) | (uint16_t)ret);
    compensation_failed_since_report = true;
    compensation_error = data->error_code;
    return ret;
  }
  data->error_code = 0;
  data->temperature_c = ambient.temperature;
  data->humidity_rh = ambient.humidity;
  data->pressure_hpa = pressure.pressure;
  return 0;
}

/* Caller holds meas_mutex; reject cached data until a fresh policy is verified. */
static int configure_stcc4_asc(bool enabled)
{
  asc_diagnostic = (sensor_asc_diagnostic_t){.stage = 1};
  int ret = stcc4_stop();
  conditioning_keepalive();
  if (!ret) {
    stcc4_data_t discarded = {0};
    (void)stcc4_read(&discarded);
    stcc4_set_asc_policy(enabled);
    asc_diagnostic.stage = 2;
    ret = stcc4_enable_testing_mode();
  }
  /* Native learning requires both sides of the production status transition. */
  if (!ret && enabled) {
    sensor_data_t ambient = {0};
    ret = update_compensation(&ambient);
    if (!ret) ret = stcc4_trigger_single_shot();
    if (!ret) {
      k_msleep(STCC4_SINGLE_SHOT_WAIT_MS);
      conditioning_keepalive();
      stcc4_data_t paused = {0};
      int read_ret = stcc4_read(&paused);
      asc_diagnostic.status = paused.status;
      if ((read_ret && read_ret != -EACCES) ||
          !(paused.status & STCC4_STATUS_TESTING_MODE))
        ret = read_ret ? read_ret : -EACCES;
    }
    if (!ret) ret = stcc4_disable_testing_mode();
  }
  if (!ret) {
    asc_diagnostic.stage = 6;
    sensor_data_t ambient = {0};
    ret = update_compensation(&ambient);
  }
  bool verification_running = false;
  if (!ret && enabled) {
    ret = stcc4_start_continuous();
    verification_running = !ret;
  }
  unsigned attempts = enabled ? 3 : 1;
  for (unsigned attempt = 0; !ret && attempt < attempts; ++attempt) {
    asc_diagnostic.stage = 3;
    if (enabled) k_msleep(1200);
    else {
      ret = stcc4_trigger_single_shot();
      if (ret) break;
      k_msleep(STCC4_SINGLE_SHOT_WAIT_MS);
    }
    conditioning_keepalive();
    asc_diagnostic.stage = 4;
    stcc4_data_t verification = {0};
    ret = stcc4_read(&verification);
    asc_diagnostic.status = verification.status;
    if (!ret && !stcc4_status_matches_asc_policy(verification.status)) ret = -EACCES;
    if (ret == -EACCES && attempt + 1 < attempts) {
      ret = 0;
      continue;
    }
    break;
  }
  if (verification_running) {
    int stop_ret = stcc4_stop();
    conditioning_keepalive();
    if (!ret) ret = stop_ret;
  }
  asc_diagnostic.error = ret;
  if (!ret) {
    compensation_failed_since_report = false;
    asc_diagnostic.stage = 5;
    software_asc_sensor_restarted();
  }
  return ret;
}

/* Apply and verify hardware first, then persist. A failed transaction restores
 * the prior policy; a failed restoration invalidates the sensor pipeline. */
int sensor_manager_change_native_asc(bool enabled)
{
  int rc = software_asc_begin_manual(); /* Shared lease excludes manual calibration. */
  if (rc) return rc;
  k_mutex_lock(&meas_mutex, K_FOREVER);
  if (!initialized) rc = -ENODEV;
  else {
    uint8_t previous = user_settings_get_native_asc_mode();
    bool was_continuous = !use_single_shot_mode;
    uint8_t desired = enabled ? 2 : (previous ? 1 : 0);
    if (desired == previous && (enabled || !user_settings_get_auto_calibrate()) && asc_diagnostic.stage == 5 && !asc_diagnostic.error &&
        stcc4_status_matches_asc_policy(asc_diagnostic.status)) {
      k_mutex_unlock(&meas_mutex);
      software_asc_end_manual();
      return 0; /* Do not interrupt learning for duplicate requests. */
    }
    settling_observe(false);
    set_sensor_fault(SENSOR_FAULT_STARTING);
    rc = configure_stcc4_asc(enabled);
    if (!rc && was_continuous) rc = stcc4_start_continuous();
    if (!rc) rc = user_settings_set_native_asc_mode(desired);
    if (rc) {
      int restore = configure_stcc4_asc(previous == 2);
      if (!restore && was_continuous) restore = stcc4_start_continuous();
      if (restore) {
        initialized = false;
        set_sensor_fault(SENSOR_FAULT_CO2);
      }
    }
    next_report_ms = 0;
  }
  k_mutex_unlock(&meas_mutex);
  software_asc_end_manual();
  return rc;
}

static int sensor_manager_init_locked(void)
{
  int ret;

  if (initialized)
  {
    return 0;
  }

  /* Only the initial boot attempt may wait out a pre-reset conditioning cycle.
   * Runtime recovery shares meas_mutex with workers whose deadlines are 35s. */
  static bool boot_attempt_available = true;
  bool boot_attempt = boot_attempt_available;
  boot_attempt_available = false;
  ARG_UNUSED(boot_attempt);
  set_sensor_fault(SENSOR_FAULT_STARTING);
  LOG_INF("Initializing sensor manager...");

  if (!work_initialized) {
    k_work_init_delayable(&sensor_work, sensor_work_handler);
    k_work_init_delayable(&recovery_work, recovery_work_handler);
    work_initialized = true;
  }

  /* 1. Initialize I2C bus first */
  ret = i2c_bus_init();
  if (ret)
  {
    LOG_ERR("I2C bus init failed: %d", ret);
    set_sensor_fault(SENSOR_FAULT_I2C);
    return ret;
  }

  /* 2. Initialize SHT40 (MUST be before STCC4 for compensation) */
  ret = sht40_init();
  if (ret)
  {
    LOG_ERR("SHT40 init failed: %d", ret);
    set_sensor_fault(SENSOR_FAULT_AMBIENT);
    return ret;
  }

  /* 3. Initialize STCC4 CO2 sensor */
  ret = stcc4_init();
  if (ret)
  {
    LOG_ERR("STCC4 init failed: %d", ret);
    set_sensor_fault(SENSOR_FAULT_CO2);
    return ret;
  }

  /* Automatic retries repair communication/measurement state. Repeat conditioning only if it has
   * not yet succeeded this boot; normal runtime faults need a volatile reset. */
  if (recovery_diagnostic.stage != 1 || !stcc4_conditioned) {
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
  if (ret && boot_attempt) {
    /* An MCU reset does not reset the powered sensor. If reset interrupted
     * conditioning, it can NACK commands until that 22-second cycle finishes.
     * Wait out one complete cycle plus settling, then retry once. */
    LOG_WRN("STCC4 busy/unavailable after reset: %d; bounded startup retry", ret);
    for (unsigned second = 0; second < 24; ++second) {
      conditioning_keepalive();
      k_msleep(1000);
    }
    conditioning_keepalive();
    /* Recover to idle whether the powered sensor was asleep or measuring.
     * Do not enter sleep: that extra transition can NACK after an MCU reset.
     * The final conditioning ACK and later ASC/sample checks decide health. */
    stcc4_exit_sleep_mode();
    int stop_rc = stcc4_stop();
    if (stop_rc) LOG_WRN("STCC4 recovery stop returned %d", stop_rc);
    /* An I2C error does not prove the sensor rejected the stop command. */
    k_msleep(stop_rc ? 1250 : 50); /* driver already waits 1200ms on ACK */
    conditioning_keepalive();
    ret = stcc4_perform_conditioning(conditioning_keepalive);
  }
  if (ret) {
    led_indicator_stop();
    LOG_ERR("STCC4 conditioning retry failed: %d", ret);
    set_sensor_fault(SENSOR_FAULT_CO2);
    return ret;
  } else {
    conditioning_keepalive();
    k_msleep(2000); /* recommended settling time after conditioning */
    conditioning_keepalive();
    stcc4_conditioned = true;
    LOG_INF("STCC4 conditioning complete");
  }
  led_indicator_stop();
#endif
  }

  /* 4. Initialize LPS22HH pressure sensor */
  ret = lps22hh_init();
  if (ret)
  {
    LOG_ERR("LPS22HH init failed: %d", ret);
    set_sensor_fault(SENSOR_FAULT_PRESSURE);
    return ret;
  }

  /* Internal ASC is independent of the AirSpot software auto-calibration flag. */
  ret = configure_stcc4_asc(user_settings_get_native_asc_mode() == 2);
  if (ret) { set_sensor_fault(SENSOR_FAULT_COMPENSATION); return ret; }

  /* Default to single-shot mode for power saving.
   * Power mode can be changed via sensor_manager_set_power_mode().
   * Mode 0-2 (On-Demand, Low, Mid) use single-shot for power efficiency.
   * Mode 3 (High) uses continuous mode for real-time monitoring.
   */
  use_single_shot_mode = true;
  LOG_INF("STCC4 initialized (single-shot mode for power saving)");


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

int sensor_manager_init(void)
{
  k_mutex_lock(&meas_mutex, K_FOREVER);
  int ret = sensor_manager_init_locked();
  k_mutex_unlock(&meas_mutex);
  return ret;
}

/* Caller holds meas_mutex, including across callback/correction processing. */
static int do_measurement_locked(void)
{
  sensor_data_t data = {0};
  stcc4_data_t stcc_data = {0};
  int ret;
  data.timestamp_ms = k_uptime_get_32();
  bool all_ok = update_compensation(&data) == 0;
  all_ok = all_ok && !compensation_failed_since_report;
  if (!all_ok && !data.error_code) data.error_code = compensation_error;

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
      if (ret == 0 && stcc_data.valid &&
          stcc4_status_matches_asc_policy(stcc_data.status) && stcc_data.co2_ppm > 0)
      {
        data.co2_ppm = stcc_data.co2_ppm;
        LOG_DBG("STCC4 (single-shot): %u ppm CO2", data.co2_ppm);
      }
      else
      {
        /* Use last known value if read failed */
        all_ok = false;
        data.co2_ppm = stcc4_get_co2();
        LOG_DBG("STCC4 single-shot read failed, using cached: %u ppm",
                data.co2_ppm);
      }
    }
    else
    {
      LOG_WRN("STCC4 single-shot trigger failed: %d", ret);
      all_ok = false;
      data.co2_ppm = stcc4_get_co2();
    }
  }
  else
  {
    /* Continuous mode: data is available immediately (1Hz updates) */
    ret = stcc4_read(&stcc_data);
    if (ret == 0 && stcc_data.valid &&
          stcc4_status_matches_asc_policy(stcc_data.status) && stcc_data.co2_ppm > 0)
    {
      data.co2_ppm = stcc_data.co2_ppm;
      LOG_DBG("STCC4 (continuous): %u ppm CO2", data.co2_ppm);
    }
    else
    {
      /* First read after starting continuous might fail, use cached */
      all_ok = false;
      data.co2_ppm = stcc4_get_co2();
      if (data.co2_ppm == 0)
      {
        LOG_DBG("STCC4 CO2 not yet available (sensor warming up)");
      }
    }
  }

#if defined(CONFIG_AIRSPOT_STCC4_SINGLE_SHOT_SLEEP)
  /* Completed shots may sleep; subsequent compensation/commands wake first.
   * A failed read/trigger remains awake for diagnostics and recovery. */
  if (use_single_shot_mode && !ret && all_ok) {
    ret = stcc4_sleep_after_sample();
    if (ret) all_ok = false;
  }
#endif

  /* 5. Read battery */
  data.battery_mv = battery_read_voltage_mv();
  data.battery_percent = battery_get_percentage();
  data.battery_low = data.battery_mv < 3300;
  data.battery_critical = data.battery_mv < 3000;
  LOG_DBG("Battery: %u mV (%u%%)", data.battery_mv, data.battery_percent);

  /* Note: Update BLE Battery Service */
  ble_service_update_battery_level(data.battery_percent);

  /* 6. Read Qi charging status */
  data.is_charging = qi_charger_is_charging();

  data.valid = all_ok;
  if (!all_ok && !data.error_code) data.error_code = SENSOR_FAULT_CO2;
  if (!data.valid) {
    int sample_error = ret ? ret : (!stcc_data.valid ? -EACCES :
        (stcc_data.co2_ppm == 0 ? -ERANGE : 0));
    diagnostics_record(DIAG_SAMPLE_FAILED,
        ((uint32_t)data.error_code << 24) | (2U << 16) | (uint16_t)sample_error);
    diagnostics_record(DIAG_CO2_CONTEXT,
        ((uint32_t)stcc_data.status << 16) | (uint16_t)stcc_data.raw_co2_ppm);
    diagnostics_record(DIAG_SENSOR_POWER,
        ((uint32_t)data.battery_mv << 16) | (data.is_charging ? 1U : 0U) |
        (stcc_data.has_raw_co2 ? 2U : 0U) | (use_single_shot_mode ? 4U : 0U));
  }
  recovery_observe(data.valid);
  settling_observe(data.valid && !use_single_shot_mode &&
                   user_settings_get_native_asc_mode() == 2);

  co2_diagnostic = (sensor_co2_diagnostic_t){
    .sequence = co2_diagnostic.sequence + 1,
    .uptime_ms = data.timestamp_ms,
    .error = ret,
    .raw_co2_ppm = stcc_data.raw_co2_ppm,
    .status = stcc_data.status,
    .temperature_centi = (int16_t)(data.temperature_c * 100.0f),
    .humidity_centi = (uint16_t)(data.humidity_rh * 100.0f),
    .pressure_hpa = (uint16_t)data.pressure_hpa,
    .flags = (stcc_data.has_raw_co2 ? 1 : 0) |
             (stcc_data.valid ? 2 : 0) | (data.valid ? 4 : 0),
    .fault = data.error_code,
  };

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

  compensation_failed_since_report = false;
  return all_ok ? 0 : -EIO;
}

static int do_measurement(void)
{
  k_mutex_lock(&meas_mutex, K_FOREVER);
  int ret = initialized ? do_measurement_locked() : -ENODEV;
  k_mutex_unlock(&meas_mutex);
  return ret;
}

static void sensor_work_handler(struct k_work *work)
{
  ARG_UNUSED(work);
  k_mutex_lock(&meas_mutex, K_FOREVER);
  if (!running || !initialized) {
    health_progress(HEALTH_SENSOR, 0);
    k_mutex_unlock(&meas_mutex);
    return;
  }
  int64_t started = k_uptime_get();
  if (use_single_shot_mode || started >= next_report_ms) {
    do_measurement_locked();
    next_report_ms = started + measurement_interval_ms;
  } else {
    sensor_data_t ambient = {0};
    if (update_compensation(&ambient)) {
      set_sensor_fault(ambient.error_code);
    }
  }
  /* Continuous STCC4 runs at 1 Hz. Updating compensation does not publish a
   * sample, read the battery, or feed software ASC. Account for I2C duration so
   * a 1 s target does not drift to 1 s plus the measurement execution time. */
  uint32_t period = use_single_shot_mode ? measurement_interval_ms : 1000;
  int64_t delay = period - (k_uptime_get() - started);
  if (running) {
    health_progress(HEALTH_SENSOR, (delay > 0 ? delay : 1) + 35000);
    k_work_schedule(&sensor_work, K_MSEC(delay > 0 ? delay : 1));
  }
  k_mutex_unlock(&meas_mutex);
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

  k_mutex_lock(&meas_mutex, K_FOREVER);
  measurement_interval_ms = interval_ms;
  next_report_ms = 0;
  running = true;

  LOG_INF("Starting sensor measurements (interval: %u ms)", interval_ms);

  health_progress(HEALTH_SENSOR, 35000);
  /* Do first measurement immediately */
  k_work_schedule(&sensor_work, K_NO_WAIT);
  k_mutex_unlock(&meas_mutex);

  return 0;
}

/**
 * @brief Stop periodic measurements and cancel scheduled work.
 */
void sensor_manager_stop(void)
{
  k_mutex_lock(&meas_mutex, K_FOREVER);
  recovery_enabled = false;
  settling_observe(false);
  running = false;
  health_progress(HEALTH_SENSOR, 0);
  k_mutex_unlock(&meas_mutex);
  /* Called by main for soft-off/lockout, never from the sensor work handler. */
  struct k_work_sync sync;
  k_work_cancel_delayable_sync(&recovery_work, &sync);
  k_work_cancel_delayable_sync(&sensor_work, &sync);
  k_mutex_lock(&meas_mutex, K_FOREVER);
  int rc = stcc4_enter_sleep_mode();
  if (rc) { LOG_ERR("STCC4 sleep failed: %d", rc); }
  k_mutex_unlock(&meas_mutex);
  LOG_INF("Sensor measurements stopped; wake requires normal reboot");
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
  return do_measurement();
}

int sensor_manager_perform_frc(uint16_t target_ppm, int16_t *applied_correction)
{
  if (!initialized)
  {
    return -ENODEV;
  }

  /* Serialize against reads and native policy changes. */
  k_mutex_lock(&meas_mutex, K_FOREVER);
#if defined(CONFIG_AIRSPOT_STCC4_SINGLE_SHOT_SLEEP)
  /* FRC qualification requires measurements ending in idle, without sleep.
   * Use continuous measurement for FRC in the sleep comparison build. */
  if (use_single_shot_mode) {
    k_mutex_unlock(&meas_mutex);
    return -EBUSY;
  }
#endif
  if (user_settings_get_native_asc_mode() == 2) {
    k_mutex_unlock(&meas_mutex);
    return -EBUSY;
  }

  /* Datasheet FRC sequence: a running continuous measurement must be stopped
   * first (single-shot leaves the sensor idle between shots, which is fine).
   * The caller is responsible for the >=30 s / >=30-shot operating
   * precondition by running the sensor before calling this. */
  bool was_continuous = !use_single_shot_mode;
  if (was_continuous)
  {
    stcc4_stop();
  }

  int16_t applied = 0;
  int ret = stcc4_perform_forced_recalibration(target_ppm, &applied);

  /* Restore continuous operation if we stopped it; use_single_shot_mode is left
   * unchanged so the sensor_manager mode state stays consistent. */
  if (was_continuous)
  {
    stcc4_start_continuous();
  }

  k_mutex_unlock(&meas_mutex);

  if (ret == 0 && applied_correction)
  {
    *applied_correction = applied;
  }
  return ret;
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
  *data = latest_data; /* Preserve startup fault details even without a sample. */
  if (latest_data.timestamp_ms == 0)
  {
    k_mutex_unlock(&data_mutex);
    return -ENODATA;
  }
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
int sensor_manager_set_power_mode(uint8_t power_mode)
{
  if (!initialized) return -ENODEV;
  if (power_mode > 3) return -EINVAL;
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

  /* Serialize mode commands with periodic/manual measurements. */
  k_mutex_lock(&meas_mutex, K_FOREVER);
  /* Switch STCC4 mode if needed */
  bool mode_changed = want_single_shot != use_single_shot_mode;
  if (mode_changed) settling_observe(false);
  if (mode_changed)
  {
    if (want_single_shot)
    {
      /* Switch to single-shot mode */
      int ret = stcc4_stop();
      if (ret != 0) {
        LOG_ERR("Failed to stop STCC4; keeping existing measurement mode");
        k_mutex_unlock(&meas_mutex);
        return ret;
      }
      use_single_shot_mode = true;
      LOG_INF("STCC4: Switched to single-shot mode (power saving)");
    }
    else
    {
      /* Seed fresh compensation before the first continuous conversion. */
      sensor_data_t ambient = {0};
      int ret = update_compensation(&ambient);
      if (!ret) ret = stcc4_start_continuous();
      if (ret == 0)
      {
        use_single_shot_mode = false;
        LOG_INF("STCC4: Switched to continuous mode (real-time)");
      }
      else
      {
        LOG_WRN("STCC4 continuous start failed: %d, keeping single-shot", ret);
        k_mutex_unlock(&meas_mutex);
        return ret;
      }
    }
  }

  if (new_interval != measurement_interval_ms || mode_changed)
  {
    measurement_interval_ms = new_interval;
    next_report_ms = k_uptime_get() + new_interval;

    if (new_interval == 0)
    {
      LOG_INF("Measurement mode: ON DEMAND (manual only)");
      /* Stop automatic measurements */
      if (running)
      {
        k_work_cancel_delayable(&sensor_work);
        running = false;
  health_progress(HEALTH_SENSOR, 0);
      }
    }
    else
    {
      LOG_INF("Measurement interval changed to %u ms (power mode %d)",
              new_interval, power_mode);

      /* Reschedule if was running, or start if switching from on-demand */
      running = true;
      /* A worker may already be running and waiting for meas_mutex. Cancel +
       * schedule can leave it CANCELLING, causing schedule() to do nothing.
       * Reschedule updates its deadline without cancelling that running work. */
      health_progress(HEALTH_SENSOR, (use_single_shot_mode ? measurement_interval_ms : 1000) + 35000);
      k_work_reschedule(&sensor_work,
          K_MSEC(use_single_shot_mode ? measurement_interval_ms : 1000));
    }
  }
  k_mutex_unlock(&meas_mutex);
  return 0;
}

K_MUTEX_DEFINE(mode_change_mutex);
int sensor_manager_change_power_mode(uint8_t mode)
{
  k_mutex_lock(&mode_change_mutex, K_FOREVER);
  uint8_t previous = user_settings_get_power_mode();
  int rc = sensor_manager_set_power_mode(mode);
  if (!rc) {
    rc = user_settings_set_power_mode(mode);
    if (rc && sensor_manager_set_power_mode(previous)) {
      /* Don't continue reporting a mode that hardware could not restore. */
      k_mutex_lock(&meas_mutex, K_FOREVER);
      initialized = false;
      k_mutex_unlock(&meas_mutex);
    }
  }
  k_mutex_unlock(&mode_change_mutex);
  return rc;
}

void sensor_manager_factory_probe(uint8_t *sht_ok, uint8_t *lps_ok, uint8_t *stcc_ok)
{
  k_mutex_lock(&meas_mutex, K_FOREVER);
  uint32_t serial = 0;
  uint8_t id = 0, product[9] = {0};
  *sht_ok = sht40_read_serial(&serial) == 0 && serial != 0;
  *lps_ok = lps22hh_read_id(&id) == 0 && id == 0xB3;
  *stcc_ok = stcc4_get_product_id(product, sizeof(product)) == 0;
  k_mutex_unlock(&meas_mutex);
}

void sensor_manager_start_recovery(void)
{
  k_mutex_lock(&meas_mutex, K_FOREVER);
  recovery_enabled = true;
  k_work_reschedule(&recovery_work, K_MSEC(5000));
  k_mutex_unlock(&meas_mutex);
}

void sensor_manager_get_recovery_diagnostic(sensor_recovery_diagnostic_t *out)
{
  k_mutex_lock(&meas_mutex, K_FOREVER);
  *out = recovery_diagnostic;
  int64_t due = recovery_after;
  if (failed_since >= 0 && due < failed_since + 30000)
    due = failed_since + 30000;
  int64_t remaining = due - k_uptime_get();
  out->retry_in_seconds = recovery_enabled && failed_since >= 0 && remaining > 0
      ? (uint16_t)((remaining + 999) / 1000) : 0;
  k_mutex_unlock(&meas_mutex);
}

static bool recovery_due(void)
{
  return recovery_enabled && failed_since >= 0 &&
      k_uptime_get() - failed_since >= 30000 && k_uptime_get() >= recovery_after;
}

static void recovery_work_handler(struct k_work *work)
{
  ARG_UNUSED(work);
  k_mutex_lock(&meas_mutex, K_FOREVER);
  bool due = recovery_due();
  k_mutex_unlock(&meas_mutex);
  if (due) {
    /* Same ordering as saved power-mode changes. The calibration lease also
     * excludes native-ASC changes, user resets, and manual calibration. */
    k_mutex_lock(&mode_change_mutex, K_FOREVER);
    int lease = software_asc_begin_manual();
    if (!lease) {
      k_mutex_lock(&meas_mutex, K_FOREVER);
      if (recovery_due()) {
        uint8_t fault = latest_data.error_code;
        recovery_diagnostic.stage = 1;
        recovery_diagnostic.fault = fault;
        if (recovery_diagnostic.total_attempts < UINT32_MAX)
          recovery_diagnostic.total_attempts++;
        if (recovery_diagnostic.consecutive_attempts < UINT16_MAX)
          recovery_diagnostic.consecutive_attempts++;
        LOG_WRN("Sensor recovery attempt %u, fault 0x%02x",
                recovery_diagnostic.total_attempts, fault);
        bool restore_mode = !initialized;
        int ret = 0;
        if (!initialized) {
          ret = sensor_manager_init_locked();
        } else if (fault == SENSOR_FAULT_AMBIENT) {
          ret = sht40_soft_reset();
          if (!ret) ret = sht40_init();
        } else if (fault == SENSOR_FAULT_PRESSURE) {
          ret = lps22hh_soft_reset();
          if (!ret) ret = lps22hh_init();
        } else {
          /* Volatile sleep/wake reset: never use factory_reset or the user
           * reset helper, which can clear the saved host correction. */
          ret = stcc4_reset();
          if (!ret) {
            use_single_shot_mode = true;
            restore_mode = true;
            ret = configure_stcc4_asc(user_settings_get_native_asc_mode() == 2);
          }
          if (ret) initialized = false;
        }
        if (!ret && restore_mode) {
          k_mutex_unlock(&meas_mutex);
          ret = sensor_manager_set_power_mode(user_settings_get_power_mode());
          k_mutex_lock(&meas_mutex, K_FOREVER);
          if (ret) initialized = false;
        }
        if (!ret) {
          /* Discard the previous failed compensation window; this attempt
           * must obtain and publish a complete fresh set of readings. */
          compensation_failed_since_report = false;
          ret = do_measurement_locked();
        }
        recovery_diagnostic.error = ret;
        recovery_diagnostic.stage = ret ? 3 : 2;
        diagnostics_record(DIAG_RECOVERY_RESULT,
            ((uint32_t)fault << 24) | ((uint32_t)recovery_diagnostic.stage << 16) | (uint16_t)ret);
        if (ret) {
          set_sensor_fault(latest_data.error_code ? latest_data.error_code : fault);
          /* 30s, 60s, 120s, then every 5min while powered. A permanent fault
           * remains visible without a reboot loop or excessive battery use. */
          unsigned attempts = recovery_diagnostic.consecutive_attempts;
          uint32_t delay = attempts < 4 ? (30000U << (attempts - 1)) : 300000U;
          recovery_after = k_uptime_get() + delay;
          LOG_WRN("Sensor recovery failed: %d; retry in %u ms", ret, delay);
        } else {
          LOG_INF("Sensor recovery verified by fresh measurement");
        }
      }
      k_mutex_unlock(&meas_mutex);
      software_asc_end_manual();
    }
    k_mutex_unlock(&mode_change_mutex);
  }
  k_mutex_lock(&meas_mutex, K_FOREVER);
  if (recovery_enabled) k_work_reschedule(&recovery_work, K_MSEC(5000));
  k_mutex_unlock(&meas_mutex);
}

/**
 * @brief Reset the CO2 sensor by reinitializing the driver.
 *
 * @return 0 on success, negative error code on failure.
 */
static sensor_reset_diagnostic_t reset_diagnostic;
void sensor_manager_get_reset_diagnostic(sensor_reset_diagnostic_t *diagnostic)
{
  if (!diagnostic) return;
  k_mutex_lock(&meas_mutex, K_FOREVER);
  *diagnostic = reset_diagnostic;
  k_mutex_unlock(&meas_mutex);
}

static int reset_co2_sensor(bool erase_native_history)
{
  int lease = software_asc_begin_manual();
  if (lease) return lease;
  if (!initialized) {
    int rc = sensor_manager_init();
    if (rc) { software_asc_end_manual(); return rc; }
  }
  k_mutex_lock(&meas_mutex, K_FOREVER);
  sensor_reset_diagnostic_t diagnostic = {.stage = 1};
  settling_observe(false);
  k_mutex_lock(&data_mutex, K_FOREVER);
  latest_data.valid = false;
  k_mutex_unlock(&data_mutex);
  next_report_ms = 0;
  int ret = lps22hh_soft_reset();
  if (!ret) ret = lps22hh_init();
  if (ret) set_sensor_fault(SENSOR_FAULT_PRESSURE);
  bool stopped = false;
  if (!ret) {
    diagnostic.stage = 2;
    if (erase_native_history) {
      ret = stcc4_stop();
      stopped = !ret;
      if (!ret) {
        diagnostic.stage = 3;
        ret = settling_prepare_erase(user_settings_is_time_set() ?
            user_settings_get_time_now() : 0);
        if (!ret) {
          ret = stcc4_factory_reset();
          if (!ret) {
            int tracked = settling_erased();
            int configured = user_settings_start_settling();
            ret = tracked ? tracked : configured;
          }
        }
        stcc4_reset_diagnostic_t erase;
        stcc4_get_reset_diagnostic(&erase);
        diagnostic.erase_stage = erase.stage;
        diagnostic.sensor_result = erase.result;
      }
    } else {
      ret = stcc4_reset();
      stopped = !ret;
    }
    if (ret) set_sensor_fault(SENSOR_FAULT_CO2);
  }
  /* A failed erase still leaves a successfully stopped sensor idle. Restore
   * its selected ASC policy and sampling, preserving the original error. */
  int recovery = 0;
  bool restore_sampling = false;
  if (stopped) {
    use_single_shot_mode = true;
    if (!ret) diagnostic.stage = 4;
    recovery = configure_stcc4_asc(user_settings_get_native_asc_mode() == 2);
    restore_sampling = !recovery;
    if (recovery) {
      set_sensor_fault(SENSOR_FAULT_COMPENSATION);
      initialized = false;
    }
    if (!ret) ret = recovery;
  }
  if (!ret && erase_native_history && !user_settings_get_native_asc_mode()) {
    diagnostic.stage = 5;
    ret = user_settings_set_co2_correction(0);
  }
  k_mutex_unlock(&meas_mutex);
  if (restore_sampling) {
    if (!ret) diagnostic.stage = 6;
    recovery = sensor_manager_set_power_mode(user_settings_get_power_mode());
    if (!ret) ret = recovery;
  }
  diagnostic.error = ret;
  diagnostic.recovery_error = recovery;
  if (!ret) diagnostic.stage = 7;
  if (erase_native_history) {
    k_mutex_lock(&meas_mutex, K_FOREVER);
    reset_diagnostic = diagnostic;
    k_mutex_unlock(&meas_mutex);
  }
  software_asc_end_manual();
  return ret;
}

int sensor_manager_reset_co2_sensor(void)
{
  return reset_co2_sensor(false);
}

int sensor_manager_reset_native_calibration(void)
{
  return reset_co2_sensor(true);
}


int sensor_manager_factory_erase_calibration(void)
{
  int rc = software_asc_begin_manual();
  if (rc) return rc;
  sensor_manager_stop();
  k_mutex_lock(&meas_mutex, K_FOREVER);
  rc = stcc4_init();
  if (!rc) rc = stcc4_exit_sleep_mode();
  if (!rc) rc = stcc4_stop();
  if (!rc) rc = settling_prepare_erase(user_settings_is_time_set() ?
      user_settings_get_time_now() : 0);
  if (!rc) rc = stcc4_factory_reset();
  if (!rc) rc = settling_erased();
  k_mutex_unlock(&meas_mutex);
  software_asc_end_manual();
  /* Remain stopped until reset finishes. Failure is reported, not hidden. */
  return rc;
}
