/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * User Settings Implementation - NVS-backed persistence
 */

#include <string.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <services/user_settings.h>

LOG_MODULE_REGISTER(user_settings, CONFIG_AIRSPOT_LOG_LEVEL);

/* Settings key prefix */
#define SETTINGS_KEY "airspot"

K_MUTEX_DEFINE(settings_mutex);

/* Native ASC is opt-in, including upgrades from pre-native settings.
 * A build overlay must never change an existing device calibration policy.
 * Magic-tagged native preferences are restored unchanged by decode_settings. */
#if defined(CONFIG_AIRSPOT_SENSOR_NATIVE_ONLY)
#define DEFAULT_NATIVE_ASC_MODE 2
#else
#define DEFAULT_NATIVE_ASC_MODE 0
#endif

#if defined(CONFIG_AIRSPOT_NATIVE_TEST_MODES)
#define DEFAULT_POWER_MODE 3
#define MAX_POWER_MODE 4
#else
#define DEFAULT_POWER_MODE 1
#define MAX_POWER_MODE 3
#endif
/* Default settings */
static user_settings_t settings = {
    .device_name = "AirSpot Slim",
    .power_mode = DEFAULT_POWER_MODE,
    .co2_correction = 0,
    .calib_target = 426,
    .auto_calibrate = 0, /* ASC off by default - manual recalibration is used */
    .asc_day_count = 0,
    .co2_yellow_start = 800,
    .co2_red_start = 1000,
    .native_asc_mode = DEFAULT_NATIVE_ASC_MODE,
    .paired = 0};
static const user_settings_t defaults = {
    .device_name = "AirSpot Slim",
    .power_mode = DEFAULT_POWER_MODE,
    .co2_correction = 0,
    .calib_target = 426,
    .auto_calibrate = 0, /* ASC off by default - manual recalibration is used */
    .asc_day_count = 0,
    .co2_yellow_start = 800,
    .co2_red_start = 1000,
    .native_asc_mode = DEFAULT_NATIVE_ASC_MODE,
    .paired = 0};

static bool initialized = false;

/* Time management - seconds since 2000-01-01 00:00:00 */
static volatile uint32_t time_base = 0;
static bool time_set = false;

/* Versioned all-settings transaction. Legacy keys are read only for migration.
 * A trial rollback to pre-journal firmware sees its last legacy settings. */
