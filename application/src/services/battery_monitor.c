#include <zephyr/kernel.h>
#include <drivers/battery.h>
#include <drivers/qi_charger.h>
#include <services/airspot_nus.h>
#include <services/battery_monitor.h>
#include <services/ble_service.h>
#include <services/protocol.h>

bool battery_monitor_poll(void)
{
  static int64_t next_check, next_report;
  static bool last_charging, was_ready;
  static uint32_t last_session;
  bool charging = qi_charger_is_charging();
  bool ready = airspot_nus_is_ready();
  uint32_t session = airspot_nus_generation();
  bool subscribed = ready && (!was_ready || session != last_session);
  was_ready = ready;
  last_session = session;

  int64_t now = k_uptime_get();
  bool report = now >= next_report || charging != last_charging || subscribed;
  if (now < next_check && !report) return false;
  last_charging = charging;

  /* Uses the serialized fresh-voltage path, including its failure indication.
   * An ADC fault must not be interpreted as either an empty or a healthy cell. */
  uint16_t mv;
  int rc = battery_get_ota_voltage(&mv);
  /* Protection is independent of the one-minute BLE reporting interval.
   * The voltage API shares sensor ADC samples and refreshes data at most five
   * seconds old, so polling here does not mean an ADC conversion every second. */
  next_check = now + (rc ? 5000 : 1000);
  if (report) {
    next_report = now + (rc ? 5000 : 60000);
    if (!rc) ble_service_update_battery_level(battery_get_percentage());
    if (ready && protocol_send_battery_level() != 0) {
      next_report = now + 1000; /* Retry notifications without delaying protection. */
    }
  }
  return !rc && mv < BATTERY_LOCKOUT_MV && !qi_charger_is_charging();
}
