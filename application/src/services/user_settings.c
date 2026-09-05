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
    .power_mode = 1,  /* Low power (3 min interval) */
    .flight_mode = 0, /* Off */
    .dnd_enabled = 0, /* Off */
    .dnd_start_hour = 22,
    .dnd_start_minute = 0,
    .dnd_end_hour = 7,
    .dnd_end_minute = 0,
    .buzzer_enabled = 0,   /* Stub */
    .vibrator_enabled = 0, /* Stub */
    .co2_correction = 0,
    .calib_target = 426,
    .auto_calibrate = 0, /* ASC off by default - manual recalibration is used */
    .asc_day_count = 0,
    .co2_yellow_start = 800,
    .co2_red_start = 1000,
    .paired = 0};

static bool initialized = false;

/* Time management - seconds since 2000-01-01 00:00:00 */
static volatile uint32_t time_base = 0;
static bool time_set = false;

/* Reject truncated records before touching live settings. */
static int settings_set(const char *name, size_t len, settings_read_cb read_cb,
                        void *cb_arg)
{
  user_settings_t candidate = settings;
  void *destination = NULL;
  size_t expected = 0;
  uint8_t dnd[5];

#define FIELD(key, member) \
  if (strcmp(name, key) == 0) { destination = &candidate.member; expected = sizeof(candidate.member); }
  FIELD("power", power_mode)
  else FIELD("flight", flight_mode)
  else FIELD("buzzer", buzzer_enabled)
  else FIELD("vibrator", vibrator_enabled)
  else FIELD("co2_cc", co2_correction)
  else FIELD("calib_target", calib_target)
  else FIELD("auto_calib", auto_calibrate)
  else FIELD("asc_day", asc_day_count)
  else FIELD("co2_yellow", co2_yellow_start)
  else FIELD("co2_red", co2_red_start)
  else FIELD("paired", paired)
  else if (strcmp(name, "name") == 0) {
    if (len > USER_SETTINGS_NAME_MAX_LEN) { return -EINVAL; }
    destination = candidate.device_name;
    expected = len;
    candidate.device_name[len] = '\0';
  } else if (strcmp(name, "dnd") == 0) {
    destination = dnd;
    expected = sizeof(dnd);
  } else {
    return -ENOENT;
  }
#undef FIELD
  if (len != expected) { return -EINVAL; }
  int rc = read_cb(cb_arg, destination, expected);
  if (rc < 0) { return rc; }
  if ((size_t)rc != expected) { return -EIO; }
  if (destination == dnd) {
    candidate.dnd_enabled = dnd[0];
    candidate.dnd_start_hour = dnd[1];
    candidate.dnd_start_minute = dnd[2];
    candidate.dnd_end_hour = dnd[3];
    candidate.dnd_end_minute = dnd[4];
  }
  if (candidate.power_mode > 3 || candidate.flight_mode > 1 ||
      candidate.buzzer_enabled > 1 || candidate.vibrator_enabled > 1 ||
      candidate.auto_calibrate > 1 || candidate.paired > 1 ||
      candidate.dnd_enabled > 1 || candidate.dnd_start_hour > 23 ||
      candidate.dnd_end_hour > 23 || candidate.dnd_start_minute > 59 ||
      candidate.dnd_end_minute > 59) { return -EINVAL; }
  settings = candidate;
  return 0;
}

/* Publish a scalar setting only after NVS accepts it. */
static int save_value(const char *key, void *destination, const void *value, size_t size)
{
  int rc = settings_save_one(key, value, size);
  if (rc == 0) { memcpy(destination, value, size); }
  return rc;
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
    LOG_ERR("Settings load failed: %d", rc);
    return rc;
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

  char value[USER_SETTINGS_NAME_MAX_LEN + 1] = {0};
  strncpy(value, name, USER_SETTINGS_NAME_MAX_LEN);
  int rc = settings_save_one(SETTINGS_KEY "/name", value, strlen(value));
  if (rc == 0) { memcpy(settings.device_name, value, sizeof(value)); }
  return rc;
}