#define SNAPSHOT_SIZE 49
static bool snapshot_loaded;
static bool persistence_blocked;
static int snapshot_error, legacy_error;
static user_settings_t loaded_snapshot;
static void encode_settings(const user_settings_t *value, uint8_t *out)
{
    memset(out, 0, SNAPSHOT_SIZE);
    out[0] = 1;
    size_t i = 4;
    memcpy(out + i, value->device_name, 21); i += 21;
    out[i++] = value->power_mode;
    /* Keep schema-1 offsets for upgrades/rollback. Removed Screen-only fields
     * are reserved zero bytes, not Slim settings. */
    /* Three formerly reserved bytes; magic distinguishes old Screen fields. */
    out[i] = value->native_asc_mode;
    out[i + 1] = 0x53;
    out[i + 2] = 0xA1;
    i += 8;
    sys_put_le16((uint16_t)value->co2_correction, out + i); i += 2;
    sys_put_le16((uint16_t)value->calib_target, out + i); i += 2;
    out[i++] = value->auto_calibrate;
    out[i++] = value->asc_day_count;
    sys_put_le16((uint16_t)value->co2_yellow_start, out + i); i += 2;
    sys_put_le16((uint16_t)value->co2_red_start, out + i); i += 2;
    out[i++] = value->paired;
    sys_put_le32(crc32_ieee(out, SNAPSHOT_SIZE - 4), out + i);
}
static int decode_settings(const uint8_t *in, user_settings_t *value)
{
    if (in[0] != 1 || in[1] || in[2] || in[3] ||
        sys_get_le32(in + SNAPSHOT_SIZE - 4) != crc32_ieee(in, SNAPSHOT_SIZE - 4)) return -EINVAL;
    size_t i = 4;
    memset(value, 0, sizeof(*value));
    memcpy(value->device_name, in + i, 21); i += 21;
    value->power_mode = in[i++];
    value->native_asc_mode = (in[i + 1] == 0x53 && in[i + 2] == 0xA1)
        ? in[i] : DEFAULT_NATIVE_ASC_MODE;
    i += 8; /* Ignore old flight/DND/buzzer/vibrator values. */
    value->co2_correction = (int16_t)sys_get_le16(in + i); i += 2;
    value->calib_target = (uint16_t)sys_get_le16(in + i); i += 2;
    value->auto_calibrate = in[i++];
    value->asc_day_count = in[i++];
    value->co2_yellow_start = (uint16_t)sys_get_le16(in + i); i += 2;
    value->co2_red_start = (uint16_t)sys_get_le16(in + i); i += 2;
    value->paired = in[i++];
    if (!memchr(value->device_name, 0, 21) || value->power_mode > MAX_POWER_MODE ||
        value->native_asc_mode > 2 || value->auto_calibrate > 1 || value->paired > 1 ||
        value->co2_yellow_start < 400 || value->co2_red_start > 5000 ||
        value->co2_yellow_start >= value->co2_red_start) return -EINVAL;
    return 0;
}
/* Caller holds settings_mutex; publish only after atomic NVS acceptance. */
static int save_snapshot_impl(const user_settings_t *candidate, bool recovery)
{
    if (!boot_is_img_confirmed()) return -EBUSY;
    if (!recovery && (persistence_blocked || snapshot_error ||
                      (!snapshot_loaded && legacy_error))) return -EIO;
    uint8_t record[SNAPSHOT_SIZE];
    encode_settings(candidate, record);
    int rc = settings_save_one(SETTINGS_KEY "/snapshot", record, sizeof(record));
    if (!rc) settings = *candidate;
    return rc;
}
static int save_snapshot(const user_settings_t *candidate)
{
    return save_snapshot_impl(candidate, false);
}
static int snapshot_commit(void)
{
    if (snapshot_error) return snapshot_error;
    if (snapshot_loaded) { settings = loaded_snapshot; return 0; }
    return legacy_error;
}

/* Reject truncated records before touching live settings. */
static int load_value(const char *name, size_t len, settings_read_cb read_cb,
                        void *cb_arg)
{
  if (strcmp(name, "snapshot") == 0) {
    uint8_t record[SNAPSHOT_SIZE];
    if (len != sizeof(record)) return -EINVAL;
    int rc = read_cb(cb_arg, record, sizeof(record));
    if (rc != sizeof(record)) return rc < 0 ? rc : -EIO;
    rc = decode_settings(record, &loaded_snapshot);
    if (!rc) snapshot_loaded = true;
    return rc;
  }
  user_settings_t candidate = settings;
  void *destination = NULL;
  size_t expected = 0;

#define FIELD(key, member) \
  if (strcmp(name, key) == 0) { destination = &candidate.member; expected = sizeof(candidate.member); }
  FIELD("power", power_mode)
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
  } else {
    return -ENOENT;
  }
#undef FIELD
  if (len != expected) { return -EINVAL; }
  int rc = read_cb(cb_arg, destination, expected);
  if (rc < 0) { return rc; }
  if ((size_t)rc != expected) { return -EIO; }
  if (candidate.power_mode > MAX_POWER_MODE || candidate.auto_calibrate > 1 ||
      candidate.paired > 1) { return -EINVAL; }
  settings = candidate;
  return 0;
}

/* Zephyr intentionally ignores per-key load errors; preserve them for commit. */
static int settings_set(const char *name, size_t len, settings_read_cb read_cb, void *arg)
{
    int rc = load_value(name, len, read_cb, arg);
    if (strcmp(name, "snapshot") == 0) snapshot_error = rc;
    else if (rc && rc != -ENOENT) legacy_error = rc;
    return rc;
}

