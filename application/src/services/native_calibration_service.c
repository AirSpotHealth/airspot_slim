/* Rob evaluation profile: manual calibration is performed inside the STCC4.
 * The returned correction is reported, never applied again by the host. */
#include <errno.h>
#include <zephyr/kernel.h>
#include <services/calibration_service.h>
#include <services/software_asc.h>
#include <services/sensor_manager.h>
#include <services/user_settings.h>

#if defined(CONFIG_AIRSPOT_NATIVE_TEST_MODES)
#define FRC_PREPARE_SECONDS 60
#else
#define FRC_PREPARE_SECONDS 180
#endif
#if defined(CONFIG_AIRSPOT_NATIVE_TEST_MODES)
#define MAX_SAMPLE_GAP_MS 2500
#else
#define MAX_SAMPLE_GAP_MS 12000
#endif
K_MUTEX_DEFINE(calib_mutex);
static calibration_callbacks_t callbacks;
static bool active, collecting, sampling_failed;
static uint16_t target, samples;
static uint8_t previous_mode;
static int64_t started_ms, last_sample_ms;
static void calibration_tick(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(calib_work, calibration_tick);

void calibration_service_init(const calibration_callbacks_t *cb)
{
  callbacks = cb ? *cb : (calibration_callbacks_t){0};
}

bool calibration_service_is_active(void)
{
  return software_asc_manual_active();
}

int calibration_service_start(uint16_t target_ppm)
{
#if defined(CONFIG_AIRSPOT_CONTINUOUS_ALL_MODES) && !defined(CONFIG_AIRSPOT_NATIVE_TEST_MODES)
  /* Current customer policy is continuous learning followed by fresh air.
   * An older app must not silently invoke FRC in this qualification build. */
  return -ENOTSUP;
#endif
  if (target_ppm > 32000) return -EINVAL;
  k_mutex_lock(&calib_mutex, K_FOREVER);
  if (active
#if !defined(CONFIG_AIRSPOT_NATIVE_TEST_MODES)
      || user_settings_get_native_asc_mode() == 2
#endif
  ) {
    k_mutex_unlock(&calib_mutex);
    return -EBUSY;
  }
  int rc = software_asc_set_target(target_ppm);
  if (!rc) {
#if defined(CONFIG_AIRSPOT_NATIVE_TEST_MODES)
    rc = software_asc_begin_manual();
#else
    rc = software_asc_begin_calibration();
#endif
  }
  if (rc) { k_mutex_unlock(&calib_mutex); return rc; }
  active = true;
  collecting = false;
  target = target_ppm;
  samples = 0;
  sampling_failed = false;
  previous_mode = user_settings_get_power_mode();
  k_mutex_unlock(&calib_mutex);

  /* Change acquisition temporarily, without overwriting the saved preference.
   * Never hold calib_mutex while taking the sensor mutex (callback lock order). */
  rc = sensor_manager_set_power_mode(3);
  k_mutex_lock(&calib_mutex, K_FOREVER);
  started_ms = last_sample_ms = k_uptime_get();
  if (rc) active = false;
  else collecting = true;
  k_mutex_unlock(&calib_mutex);
  if (rc) { software_asc_end_manual(); return rc; }
  if (callbacks.on_start) callbacks.on_start();
  if (callbacks.on_countdown) callbacks.on_countdown(FRC_PREPARE_SECONDS);
  k_work_schedule(&calib_work, K_SECONDS(1));
  return 0;
}

void calibration_service_process_measurement(uint16_t co2_ppm)
{
  k_mutex_lock(&calib_mutex, K_FOREVER);
  if (active && collecting) {
    int64_t now = k_uptime_get();
    if (co2_ppm == 0 || co2_ppm > 32000 ||
        (samples && now - last_sample_ms > MAX_SAMPLE_GAP_MS)) sampling_failed = true;
    last_sample_ms = now;
    if (samples < UINT16_MAX) samples++;
  }
  k_mutex_unlock(&calib_mutex);
}

static void calibration_tick(struct k_work *work)
{
  ARG_UNUSED(work);
  k_mutex_lock(&calib_mutex, K_FOREVER);
  if (!active) { k_mutex_unlock(&calib_mutex); return; }
  int64_t now = k_uptime_get();
  int64_t remaining_ms = FRC_PREPARE_SECONDS * 1000 - (now - started_ms);
  uint16_t remaining = remaining_ms > 0 ? (remaining_ms + 999) / 1000 : 0;
  bool valid = !sampling_failed && samples >= 30 &&
      now - last_sample_ms <= MAX_SAMPLE_GAP_MS;
  if (!remaining) collecting = false;
  uint16_t reference = target;
  uint8_t restore_mode = previous_mode;
  k_mutex_unlock(&calib_mutex);

  if (callbacks.on_countdown) callbacks.on_countdown(remaining);
  if (remaining) {
    k_work_schedule(&calib_work, K_SECONDS(1));
    return;
  }
  int16_t correction = 0;
  int rc = valid ? sensor_manager_perform_frc(reference, &correction) : -ENODATA;
  int restore = sensor_manager_set_power_mode(restore_mode);
  if (!rc) rc = restore;
  k_mutex_lock(&calib_mutex, K_FOREVER);
  active = false;
  k_mutex_unlock(&calib_mutex);
  software_asc_end_manual();
  if (rc && callbacks.on_error) callbacks.on_error(rc);
  if (!rc && callbacks.on_done) callbacks.on_done(correction);
}