uint8_t user_settings_get_power_mode(void) { return settings.power_mode; }

int user_settings_set_power_mode(uint8_t mode)
{
  if (mode > 3) { return -EINVAL; }
  uint8_t value = mode;
  return save_value(SETTINGS_KEY "/power", &settings.power_mode, &value, sizeof(value));
}

uint8_t user_settings_get_flight_mode(void) { return settings.flight_mode; }

int user_settings_set_flight_mode(uint8_t enabled)
{
  uint8_t value = enabled ? 1 : 0;
  return save_value(SETTINGS_KEY "/flight", &settings.flight_mode, &value, sizeof(value));
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
  if (enabled > 1 || start_hour > 23 || end_hour > 23 ||
      start_minute > 59 || end_minute > 59) { return -EINVAL; }
  uint8_t dnd_data[5] = {enabled, start_hour, start_minute, end_hour, end_minute};
  int rc = settings_save_one(SETTINGS_KEY "/dnd", dnd_data, sizeof(dnd_data));
  if (rc) { return rc; }
  settings.dnd_enabled = enabled;
  settings.dnd_start_hour = start_hour;
  settings.dnd_start_minute = start_minute;
  settings.dnd_end_hour = end_hour;
  settings.dnd_end_minute = end_minute;
  return 0;
}

uint8_t user_settings_get_buzzer(void) { return settings.buzzer_enabled; }

int user_settings_set_buzzer(uint8_t enabled)
{
  uint8_t value = enabled ? 1 : 0;
  return save_value(SETTINGS_KEY "/buzzer", &settings.buzzer_enabled, &value, sizeof(value));
}

uint8_t user_settings_get_vibrator(void) { return settings.vibrator_enabled; }

int user_settings_set_vibrator(uint8_t enabled)
{
  uint8_t value = enabled ? 1 : 0;
  return save_value(SETTINGS_KEY "/vibrator", &settings.vibrator_enabled, &value, sizeof(value));
}

int16_t user_settings_get_co2_correction(void)
{
  return settings.co2_correction;
}

int user_settings_set_co2_correction(int16_t correction)
{
  int16_t value = correction;
  return save_value(SETTINGS_KEY "/co2_cc", &settings.co2_correction, &value, sizeof(value));
}

uint16_t user_settings_get_calib_target(void) { return settings.calib_target; }

int user_settings_set_calib_target(uint16_t target)
{
  uint16_t value = target;
  return save_value(SETTINGS_KEY "/calib_target", &settings.calib_target, &value, sizeof(value));
}

uint16_t user_settings_get_co2_yellow_start(void)
{
  return settings.co2_yellow_start;
}

uint16_t user_settings_get_co2_red_start(void) { return settings.co2_red_start; }

int user_settings_set_co2_thresholds(uint16_t yellow_start, uint16_t red_start)
{
  if (yellow_start >= red_start || yellow_start < 400 || red_start > 5000)
  {
    return -EINVAL;
  }

  settings.co2_yellow_start = yellow_start;
  settings.co2_red_start = red_start;

  int rc = settings_save_one(SETTINGS_KEY "/co2_yellow",
                             &settings.co2_yellow_start,
                             sizeof(settings.co2_yellow_start));
  if (rc)
  {
    return rc;
  }

  return settings_save_one(SETTINGS_KEY "/co2_red", &settings.co2_red_start,
                           sizeof(settings.co2_red_start));
}

uint8_t user_settings_get_auto_calibrate(void)
{
  return settings.auto_calibrate;
}

int user_settings_set_auto_calibrate(uint8_t enabled)
{
  uint8_t value = enabled ? 1 : 0;
  return save_value(SETTINGS_KEY "/auto_calib", &settings.auto_calibrate, &value, sizeof(value));
}

uint8_t user_settings_get_asc_day_count(void)
{
  return settings.asc_day_count;
}

int user_settings_set_asc_day_count(uint8_t day_count)
{
  uint8_t value = day_count;
  return save_value(SETTINGS_KEY "/asc_day", &settings.asc_day_count, &value, sizeof(value));
}