/* Publish a scalar setting only after NVS accepts it. */
static int save_value(const char *key, void *destination, const void *value, size_t size)
{
  ARG_UNUSED(key);
  k_mutex_lock(&settings_mutex, K_FOREVER);
  user_settings_t candidate = settings;
  size_t offset = (uint8_t *)destination - (uint8_t *)&settings;
  memcpy((uint8_t *)&candidate + offset, value, size);
  int rc = save_snapshot(&candidate);
  k_mutex_unlock(&settings_mutex);
  return rc;
}

SETTINGS_STATIC_HANDLER_DEFINE(airspot, SETTINGS_KEY, NULL, settings_set, snapshot_commit,
                               NULL);

int user_settings_init(void)
{
  if (initialized)
  {
    return 0;
  }

  persistence_blocked = true;
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

#if defined(CONFIG_AIRSPOT_SENSOR_NATIVE_ONLY)
  /* Never revive host learning or a saved offset. No sensor history is erased. */
  if (!settings.native_asc_mode) settings.native_asc_mode = 2;
#if defined(CONFIG_AIRSPOT_NATIVE_TEST_MODES)
  /* Product policy: re-enable learning even for an older saved OFF preference. */
  settings.native_asc_mode = 2;
  if (settings.power_mode != 1 && settings.power_mode != 3 && settings.power_mode != 4) settings.power_mode = 3;
#endif
  settings.auto_calibrate = 0;
  settings.co2_correction = 0;
#endif

  /* Migrate only after every legacy key has been loaded, independent of order. */
  if (!snapshot_loaded) {
    uint8_t record[SNAPSHOT_SIZE];
    user_settings_t checked;
    encode_settings(&settings, record);
    if (decode_settings(record, &checked)) return -EINVAL;
    if (boot_is_img_confirmed()) {
      rc = save_snapshot_impl(&checked, true);
      if (rc) return rc;
    }
  }
  persistence_blocked = false;
  initialized = true;
  LOG_INF("User settings initialized: name='%s', power=%d",
          settings.device_name, settings.power_mode);
  return 0;
}

void user_settings_get_snapshot(user_settings_t *out) {
  k_mutex_lock(&settings_mutex, K_FOREVER);
  *out = settings;
  if (settings.native_asc_mode) {
    out->co2_correction = 0;
    out->auto_calibrate = settings.native_asc_mode == 2;
#if !defined(CONFIG_AIRSPOT_NATIVE_TEST_MODES)
    out->calib_target = settings.native_asc_mode == 2 ? 400 : settings.calib_target;
#endif
  }
  k_mutex_unlock(&settings_mutex);
}

const char *user_settings_get_device_name(void) { return settings.device_name; }

int user_settings_set_device_name(const char *name)
{
  if (!name)
  {
    return -EINVAL;
  }

  char value[USER_SETTINGS_NAME_MAX_LEN + 1] = {0};
  strncpy(value, name, USER_SETTINGS_NAME_MAX_LEN);
  return save_value(SETTINGS_KEY "/name", settings.device_name, value, sizeof(value));
}

uint8_t user_settings_get_native_asc_mode(void) { return settings.native_asc_mode; }

int user_settings_set_native_asc_mode(uint8_t mode)
{
#if defined(CONFIG_AIRSPOT_NATIVE_TEST_MODES)
  if (mode != 2) return -ENOTSUP;
#endif
  if (mode > 2) return -EINVAL;
  k_mutex_lock(&settings_mutex, K_FOREVER);
  user_settings_t candidate = settings;
  candidate.native_asc_mode = mode;
  candidate.auto_calibrate = 0; /* Never run two learning algorithms. */
  int rc = save_snapshot(&candidate);
  k_mutex_unlock(&settings_mutex);
  return rc;
}

int user_settings_commit_manual_correction(int16_t correction)
{
#if defined(CONFIG_AIRSPOT_NATIVE_TEST_MODES)
  return -ENOTSUP;
#endif
  k_mutex_lock(&settings_mutex, K_FOREVER);
  int rc = -EBUSY;
  if (settings.native_asc_mode != 2) {
    user_settings_t candidate = settings;
    candidate.co2_correction = correction;
    candidate.native_asc_mode = 0;
    rc = save_snapshot(&candidate);
  }
  k_mutex_unlock(&settings_mutex);
  return rc;
}

