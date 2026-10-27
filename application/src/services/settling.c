/* Customer guidance, not a measurement of the STCC4's internal ASC state. */
#include <string.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <services/settling.h>
#if defined(CONFIG_AIRSPOT_NATIVE_TEST_MODES)
#include <services/initial_settling.h>
#endif

K_MUTEX_DEFINE(settling_mutex);
static settling_status_t state;
static int64_t last_sample = -1;
static uint32_t checkpoint;
static int64_t last_retry;
/* Separate key survives ordinary settings/history resets. CRC and fixed schema. */
static int persist(void)
{
  if (!boot_is_img_confirmed()) return -EBUSY;
  uint8_t record[24] = {1, state.stage, 0, 0};
  sys_put_be32(state.generation, record + 4);
  sys_put_be32(state.reset_time, record + 8);
  sys_put_be32(state.seconds, record + 12);
  sys_put_be32(state.fresh_seconds, record + 16);
  sys_put_be32(crc32_ieee(record, 20), record + 20);
  int rc = settings_save_one("settling/state", record, sizeof(record));
  state.error = rc;
  if (!rc) checkpoint = state.seconds;
  return rc;
}
static int load(const char *name, size_t len, settings_read_cb read, void *arg)
{
  if (strcmp(name, "state")) return -ENOENT;
  uint8_t record[24];
  int rc = len == sizeof(record) ? read(arg, record, sizeof(record)) : -EINVAL;
  if (rc != sizeof(record) || record[0] != 1 || record[1] > SETTLING_ERASE_PENDING ||
      record[2] || record[3] || sys_get_be32(record + 20) != crc32_ieee(record, 20) ||
      sys_get_be32(record + 12) > SETTLING_SECONDS ||
      sys_get_be32(record + 16) > FRESH_AIR_SECONDS) {
    state.error = -EIO;
    return -EIO;
  }
  state.stage = record[1];
  state.generation = sys_get_be32(record + 4);
  state.reset_time = sys_get_be32(record + 8);
  /* A reboot breaks continuous operation. Never count powered-off time. */
  state.seconds = state.stage == SETTLING_RUN ? 0 : sys_get_be32(record + 12);
  state.fresh_seconds = 0;
  if (state.stage == SETTLING_EXPOSING) state.stage = SETTLING_FRESH;
  last_sample = -1;
  state.running = false;
  state.error = 0;
  return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(settling, "settling", NULL, load, NULL, NULL);

int settling_prepare_erase(uint32_t reset_time)
{
  k_mutex_lock(&settling_mutex, K_FOREVER);
  settling_status_t before = state;
  state = (settling_status_t){.stage = SETTLING_ERASE_PENDING,
    .generation = before.generation + 1, .reset_time = reset_time};
  int rc = persist();
  if (rc) { state = before; state.error = rc; }
  last_sample = -1;
  k_mutex_unlock(&settling_mutex);
  return rc;
}
int settling_erased(void)
{
#if defined(CONFIG_AIRSPOT_NATIVE_TEST_MODES)
  int reset_rc = initial_settling_reset();
  if (reset_rc) return reset_rc;
#endif
  k_mutex_lock(&settling_mutex, K_FOREVER);
  state.stage = SETTLING_RUN;
  state.seconds = state.fresh_seconds = 0;
  last_sample = -1;
  int rc = persist();
  k_mutex_unlock(&settling_mutex);
  return rc;
}
void settling_observe(bool eligible)
{
  k_mutex_lock(&settling_mutex, K_FOREVER);
  int64_t now = k_uptime_get();
  if (state.error && state.stage && now - last_retry >= 30000) {
    last_retry = now; (void)persist();
  }
  bool active = state.stage == SETTLING_RUN || state.stage == SETTLING_EXPOSING;
  bool gap = last_sample >= 0 && now - last_sample > 10000;
  if (active && (!eligible || gap)) {
    bool changed = state.seconds != 0 || state.stage == SETTLING_EXPOSING;
    if (state.stage == SETTLING_RUN) state.seconds = 0;
    else { state.stage = SETTLING_FRESH; state.fresh_seconds = 0; }
    last_sample = -1;
    if (changed) (void)persist();
  }
  state.running = eligible && (state.stage == SETTLING_RUN || state.stage == SETTLING_EXPOSING);
  if (state.running) {
    if (last_sample >= 0) {
      uint32_t delta = (now - last_sample) / 1000;
      if (state.stage == SETTLING_RUN) {
        state.seconds += delta;
        if (state.seconds >= SETTLING_SECONDS) {
          state.seconds = SETTLING_SECONDS;
          state.stage = SETTLING_FRESH;
          state.running = false;
          (void)persist();
        } else if (state.seconds >= checkpoint + 300 || state.error) {
          (void)persist();
        }
      } else if (state.stage == SETTLING_EXPOSING) {
        state.fresh_seconds += delta;
        if (state.fresh_seconds > FRESH_AIR_SECONDS) state.fresh_seconds = FRESH_AIR_SECONDS;
      }
      last_sample += (int64_t)delta * 1000;
    } else last_sample = now;
  } else last_sample = -1;
  k_mutex_unlock(&settling_mutex);
}
void settling_get(settling_status_t *out)
{
  k_mutex_lock(&settling_mutex, K_FOREVER);
  *out = state;
  out->running = state.running && last_sample >= 0 && k_uptime_get() - last_sample <= 10000;
  k_mutex_unlock(&settling_mutex);
}
int settling_action(uint8_t action, uint32_t generation)
{
  k_mutex_lock(&settling_mutex, K_FOREVER);
  int rc = 0;
  settling_status_t before = state;
  if (generation != state.generation) rc = -ESTALE;
  else if (state.error) rc = state.error;
  else if (action == 3 && (state.stage == SETTLING_NONE || state.stage == SETTLING_DONE)) {
    /* Explicit guided recovery; preserve all sensor calibration history. */
    state = (settling_status_t){.stage = SETTLING_RUN,
      .generation = before.generation + 1};
    rc = persist();
    if (!rc) last_sample = -1;
  } else if (action == 1 && state.stage == SETTLING_FRESH) {
    state.stage = SETTLING_EXPOSING; state.fresh_seconds = 0; last_sample = -1;
    rc = persist();
  } else if (action == 2 && state.stage == SETTLING_EXPOSING &&
             state.fresh_seconds >= FRESH_AIR_SECONDS && state.running &&
             last_sample >= 0 && k_uptime_get() - last_sample <= 10000) {
    state.stage = SETTLING_DONE; state.running = false; rc = persist();
  } else rc = -EAGAIN;
  if (rc) state = before;
  k_mutex_unlock(&settling_mutex);
  return rc;
}
