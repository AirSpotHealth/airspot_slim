/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * User Settings - Persistent storage via NVS
 */

#ifndef USER_SETTINGS_H
#define USER_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Maximum device name length */
#define USER_SETTINGS_NAME_MAX_LEN 20

  /**
   * @brief User settings structure (native_asc_mode controls sensor learning independently)
   */
  typedef struct
  {
    char device_name[USER_SETTINGS_NAME_MAX_LEN + 1];
    uint8_t power_mode;  /* 0=on-demand, 1=low, 2=mid, 3=high */
    /* Calibration settings (matches legacy behavior) */
    int16_t co2_correction; /* CC value (ppm) */
    uint16_t calib_target;  /* Calibration target (ppm) */
    uint8_t auto_calibrate; /* AirSpot software ASC enabled (0/1); never hardware ASC */
    uint8_t asc_day_count;  /* Legacy stored field; current ASC progress is volatile */
    uint16_t co2_yellow_start; /* Green/amber LED boundary (ppm) */
    uint16_t co2_red_start;    /* Amber/red LED boundary (ppm) */
    uint8_t native_asc_mode; /* 0=legacy host correction, 1=native paused, 2=native learning */
    uint8_t paired;            /* 1 = a phone has connected at least once */
  } user_settings_t;

  /**
   * @brief Initialize user settings
   * Loads settings from NVS or uses defaults if not found.
   * @return 0 on success, negative errno on failure
   */
  int user_settings_init(void);

  uint8_t user_settings_get_native_asc_mode(void);
  int user_settings_set_native_asc_mode(uint8_t mode);
  /* Commit an explicit manual correction and leave native baseline-only mode. */
  int user_settings_commit_manual_correction(int16_t correction);

  /**
   * @brief Copy a consistent settings snapshot
   */
  void user_settings_get_snapshot(user_settings_t *out);

  /**
   * @brief Get device name
   */
  const char *user_settings_get_device_name(void);

  /**
   * @brief Set device name (saves to NVS)
   */
  int user_settings_set_device_name(const char *name);

  /**
   * @brief Get power mode
   */
  uint8_t user_settings_get_power_mode(void);

  /**
   * @brief Set power mode (saves to NVS)
   */
  int user_settings_set_power_mode(uint8_t mode);

  /**
   * @brief Set timebase (seconds since 2000-01-01, synced from app)
   * @param time Seconds since 2000-01-01 00:00:00
   */
  void user_settings_set_timebase(uint32_t time);

  /**
   * @brief Get current time in seconds since 2000-01-01
   */
  uint32_t user_settings_get_time_now(void);

  /**
   * @brief Check if time has been set by the app
   */
  bool user_settings_is_time_set(void);

  /**
   * @brief Increment time by 1 second (called from timer)
   */
  void user_settings_inc_second(void);

  /**
   * @brief Get CO2 correction (CC value)
   */
  int16_t user_settings_get_co2_correction(void);

  /**
   * @brief Set CO2 correction (CC value)
   */
  int user_settings_set_co2_correction(int16_t correction);

  /**
   * @brief Get calibration target (ppm)
   */
  uint16_t user_settings_get_calib_target(void);

  /**
   * @brief Set calibration target (ppm)
   */
  int user_settings_set_calib_target(uint16_t target);

  /**
   * @brief Get CO2 green/amber threshold (ppm)
   */
  uint16_t user_settings_get_co2_yellow_start(void);

  /**
   * @brief Get CO2 amber/red threshold (ppm)
   */
  uint16_t user_settings_get_co2_red_start(void);

  /**
   * @brief Set CO2 LED thresholds (ppm). yellow_start must be < red_start.
   */
  int user_settings_set_co2_thresholds(uint16_t yellow_start, uint16_t red_start);

  /**
   * @brief Get auto-calibration (ASC) enable flag
   */
  uint8_t user_settings_get_auto_calibrate(void);

  /**
   * @brief Set auto-calibration (ASC) enable flag
   */
  int user_settings_set_auto_calibrate(uint8_t enabled);

  /**
   * @brief Get ASC day count
   */
  uint8_t user_settings_get_asc_day_count(void);

  /**
   * @brief Set ASC day count
   */
  int user_settings_set_asc_day_count(uint8_t day_count);

  /**
   * @brief Apply CO2 correction with clamp
   */
  uint16_t user_settings_apply_co2_correction(uint16_t raw_ppm);

  /**
   * @brief Has a phone ever connected to this device? ("paired" for the
   *        status LED: blue = paired, white = never set up with the app)
   */
  bool user_settings_is_paired(void);

  /**
   * @brief Mark the device as paired/unpaired (persisted; no-op if unchanged)
   */
  int user_settings_set_paired(bool paired);

  /**
   * @brief Factory reset - clear all user settings to defaults
   *
   * Restores device name, power mode, calibration and LED thresholds.
   * Does NOT reboot - caller should reboot after if needed.
   *
   * @return 0 on success, negative errno on failure
   */
  int user_settings_factory_reset(void);
  int user_settings_reset_preferences(void);
  int user_settings_start_settling(void);

#ifdef __cplusplus
}
#endif

#endif /* USER_SETTINGS_H */