uint8_t user_settings_get_power_mode(void) { return settings.power_mode; }

int user_settings_set_power_mode(uint8_t mode)
{
#if defined(CONFIG_AIRSPOT_NATIVE_TEST_MODES)
  if (mode != 1 && mode != 3 && mode != 4) return -EINVAL;
#endif
  if (mode > MAX_POWER_MODE) { return -EINVAL; }
  uint8_t value = mode;
  return save_value(SETTINGS_KEY "/power", &settings.power_mode, &value, sizeof(value));
}

int16_t user_settings_get_co2_correction(void)
{
  return settings.native_asc_mode ? 0 : settings.co2_correction;
}

int user_settings_set_co2_correction(int16_t correction)
{
#if defined(CONFIG_AIRSPOT_NATIVE_TEST_MODES)
  return -ENOTSUP;
#endif
  int16_t value = correction;
  return save_value(SETTINGS_KEY "/co2_cc", &settings.co2_correction, &value, sizeof(value));
}

uint16_t user_settings_get_calib_target(void) {
#if defined(CONFIG_AIRSPOT_NATIVE_TEST_MODES)
  return settings.calib_target;
#else
  return settings.native_asc_mode == 2 ? 400 : settings.calib_target;
#endif
}

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

  k_mutex_lock(&settings_mutex, K_FOREVER);
  user_settings_t candidate = settings;
  candidate.co2_yellow_start = yellow_start;
  candidate.co2_red_start = red_start;
  int rc = save_snapshot(&candidate);
  k_mutex_unlock(&settings_mutex);
  return rc;
}

uint8_t user_settings_get_auto_calibrate(void)
{
  return settings.native_asc_mode ? settings.native_asc_mode == 2 : settings.auto_calibrate;
}

int user_settings_set_auto_calibrate(uint8_t enabled)
{
#if defined(CONFIG_AIRSPOT_NATIVE_TEST_MODES)
  return -ENOTSUP;
#endif
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
#if defined(CONFIG_AIRSPOT_NATIVE_TEST_MODES)
  return raw_ppm;
#endif
  if (settings.native_asc_mode) return raw_ppm; /* Retain baseline when learning is paused. */
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

bool user_settings_is_time_set(void) { return time_set; }

void user_settings_inc_second(void) { time_base++; }

int user_settings_factory_reset(void)
{
  k_mutex_lock(&settings_mutex, K_FOREVER);
  int rc = save_snapshot_impl(&defaults, true);
  if (!rc) {
    time_base = 0; time_set = false;
    snapshot_error = legacy_error = 0;
    snapshot_loaded = true;
    loaded_snapshot = defaults;
    persistence_blocked = false;
    initialized = true;
  }
  k_mutex_unlock(&settings_mutex);
  return rc;
}

/* Reset preferences without erasing calibration, ASC policy or sampling cadence. */
int user_settings_reset_preferences(void)
{
  k_mutex_lock(&settings_mutex, K_FOREVER);
  user_settings_t candidate = defaults;
  candidate.co2_correction = settings.co2_correction;
  candidate.calib_target = settings.calib_target;
  candidate.auto_calibrate = settings.auto_calibrate;
  candidate.native_asc_mode = settings.native_asc_mode;
  candidate.power_mode = settings.power_mode;
  int rc = save_snapshot(&candidate);
  k_mutex_unlock(&settings_mutex);
  return rc;
}
int user_settings_start_settling(void)
{
  k_mutex_lock(&settings_mutex, K_FOREVER);
  user_settings_t candidate = settings;
  candidate.native_asc_mode = 2;
  candidate.auto_calibrate = 0;
  candidate.co2_correction = 0;
  candidate.power_mode = 3;
  int rc = save_snapshot(&candidate);
  k_mutex_unlock(&settings_mutex);
  return rc;
}
