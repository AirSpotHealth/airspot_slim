/* AirSpot software ASC; mirrors Screen's three-sample / seven-day policy. */
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <services/software_asc.h>
#include <services/user_settings.h>
#include <services/data_log.h>

LOG_MODULE_REGISTER(software_asc, CONFIG_AIRSPOT_LOG_LEVEL);
K_MUTEX_DEFINE(asc_mutex);
static struct {
  uint16_t samples[3];
  uint8_t count, days;
  uint32_t lowest, records;
  uint32_t last_calendar_day;
  int64_t last_check_ms;
  uint16_t check_seconds;
  bool manual;
} asc;

static void restart_locked(void) {
  memset(asc.samples, 0, sizeof(asc.samples));
  asc.count = asc.days = 0;
  asc.lowest = UINT32_MAX;
  asc.records = 0;
  asc.last_check_ms = k_uptime_get();
}
static void log_record(int32_t value, record_type_t type) {
  uint32_t ts = user_settings_is_time_set() ? user_settings_get_time_now()
                                           : (uint32_t)(k_uptime_get() / 1000);
  /* Match legacy history's magnitude encoding for correction events. */
  (void)data_log_store_record(ts, (uint16_t)(value < 0 ? -value : value), type);
}
static int commit_correction(int32_t value) {
  if (value < INT16_MIN || value > INT16_MAX) return -ERANGE;
  int rc = user_settings_set_co2_correction((int16_t)value);
  if (rc) LOG_ERR("Software calibration storage failed: %d", rc);
  return rc;
}
void software_asc_init(void) {
  k_mutex_lock(&asc_mutex, K_FOREVER);
  memset(&asc, 0, sizeof(asc));
  restart_locked();
  k_mutex_unlock(&asc_mutex);
}
int software_asc_set_enabled(uint8_t enabled) {
  k_mutex_lock(&asc_mutex, K_FOREVER);
  int rc = user_settings_set_auto_calibrate(enabled);
  if (!rc) {
    restart_locked();
    log_record(enabled != 0, RECORD_TYPE_SENSOR_AUTO_CALIBRATION);
  }
  k_mutex_unlock(&asc_mutex);
  return rc;
}
int software_asc_set_target(uint16_t target) {
  k_mutex_lock(&asc_mutex, K_FOREVER);
  if (asc.manual) {
    k_mutex_unlock(&asc_mutex);
    return -EBUSY;
  }
  if (target == user_settings_get_calib_target()) {
    k_mutex_unlock(&asc_mutex);
    return 0;
  }
  int rc = user_settings_set_calib_target(target);
  if (!rc) {
    restart_locked();
    log_record(target, RECORD_TYPE_CALIBRATION_TARGET_UPDATE);
  }
  k_mutex_unlock(&asc_mutex);
  return rc;
}
void software_asc_set_check_duration(uint16_t seconds) {
  k_mutex_lock(&asc_mutex, K_FOREVER);
  asc.check_seconds = seconds;
  asc.last_check_ms = k_uptime_get();
  k_mutex_unlock(&asc_mutex);
}
uint8_t software_asc_get_day_count(void) {
  k_mutex_lock(&asc_mutex, K_FOREVER);
  uint8_t days = asc.days;
  k_mutex_unlock(&asc_mutex);
  return days;
}
void software_asc_process_measurement(uint16_t raw_ppm) {
  k_mutex_lock(&asc_mutex, K_FOREVER);
  if (!user_settings_get_auto_calibrate() || asc.manual || raw_ppm == 0) goto done;
  int32_t corrected = (int32_t)raw_ppm + user_settings_get_co2_correction();
  uint16_t ppm = (uint16_t)CLAMP(corrected, 0, UINT16_MAX);
  if (asc.records < UINT32_MAX) asc.records++;
  asc.samples[2] = asc.samples[1];
  asc.samples[1] = asc.samples[0];
  asc.samples[0] = ppm;
  if (asc.count < 3) asc.count++;
  if (asc.count < 3) goto done;
  uint32_t average = ((uint32_t)asc.samples[0] + asc.samples[1] + asc.samples[2]) / 3;
  if (average < asc.lowest) asc.lowest = average;
  if (asc.lowest < 420 && !user_settings_get_flight_mode()) {
    int32_t adjustment = 420 - (int32_t)asc.lowest;
    int32_t next = (int32_t)user_settings_get_co2_correction() + adjustment;
    if (!commit_correction(next)) {
      log_record(adjustment, RECORD_TYPE_ASC_ADJUSTMENT);
      asc.lowest = 420;
      /* Old samples used the old correction; do not correct them twice. */
      asc.count = 0;
    }
  }
done:
  k_mutex_unlock(&asc_mutex);
}
void software_asc_tick(void) {
  k_mutex_lock(&asc_mutex, K_FOREVER);
  if (!user_settings_get_auto_calibrate() || asc.manual) goto done;
  if (asc.check_seconds) {
    int64_t now = k_uptime_get();
    if (user_settings_get_power_mode() == 0 ||
        now - asc.last_check_ms < (int64_t)asc.check_seconds * 1000) goto done;
    asc.last_check_ms = now;
  } else {
    if (!user_settings_is_time_set()) goto done;
    uint32_t now = user_settings_get_time_now();
    uint32_t day = now / 86400;
    /* High-water mark prevents duplicate days after backward clock changes. */
    if (now % 86400 < 86340 || day <= asc.last_calendar_day) goto done;
    asc.last_calendar_day = day;
    if (asc.records < 400) goto done;
  }
  /* Never calibrate against an uninitialized minimum, even in accelerated mode. */
  if (asc.lowest == UINT32_MAX) goto done;
  if (asc.days < 6) {
    asc.days++;
    asc.records = 0;
  } else {
    int32_t old = user_settings_get_co2_correction();
    int32_t target = user_settings_get_calib_target();
    int32_t next = old + target - (int32_t)asc.lowest;
    if (!commit_correction(next)) {
      log_record(target, RECORD_TYPE_CALIB_TARGET);
      log_record(asc.lowest, RECORD_TYPE_ASC_LOWEST);
      log_record(old, RECORD_TYPE_CALIBRATION_CORRECTION_OLD);
      log_record(next, RECORD_TYPE_CALIBRATION_CORRECTION);
      restart_locked();
    }
  }
done:
  k_mutex_unlock(&asc_mutex);
}
int software_asc_begin_manual(void) {
  k_mutex_lock(&asc_mutex, K_FOREVER);
  int rc = asc.manual ? -EBUSY : 0;
  if (!rc) asc.manual = true;
  k_mutex_unlock(&asc_mutex);
  return rc;
}
int software_asc_finish_manual(uint16_t target, uint16_t average, bool have_samples,
                               int16_t *delta) {
  k_mutex_lock(&asc_mutex, K_FOREVER);
  int32_t next = (int32_t)target - average;
  int32_t difference = next - user_settings_get_co2_correction();
  int rc;
  if (!have_samples) rc = -ENODATA;
  else if (difference < INT16_MIN || difference > INT16_MAX) rc = -ERANGE;
  else rc = commit_correction(next);
  if (!rc) *delta = (int16_t)difference;
  asc.manual = false;
  restart_locked();
  k_mutex_unlock(&asc_mutex);
  return rc;
}
void software_asc_end_manual(void) {
  k_mutex_lock(&asc_mutex, K_FOREVER);
  asc.manual = false;
  restart_locked();
  k_mutex_unlock(&asc_mutex);
}
bool software_asc_manual_active(void) {
  k_mutex_lock(&asc_mutex, K_FOREVER);
  bool active = asc.manual;
  k_mutex_unlock(&asc_mutex);
  return active;
}
