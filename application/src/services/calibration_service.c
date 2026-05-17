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
#include <services/data_log.h>
#include <services/sensor_manager.h>
#include <services/user_settings.h>

LOG_MODULE_REGISTER(calibration_service, CONFIG_AIRSPOT_LOG_LEVEL);

#define CALIB_DURATION_FAST_SEC 241
#define CALIB_DURATION_SLOW_SEC 541

static calibration_callbacks_t callbacks = {0};

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
  int16_t old_correction = user_settings_get_co2_correction();
  int16_t new_correction = old_correction;
  uint16_t avg = 0;

  if (calib_count > 0) {
    avg = (uint16_t)(calib_sum / calib_count);
    new_correction = (int16_t)calib_target_ppm - (int16_t)avg;
    user_settings_set_co2_correction(new_correction);
  }

  /* Legacy protocol semantics: report delta CC to the app. */
  int16_t correction_delta = new_correction - old_correction;

  LOG_INF(
      "Calibration done: target=%u avg=%u samples=%u old=%d new=%d delta=%d",
      calib_target_ppm, avg, calib_count, old_correction, new_correction,
      correction_delta);

  /* Log correction record */
  uint32_t timestamp = user_settings_is_time_set()
                           ? user_settings_get_time_now()
                           : (k_uptime_get_32() / 1000);
  (void)data_log_store_record(timestamp,
                              (uint16_t)(new_correction < 0 ? -new_correction
                                                            : new_correction),
                              RECORD_TYPE_CALIBRATION_CORRECTION);

  if (callbacks.on_done) {
    callbacks.on_done(correction_delta);
  }

  if (calib_power_mode_changed) {
    user_settings_set_power_mode(calib_prev_power_mode);
    sensor_manager_set_power_mode(calib_prev_power_mode);
  }

  calib_active = false;
  calib_sum = 0;
  calib_count = 0;
  calib_remaining_sec = 0;
  calib_power_mode_changed = false;
}

void calibration_service_init(const calibration_callbacks_t *cb) {
  if (cb) {
    callbacks = *cb;
  } else {
    callbacks.on_start = NULL;
    callbacks.on_countdown = NULL;
    callbacks.on_done = NULL;
  }
}

int calibration_service_start(uint16_t target_ppm) {
  if (calib_active) {
    return -EBUSY;
  }

  uint16_t prev_target = user_settings_get_calib_target();
  calib_target_ppm = target_ppm;
  if (target_ppm != prev_target) {
    user_settings_set_calib_target(target_ppm);
    uint32_t ts = user_settings_is_time_set() ? user_settings_get_time_now()
                                             : (k_uptime_get_32() / 1000);
    (void)data_log_store_record(ts, target_ppm,
                                RECORD_TYPE_CALIBRATION_TARGET_UPDATE);
  }

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
    user_settings_set_power_mode(2);
    sensor_manager_set_power_mode(2);
    power_mode = 2;
    calib_power_mode_changed = true;
  }

  calib_remaining_sec = (power_mode == 1) ? CALIB_DURATION_SLOW_SEC
                                          : CALIB_DURATION_FAST_SEC;
  calib_active = true;

  LOG_INF("Calibration started: target=%u duration=%us mode=%u", calib_target_ppm,
          calib_remaining_sec, power_mode);

  if (callbacks.on_start) {
    callbacks.on_start();
  }
  if (callbacks.on_countdown) {
    callbacks.on_countdown(calib_remaining_sec);
  }

  k_work_schedule(&calib_tick_work, K_SECONDS(1));
  return 0;
}

void calibration_service_process_measurement(uint16_t co2_ppm) {
  if (!calib_active || co2_ppm == 0) {
    return;
  }

  calib_sum += co2_ppm;
  calib_count++;
}

bool calibration_service_is_active(void) { return calib_active; }

static void calibration_tick_work_handler(struct k_work *work) {
  ARG_UNUSED(work);

  if (!calib_active) {
    return;
  }

  if (calib_remaining_sec > 0) {
    calib_remaining_sec--;
  }

  LOG_INF("Calibration countdown: %us remaining", calib_remaining_sec);

  if (callbacks.on_countdown) {
    callbacks.on_countdown(calib_remaining_sec);
  }

  if (calib_remaining_sec == 0) {
    calibration_complete();
    return;
  }

  k_work_schedule(&calib_tick_work, K_SECONDS(1));
}
