/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * User Settings Implementation - NVS-backed persistence
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <services/user_settings.h>

LOG_MODULE_REGISTER(user_settings, CONFIG_AIRSPOT_LOG_LEVEL);

/* Settings key prefix */
#define SETTINGS_KEY "airspot"

/* Default settings */
static user_settings_t settings = {
    .device_name = "AirSpot Slim",
    .power_mode = 0,  /* Low power (3 min interval) */
    .flight_mode = 0, /* Off */
    .dnd_enabled = 0, /* Off */
    .dnd_start_hour = 22,
    .dnd_start_minute = 0,
    .dnd_end_hour = 7,
    .dnd_end_minute = 0,
    .buzzer_enabled = 0,  /* Stub */
    .vibrator_enabled = 0 /* Stub */
};

static bool initialized = false;

/* Time management - seconds since 2000-01-01 00:00:00 */
static volatile uint32_t time_base = 0;
static bool time_set = false;

/* Settings handler - called when loading from NVS */
static int settings_set(const char *name, size_t len, settings_read_cb read_cb,
                        void *cb_arg)
{
  const char *next;
  int rc;

  if (settings_name_steq(name, "name", &next) && !next)
  {
    if (len > USER_SETTINGS_NAME_MAX_LEN)
    {
      len = USER_SETTINGS_NAME_MAX_LEN;
    }
    rc = read_cb(cb_arg, settings.device_name, len);
    settings.device_name[len] = '\0';
    return (rc >= 0) ? 0 : rc;
  }

  if (settings_name_steq(name, "power", &next) && !next)
  {
    rc = read_cb(cb_arg, &settings.power_mode, sizeof(settings.power_mode));
    return (rc >= 0) ? 0 : rc;
  }

  if (settings_name_steq(name, "flight", &next) && !next)
  {
    rc = read_cb(cb_arg, &settings.flight_mode, sizeof(settings.flight_mode));
    return (rc >= 0) ? 0 : rc;
  }

  if (settings_name_steq(name, "dnd", &next) && !next)
  {
    /* DND settings packed: enabled, start_h, start_m, end_h, end_m */
    uint8_t dnd_data[5];
    rc = read_cb(cb_arg, dnd_data, sizeof(dnd_data));
    if (rc >= 0)
    {
      settings.dnd_enabled = dnd_data[0];
      settings.dnd_start_hour = dnd_data[1];
      settings.dnd_start_minute = dnd_data[2];
      settings.dnd_end_hour = dnd_data[3];
      settings.dnd_end_minute = dnd_data[4];
    }
    return (rc >= 0) ? 0 : rc;
  }

  if (settings_name_steq(name, "buzzer", &next) && !next)
  {
    rc = read_cb(cb_arg, &settings.buzzer_enabled,
                 sizeof(settings.buzzer_enabled));
    return (rc >= 0) ? 0 : rc;
  }

  if (settings_name_steq(name, "vibrator", &next) && !next)
  {
    rc = read_cb(cb_arg, &settings.vibrator_enabled,
                 sizeof(settings.vibrator_enabled));
    return (rc >= 0) ? 0 : rc;
  }

  return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(airspot, SETTINGS_KEY, NULL, settings_set, NULL,
                               NULL);

int user_settings_init(void)
{
  if (initialized)
  {
    return 0;
  }

  int rc = settings_subsys_init();
  if (rc)
  {
    LOG_ERR("Settings init failed: %d", rc);
    return rc;
  }

  rc = settings_load();
  if (rc)
  {
    LOG_WRN("Settings load failed: %d (using defaults)", rc);
  }

  initialized = true;
  LOG_INF("User settings initialized: name='%s', power=%d, flight=%d",
          settings.device_name, settings.power_mode, settings.flight_mode);
  return 0;
}

const user_settings_t *user_settings_get(void) { return &settings; }

const char *user_settings_get_device_name(void) { return settings.device_name; }

int user_settings_set_device_name(const char *name)
{
  if (!name)
  {
    return -EINVAL;
  }

  strncpy(settings.device_name, name, USER_SETTINGS_NAME_MAX_LEN);
  settings.device_name[USER_SETTINGS_NAME_MAX_LEN] = '\0';

  int rc = settings_save_one(SETTINGS_KEY "/name", settings.device_name,
                             strlen(settings.device_name));
  if (rc)
  {
    LOG_ERR("Failed to save device name: %d", rc);
  }
  return rc;
}

uint8_t user_settings_get_power_mode(void) { return settings.power_mode; }

int user_settings_set_power_mode(uint8_t mode)
{
  settings.power_mode = mode;
  return settings_save_one(SETTINGS_KEY "/power", &settings.power_mode,
                           sizeof(settings.power_mode));
}

uint8_t user_settings_get_flight_mode(void) { return settings.flight_mode; }

int user_settings_set_flight_mode(uint8_t enabled)
{
  settings.flight_mode = enabled ? 1 : 0;
  return settings_save_one(SETTINGS_KEY "/flight", &settings.flight_mode,
                           sizeof(settings.flight_mode));
}

bool user_settings_is_dnd_active(void)
{
  if (!settings.dnd_enabled)
  {
    return false;
  }

  /* If time hasn't been set by app, we can't check DND window */
  if (!time_set)
  {
    return false;
  }

  uint8_t current_hour = user_settings_get_time_now_hour24();
  uint8_t current_minute = user_settings_get_time_now_minute();
  uint8_t start_hour = settings.dnd_start_hour;
  uint8_t start_minute = settings.dnd_start_minute;
  uint8_t end_hour = settings.dnd_end_hour;
  uint8_t end_minute = settings.dnd_end_minute;

  /* Convert to minutes since midnight for easier comparison */
  uint16_t current = current_hour * 60 + current_minute;
  uint16_t start = start_hour * 60 + start_minute;
  uint16_t end = end_hour * 60 + end_minute;

  if (start < end)
  {
    /* Simple case: e.g., 09:00 to 17:00 */
    return (current >= start && current < end);
  }
  else
  {
    /* Overnight case: e.g., 22:00 to 07:00 */
    return (current >= start || current < end);
  }
}

int user_settings_set_dnd(uint8_t enabled, uint8_t start_hour,
                          uint8_t start_minute, uint8_t end_hour,
                          uint8_t end_minute)
{
  settings.dnd_enabled = enabled;
  settings.dnd_start_hour = start_hour;
  settings.dnd_start_minute = start_minute;
  settings.dnd_end_hour = end_hour;
  settings.dnd_end_minute = end_minute;

  uint8_t dnd_data[5] = {enabled, start_hour, start_minute, end_hour,
                         end_minute};
  return settings_save_one(SETTINGS_KEY "/dnd", dnd_data, sizeof(dnd_data));
}

uint8_t user_settings_get_buzzer(void) { return settings.buzzer_enabled; }

int user_settings_set_buzzer(uint8_t enabled)
{
  settings.buzzer_enabled = enabled ? 1 : 0;
  LOG_INF("Buzzer set to %d (stub - no hardware)", settings.buzzer_enabled);
  return settings_save_one(SETTINGS_KEY "/buzzer", &settings.buzzer_enabled,
                           sizeof(settings.buzzer_enabled));
}

uint8_t user_settings_get_vibrator(void) { return settings.vibrator_enabled; }

int user_settings_set_vibrator(uint8_t enabled)
{
  settings.vibrator_enabled = enabled ? 1 : 0;
  LOG_INF("Vibrator set to %d (stub - no hardware)", settings.vibrator_enabled);
  return settings_save_one(SETTINGS_KEY "/vibrator", &settings.vibrator_enabled,
                           sizeof(settings.vibrator_enabled));
}

/* Time management functions */

void user_settings_set_timebase(uint32_t time)
{
  time_base = time;
  if (!time_set)
  {
    time_set = true;
    LOG_INF("Time set for the first time: %u", time);
  }
  else
  {
    LOG_INF("Time updated: %u", time);
  }
}

uint32_t user_settings_get_time_now(void) { return time_base; }

uint8_t user_settings_get_time_now_hour24(void)
{
  uint32_t current_seconds = time_base % 60;
  uint32_t remaining_minutes = (time_base - current_seconds) / 60;
  uint32_t current_minutes = remaining_minutes % 60;
  uint32_t remaining_hours = (remaining_minutes - current_minutes) / 60;
  uint32_t current_hours = remaining_hours % 24;

  return (uint8_t)current_hours;
}

uint8_t user_settings_get_time_now_minute(void)
{
  uint32_t current_seconds = time_base % 60;
  uint32_t remaining_minutes = (time_base - current_seconds) / 60;
  uint32_t current_minutes = remaining_minutes % 60;

  return (uint8_t)current_minutes;
}

bool user_settings_is_time_set(void) { return time_set; }

void user_settings_inc_second(void) { time_base++; }