bool user_settings_is_paired(void) { return settings.paired != 0; }

int user_settings_set_paired(bool paired)
{
  uint8_t value = paired ? 1 : 0;
  if (settings.paired == value)
  {
    return 0; /* avoid an NVS write on every connection */
  }

  return save_value(SETTINGS_KEY "/paired", &settings.paired, &value, sizeof(value));
}

uint16_t user_settings_apply_co2_correction(uint16_t raw_ppm)
{
  int32_t corrected = (int32_t)raw_ppm + settings.co2_correction;
  if (corrected < 0)
  {
    corrected = 0;
  }
  else if (corrected > 65535)
  {
    corrected = 65535;
  }
  return (uint16_t)corrected;
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

int user_settings_factory_reset(void)
{
  LOG_WRN("Factory reset - clearing all user settings");

  /* Reset to defaults */
  memset(&settings, 0, sizeof(settings));
  settings.power_mode = 1;       /* Low power (3 min interval) */
  settings.buzzer_enabled = 0;   /* Stub - keep off */
  settings.vibrator_enabled = 0; /* Stub - keep off */
  settings.calib_target = 426;
  settings.co2_correction = 0;
  settings.auto_calibrate = 0; /* ASC off by default - manual recalibration is used */
  settings.asc_day_count = 0;
  settings.co2_yellow_start = 800;
  settings.co2_red_start = 1000;

  /* Clear time */
  time_base = 0;
  time_set = false;

  /* Delete all settings from NVS */
  int rc = settings_delete(SETTINGS_KEY "/name");
  if (rc && rc != -ENOENT)
  {
    LOG_WRN("Failed to delete name: %d", rc);
  }

  rc = settings_delete(SETTINGS_KEY "/power");
  if (rc && rc != -ENOENT)
  {
    LOG_WRN("Failed to delete power: %d", rc);
  }

  rc = settings_delete(SETTINGS_KEY "/flight");
  if (rc && rc != -ENOENT)
  {
    LOG_WRN("Failed to delete flight: %d", rc);
  }

  rc = settings_delete(SETTINGS_KEY "/dnd");
  if (rc && rc != -ENOENT)
  {
    LOG_WRN("Failed to delete dnd: %d", rc);
  }

  rc = settings_delete(SETTINGS_KEY "/buzzer");
  if (rc && rc != -ENOENT)
  {
    LOG_WRN("Failed to delete buzzer: %d", rc);
  }

  rc = settings_delete(SETTINGS_KEY "/vibrator");
  if (rc && rc != -ENOENT)
  {
    LOG_WRN("Failed to delete vibrator: %d", rc);
  }

  rc = settings_delete(SETTINGS_KEY "/co2_cc");
  if (rc && rc != -ENOENT)
  {
    LOG_WRN("Failed to delete co2_cc: %d", rc);
  }

  rc = settings_delete(SETTINGS_KEY "/calib_target");
  if (rc && rc != -ENOENT)
  {
    LOG_WRN("Failed to delete calib_target: %d", rc);
  }

  rc = settings_delete(SETTINGS_KEY "/auto_calib");
  if (rc && rc != -ENOENT)
  {
    LOG_WRN("Failed to delete auto_calib: %d", rc);
  }

  rc = settings_delete(SETTINGS_KEY "/asc_day");
  if (rc && rc != -ENOENT)
  {
    LOG_WRN("Failed to delete asc_day: %d", rc);
  }

  rc = settings_delete(SETTINGS_KEY "/co2_yellow");
  if (rc && rc != -ENOENT)
  {
    LOG_WRN("Failed to delete co2_yellow: %d", rc);
  }

  rc = settings_delete(SETTINGS_KEY "/co2_red");
  if (rc && rc != -ENOENT)
  {
    LOG_WRN("Failed to delete co2_red: %d", rc);
  }

  rc = settings_delete(SETTINGS_KEY "/paired");
  if (rc && rc != -ENOENT)
  {
    LOG_WRN("Failed to delete paired: %d", rc);
  }

  LOG_INF("Factory reset complete - settings cleared");
  return 0;
}
