/* Shared soft-off request for the protocol and main-thread power state machine. */
#include <zephyr/sys/atomic.h>
#include <services/power_manager.h>

static atomic_t off_requested;

int power_manager_init(void)
{
  atomic_clear(&off_requested);
  return 0;
}

void power_manager_enter_off_state(void)
{
  atomic_set(&off_requested, 1);
}

bool power_manager_take_off_request(void)
{
  return atomic_cas(&off_requested, 1, 0);
}

/* Rev3A: P2.03 drives nPM1100 SHPACT (TP41). No sensor-only power switch. */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <drivers/qi_charger.h>
#include <drivers/button_driver.h>
static atomic_t factory_off;
static const struct device *const ship_gpio = DEVICE_DT_GET(DT_NODELABEL(gpio2));

int power_manager_prepare_factory_off(void)
{
  bool pressed = true;
  int rc = button_driver_read_now(&pressed);
  if (rc) return rc;
  if (pressed || qi_charger_is_charging()) return -EBUSY;
  if (!device_is_ready(ship_gpio)) return -ENODEV;
  return gpio_pin_configure(ship_gpio, 3, GPIO_OUTPUT_INACTIVE);
}
void power_manager_request_factory_off(void) { atomic_set(&factory_off, 1); }
bool power_manager_factory_off_requested(void) { return atomic_get(&factory_off) != 0; }
int power_manager_cut_battery(void)
{
  int rc = power_manager_prepare_factory_off();
  if (rc) return rc;
  rc = gpio_pin_set(ship_gpio, 3, 1);
  if (rc) return rc;
  /* PMIC requires >=200 ms. Execution should stop as supply collapses. */
  k_msleep(1000);
  (void)gpio_pin_set(ship_gpio, 3, 0);
  return -ETIMEDOUT; /* Still powered: never claim the power cut succeeded. */
}
