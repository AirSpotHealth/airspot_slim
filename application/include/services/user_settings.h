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
extern "C" {
#endif

/* Maximum device name length */
#define USER_SETTINGS_NAME_MAX_LEN 20

/**
 * @brief User settings structure
 */
typedef struct {
  char device_name[USER_SETTINGS_NAME_MAX_LEN + 1];
  uint8_t power_mode;  /* 0=low, 1=normal */
  uint8_t flight_mode; /* 0=off, 1=on */
  uint8_t dnd_enabled; /* Do Not Disturb */
  uint8_t dnd_start_hour;
  uint8_t dnd_start_minute;
  uint8_t dnd_end_hour;
  uint8_t dnd_end_minute;
  /* Stub settings (no buzzer/vibrator on Slim) */
  uint8_t buzzer_enabled;
  uint8_t vibrator_enabled;
} user_settings_t;

/**
 * @brief Initialize user settings
 * Loads settings from NVS or uses defaults if not found.
 * @return 0 on success, negative errno on failure
 */
int user_settings_init(void);

/**
 * @brief Get pointer to current settings (read-only)
 */
const user_settings_t *user_settings_get(void);

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
 * @brief Get flight mode
 */
uint8_t user_settings_get_flight_mode(void);

/**
 * @brief Set flight mode (saves to NVS)
 */
int user_settings_set_flight_mode(uint8_t enabled);

/**
 * @brief Check if DND is currently active
 */
bool user_settings_is_dnd_active(void);

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
 * @brief Get current hour (24-hour format)
 */
uint8_t user_settings_get_time_now_hour24(void);

/**
 * @brief Get current minute
 */
uint8_t user_settings_get_time_now_minute(void);

/**
 * @brief Check if time has been set by the app
 */
bool user_settings_is_time_set(void);

/**
 * @brief Increment time by 1 second (called from timer)
 */
void user_settings_inc_second(void);

/**
 * @brief Set DND settings (saves to NVS)
 */
int user_settings_set_dnd(uint8_t enabled, uint8_t start_hour,
                          uint8_t start_minute, uint8_t end_hour,
                          uint8_t end_minute);

/**
 * @brief Get buzzer state (stub - always returns saved value)
 */
uint8_t user_settings_get_buzzer(void);

/**
 * @brief Set buzzer state (stub - saves but no hardware effect)
 */
int user_settings_set_buzzer(uint8_t enabled);

/**
 * @brief Get vibrator state (stub - always returns saved value)
 */
uint8_t user_settings_get_vibrator(void);

/**
 * @brief Set vibrator state (stub - saves but no hardware effect)
 */
int user_settings_set_vibrator(uint8_t enabled);

#ifdef __cplusplus
}
#endif

#endif /* USER_SETTINGS_H */
