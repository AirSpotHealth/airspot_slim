/* First-hour indication only: this does not certify accuracy or ASC convergence.
 * Count successful sensing, never time spent powered off. Persist completion
 * and checkpoint progress each minute; powered-off time is never counted. */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <services/initial_settling.h>
#include <services/led_indicator.h>

static bool complete;
static uint32_t measured_ms;
static int64_t last_sample = -1, next_blink, next_save;
static uint32_t persisted_ms;
static int load(const char *name, size_t len, settings_read_cb read, void *arg)
{
  if (strcmp(name, "complete")) return -ENOENT;
  /* Read the original completion-only format for existing devices. */
  if (len == 1) {
    uint8_t value;
    if (read(arg, &value, 1) != 1 || value > 1) return -EINVAL;
    complete = value != 0;
    measured_ms = persisted_ms = complete ? 3600000 : 0;
    return 0;
  }
  uint8_t b[12];
  if (len != sizeof(b) || read(arg, b, sizeof(b)) != sizeof(b) ||
      b[0] != 1 || b[1] || b[2] || b[3] ||
      sys_get_be32(b + 8) != crc32_ieee(b, 8)) return -EINVAL;
  uint32_t progress = sys_get_be32(b + 4);
  if (progress > 3600000) return -EINVAL;
  measured_ms = persisted_ms = progress;
  complete = progress == 3600000;
  return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(initial_settling, "initial_settling", NULL, load, NULL, NULL);
static int save(bool done)
{
  if (!boot_is_img_confirmed()) return -EBUSY;
  uint8_t b[12] = {1};
  uint32_t progress = done ? 3600000 : measured_ms;
  sys_put_be32(progress, b + 4);
  sys_put_be32(crc32_ieee(b, 8), b + 8);
  int rc = settings_save_one("initial_settling/complete", b, sizeof(b));
  if (!rc) persisted_ms = progress;
  return rc;
}

/* Called with the sensor mutex held, as is observe. */
int initial_settling_reset(void)
{
  complete = false;
  measured_ms = persisted_ms = 0;
  last_sample = -1;
  next_blink = next_save = 0;
  return save(false);
}
void initial_settling_observe(bool valid, bool single_shot, bool paired, bool low_battery)
{
  if (complete || !paired) return;
  int64_t now = k_uptime_get();
  if (valid) {
    if (last_sample >= 0 && now - last_sample <= (single_shot ? 15000 : 2500)) {
      measured_ms += (uint32_t)(now - last_sample);
    }
    last_sample = now;
  } else last_sample = -1;
  if (measured_ms >= 3600000) {
    if (now >= next_save) {
      next_save = now + 30000;
      if (!save(true)) complete = true;
    }
    return;
  }
  if (measured_ms - persisted_ms >= 60000 && now >= next_save) {
    next_save = now + 30000;
    (void)save(false);
  }
  if (now >= next_blink) {
    next_blink = now + 10000;
    if (!low_battery) led_indicator_show_settling_tick();
  }
}

uint32_t initial_settling_remaining_seconds(void)
{
  if (complete || measured_ms >= 3600000) return 0;
  return (3600000 - measured_ms + 999) / 1000;
}
