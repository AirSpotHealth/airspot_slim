/*
 * Copyright (c) 2026 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Calibration Service - Legacy math-based calibration
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <services/calibration_service.h>
#include <services/software_asc.h>
#include <services/data_log.h>
#include <services/sensor_manager.h>
#include <services/user_settings.h>

LOG_MODULE_REGISTER(calibration_service, CONFIG_AIRSPOT_LOG_LEVEL);

#define CALIB_DURATION_FAST_SEC 241
#define CALIB_DURATION_SLOW_SEC 541

static calibration_callbacks_t callbacks = {0};

K_MUTEX_DEFINE(calib_mutex);
static bool calib_active = false;
static uint16_t calib_target_ppm = 426;
static uint32_t calib_sum = 0;
static uint16_t calib_count = 0;
static uint16_t calib_remaining_sec = 0;
static uint8_t calib_prev_power_mode = 0;
static bool calib_power_mode_changed = false;

static void calibration_tick_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(calib_tick_work, calibration_tick_work_handler);

static void calibration_complete(void) {
  k_mutex_lock(&calib_mutex, K_FOREVER);
  if (!calib_active) { k_mutex_unlock(&calib_mutex); return; }
  int16_t old_correction = user_settings_get_co2_correction();
  int16_t new_correction = old_correction;
  uint16_t avg = 0;

  if (calib_count > 0) avg = (uint16_t)(calib_sum / calib_count);
  int16_t correction_delta = 0;
  int rc = software_asc_finish_manual(calib_target_ppm, avg, calib_count > 0,
                                     &correction_delta);
  new_correction = user_settings_get_co2_correction();

  LOG_INF(
      "Calibration done: target=%u avg=%u samples=%u old=%d new=%d delta=%d",
      calib_target_ppm, avg, calib_count, old_correction, new_correction,
      correction_delta);

  /* Log correction record */
  uint32_t timestamp = user_settings_is_time_set()
                           ? user_settings_get_time_now()
                           : (k_uptime_get_32() / 1000);
  if (!rc) (void)data_log_store_record(timestamp,
                              (uint16_t)(new_correction < 0 ? -new_correction
                                                            : new_correction),
                              RECORD_TYPE_CALIBRATION_CORRECTION);

  bool restore_mode = calib_power_mode_changed;
  uint8_t previous_mode = calib_prev_power_mode;
  calib_sum = 0;
  calib_count = 0;
  calib_remaining_sec = 0;
  calib_power_mode_changed = false;
  k_mutex_unlock(&calib_mutex);

  /* Never acquire the sensor mutex while holding calib_mutex: measurements
   * acquire those locks in the opposite order when delivering a sample. */
  if (restore_mode) {
    int mode_rc = user_settings_set_power_mode(previous_mode);
    if (!mode_rc) sensor_manager_set_power_mode(previous_mode);
    else if (!rc) rc = mode_rc;
  }
  k_mutex_lock(&calib_mutex, K_FOREVER);
  calib_active = false;
  k_mutex_unlock(&calib_mutex);
  if (rc && callbacks.on_error) callbacks.on_error(rc);
  if (!rc && callbacks.on_done) callbacks.on_done(correction_delta);

}

void calibration_service_init(const calibration_callbacks_t *cb) {
  if (cb) {
    callbacks = *cb;
  } else {
    callbacks.on_start = NULL;
    callbacks.on_countdown = NULL;
    callbacks.on_done = NULL;
    callbacks.on_error = NULL;
  }
}

int calibration_service_start(uint16_t target_ppm) {
  k_mutex_lock(&calib_mutex, K_FOREVER);
  if (calib_active) {
    k_mutex_unlock(&calib_mutex);
    return -EBUSY;
  }

  uint16_t prev_target = user_settings_get_calib_target();
  calib_target_ppm = target_ppm;
  if (target_ppm != prev_target) {
    int rc = software_asc_set_target(target_ppm);
    if (rc) { k_mutex_unlock(&calib_mutex); return rc; }
  }

  int rc = software_asc_begin_manual();
  if (rc) { k_mutex_unlock(&calib_mutex); return rc; }

  {
    uint32_t ts = user_settings_is_time_set() ? user_settings_get_time_now()
                                             : (k_uptime_get_32() / 1000);
    (void)data_log_store_record(ts, target_ppm,
                                RECORD_TYPE_MANUAL_CALIB_START);
  }

  calib_sum = 0;
  calib_count = 0;

  uint8_t power_mode = user_settings_get_power_mode();
  calib_prev_power_mode = power_mode;
  calib_power_mode_changed = false;

  if (power_mode == 0) {
    /* On-demand -> force mid mode for calibration sampling */
    rc = user_settings_set_power_mode(2);
    if (rc) {
      software_asc_end_manual();
      k_mutex_unlock(&calib_mutex);
      return rc;
    }
    power_mode = 2;
    calib_power_mode_changed = true;
  }

  calib_remaining_sec = (power_mode == 1) ? CALIB_DURATION_SLOW_SEC
                                          : CALIB_DURATION_FAST_SEC;
  calib_active = true;
  bool change_mode = calib_power_mode_changed;
  uint16_t remaining = calib_remaining_sec;
  k_mutex_unlock(&calib_mutex);
  if (change_mode) sensor_manager_set_power_mode(2);

  LOG_INF("Calibration started: target=%u duration=%us mode=%u", target_ppm,
          remaining, power_mode);

  if (callbacks.on_start) {
    callbacks.on_start();
  }
  if (callbacks.on_countdown) {
    callbacks.on_countdown(remaining);
  }

  k_work_schedule(&calib_tick_work, K_SECONDS(1));
  return 0;
}

void calibration_service_process_measurement(uint16_t co2_ppm) {
  k_mutex_lock(&calib_mutex, K_FOREVER);
  if (calib_active && co2_ppm > 0 && calib_count < UINT16_MAX &&
      calib_sum <= UINT32_MAX - co2_ppm) {
    calib_sum += co2_ppm;
    calib_count++;
  }
  k_mutex_unlock(&calib_mutex);
}

bool calibration_service_is_active(void) { return software_asc_manual_active(); }

static void calibration_tick_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  k_mutex_lock(&calib_mutex, K_FOREVER);
  if (!calib_active) {
    k_mutex_unlock(&calib_mutex);
    return;
  }

  if (calib_remaining_sec > 0) {
    calib_remaining_sec--;
  }

  uint16_t remaining = calib_remaining_sec;
  k_mutex_unlock(&calib_mutex);
  LOG_INF("Calibration countdown: %us remaining", remaining);

  if (callbacks.on_countdown) {
    callbacks.on_countdown(remaining);
  }

  if (remaining == 0) {
    calibration_complete();
    return;
  }

  k_work_schedule(&calib_tick_work, K_SECONDS(1));
}
