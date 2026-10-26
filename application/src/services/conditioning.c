/* STCC4 idle policy. No clock is assumed to survive System OFF. */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <services/conditioning.h>

#define IDLE_MS (3LL * 60 * 60 * 1000)
#define MIN_EPOCH 1577836800U /* 2020-01-01: reject unset/uptime clocks. */
K_MUTEX_DEFINE(conditioning_mutex);
static uint32_t saved_time, anchor_time;
static int64_t anchor_ms, resume_ms, last_sample_ms = -1, conditioned_ms = -1;
static bool begun, waiting, needed;

static int load(const char *name, size_t len, settings_read_cb read, void *arg)
{
  if (strcmp(name, "last")) return -ENOENT;
  uint8_t b[12];
  int rc = len == sizeof(b) ? read(arg, b, sizeof(b)) : -EINVAL;
  saved_time = 0;
  if (rc != sizeof(b) || b[0] != 1 || b[1] || b[2] || b[3] ||
      sys_get_be32(b + 8) != crc32_ieee(b, 8)) return 0; /* Missing/corrupt: condition. */
  uint32_t value = sys_get_be32(b + 4);
  if (value && value < MIN_EPOCH) return 0;
  saved_time = value;
  return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(conditioning, "conditioning", NULL, load, NULL, NULL);
static int save(uint32_t epoch)
{
  uint8_t b[12] = {1};
  sys_put_be32(epoch, b + 4);
  sys_put_be32(crc32_ieee(b, 8), b + 8);
  return settings_save_one("conditioning/last", b, sizeof(b));
}
int conditioning_begin(void)
{
  k_mutex_lock(&conditioning_mutex, K_FOREVER);
  int rc = 0;
  if (!begun) {
    /* Invalidate BEFORE measuring: an abrupt loss must never reuse stale time.
     * This independent key is safe even in an unconfirmed OTA image. */
    rc = save(0);
    if (!rc) {
      begun = true;
      resume_ms = k_uptime_get();
      waiting = saved_time != 0;
      needed = !waiting;
      if (waiting && anchor_time) {
        int64_t current = (int64_t)anchor_time + (resume_ms - anchor_ms) / 1000;
        if (current >= saved_time) {
          needed = current - saved_time > IDLE_MS / 1000;
          waiting = false;
        }
      }
    }
  }
  k_mutex_unlock(&conditioning_mutex);
  return rc;
}
void conditioning_time_sync(uint32_t epoch)
{
  k_mutex_lock(&conditioning_mutex, K_FOREVER);
  int64_t now = k_uptime_get();
  int64_t resumed = (int64_t)epoch - (now - resume_ms) / 1000;
  /* A backward/invalid clock cannot prove the old idle gap. Keep waiting for
   * a valid sync; do not overwrite the old timestamp or create a new anchor. */
  if (epoch >= MIN_EPOCH && (!waiting || resumed >= saved_time) &&
      (!anchor_time || (int64_t)epoch >= (int64_t)anchor_time + (now - anchor_ms) / 1000 - 2)) {
    anchor_time = epoch;
    anchor_ms = now;
    if (begun && waiting) {
      needed = resumed - saved_time > IDLE_MS / 1000;
      waiting = false;
    }
  }
  k_mutex_unlock(&conditioning_mutex);
}
bool conditioning_due(bool single_shot)
{
  k_mutex_lock(&conditioning_mutex, K_FOREVER);
  int64_t last = last_sample_ms > conditioned_ms ? last_sample_ms : conditioned_ms;
  bool due = begun && (needed || (!waiting && single_shot && last >= 0 && k_uptime_get() - last > IDLE_MS));
  k_mutex_unlock(&conditioning_mutex);
  return due;
}
void conditioning_completed(void)
{
  k_mutex_lock(&conditioning_mutex, K_FOREVER);
  needed = false;
  conditioned_ms = k_uptime_get();
  k_mutex_unlock(&conditioning_mutex);
}
void conditioning_observe(void)
{
  k_mutex_lock(&conditioning_mutex, K_FOREVER);
  last_sample_ms = k_uptime_get();
  k_mutex_unlock(&conditioning_mutex);
}
int conditioning_shutdown(void)
{
  k_mutex_lock(&conditioning_mutex, K_FOREVER);
  uint32_t timestamp = 0;
  if (begun && !waiting && !needed && anchor_time && last_sample_ms >= 0) {
    int64_t value = (int64_t)anchor_time + (last_sample_ms - anchor_ms) / 1000;
    if (value >= MIN_EPOCH && value <= UINT32_MAX) timestamp = value;
  }
  int rc = begun ? save(timestamp) : 0;
  k_mutex_unlock(&conditioning_mutex);
  return rc;
}
