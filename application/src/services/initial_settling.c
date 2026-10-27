/* First-hour indication only: this does not certify accuracy or ASC convergence.
 * Count successful sensing, never time spent powered off. Persist completion
 * once; an interrupted first hour restarts conservatively after reboot. */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/dfu/mcuboot.h>
#include <services/initial_settling.h>
#include <services/led_indicator.h>

static bool complete;
static uint32_t measured_ms;
static int64_t last_sample = -1, next_blink, next_save;
static int load(const char *name, size_t len, settings_read_cb read, void *arg)
{
  if (strcmp(name, "complete")) return -ENOENT;
  uint8_t value;
  if (len != 1 || read(arg, &value, 1) != 1 || value > 1) return -EINVAL;
  complete = value != 0;
  return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(initial_settling, "initial_settling", NULL, load, NULL, NULL);
static int save(bool done)
{
  if (!boot_is_img_confirmed()) return -EBUSY;
  uint8_t value = done;
  return settings_save_one("initial_settling/complete", &value, 1);
}
/* Called with the sensor mutex held, as is observe. */
int initial_settling_reset(void)
{
  complete = false;
  measured_ms = 0;
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
  if (now >= next_blink) {
    next_blink = now + 10000;
    if (!low_battery) led_indicator_show_settling_tick();
  }
}
