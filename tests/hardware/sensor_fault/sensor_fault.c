/* Bench-only fault injection. Never include this module in release builds.
 * Allow startup/OTA confirmation first, then fail all measurement reads.
 * Reboot restarts the 60-second healthy window; no persistent setting is used.
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/stcc4.h>
LOG_MODULE_REGISTER(sensor_fault_test, LOG_LEVEL_INF);
int __real_stcc4_read(stcc4_data_t *data);
static bool window_started;
static int64_t first_read_ms;
int __wrap_stcc4_read(stcc4_data_t *data)
{
  int64_t now = k_uptime_get();
  if (!window_started) {
    first_read_ms = now;
    window_started = true;
  }
  if (now - first_read_ms < 60000) return __real_stcc4_read(data);
  LOG_WRN("BENCH TEST: injecting STCC4 read -EIO; no valid CO2 sample");
  if (data) data->valid = false;
  return -EIO;
}
