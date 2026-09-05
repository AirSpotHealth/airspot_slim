/* Rev3A nPM1100 MODE experiment. No calibration or NVS operations. */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <drivers/supply_diagnostic.h>

BUILD_ASSERT(DT_GPIO_PIN(DT_PATH(zephyr_user), pmic_mode_gpios) == 9);
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(DT_PATH(zephyr_user), pmic_mode_gpios),
                          DT_NODELABEL(gpio1)));
BUILD_ASSERT(DT_GPIO_FLAGS(DT_PATH(zephyr_user), pmic_mode_gpios) == GPIO_ACTIVE_HIGH);

static const struct gpio_dt_spec mode_pin =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), pmic_mode_gpios);
K_MUTEX_DEFINE(supply_mutex);
static bool pwm_active;
static int64_t deadline;
static int restore_error;
static void expire(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(expiry_work, expire);

static int release_locked(void)
{
  /* P1.09 is otherwise unused. Restore its disconnected input and let the
   * schematic's external pull-down select automatic regulator operation. */
  int ret = gpio_pin_configure_dt(&mode_pin, GPIO_DISCONNECTED);
  restore_error = ret;
  if (!ret) {
    pwm_active = false;
    deadline = 0;
    k_work_cancel_delayable(&expiry_work);
  } else {
    k_work_reschedule(&expiry_work, K_MSEC(1000));
  }
  return ret;
}

static void expire(struct k_work *work)
{
  ARG_UNUSED(work);
  k_mutex_lock(&supply_mutex, K_FOREVER);
  if (pwm_active && k_uptime_get() >= deadline) release_locked();
  k_mutex_unlock(&supply_mutex);
}

int supply_diagnostic_control(uint8_t action, supply_diagnostic_t *state)
{
  if (!state) return -EINVAL;
  k_mutex_lock(&supply_mutex, K_FOREVER);
  int ret = 0;
  if (action > 2) ret = -EINVAL;
  else if (!gpio_is_ready_dt(&mode_pin)) ret = -ENODEV;
  else if (action == 0) ret = release_locked();
  else if (action == 1) {
    if (!pwm_active) {
      ret = gpio_pin_configure_dt(&mode_pin, GPIO_OUTPUT_ACTIVE | GPIO_INPUT);
      if (!ret) pwm_active = true;
      /* Nordic specifies a 55 us transition. Allow settling before returning
       * or starting the diagnostic observation window. */
      k_busy_wait(100);
    }
    if (!ret) {
      int level = gpio_pin_get_dt(&mode_pin);
      if (level != 1) {
        ret = level < 0 ? level : -EIO;
        release_locked();
      } else {
        restore_error = 0;
        deadline = k_uptime_get() + 300000;
        k_work_reschedule(&expiry_work, K_MSEC(300000));
      }
    }
  } else ret = restore_error;
  int64_t remaining = deadline - k_uptime_get();
  state->pwm_active = pwm_active;
  state->seconds_remaining = pwm_active && remaining > 0 ? (remaining + 999) / 1000 : 0;
  k_mutex_unlock(&supply_mutex);
  return ret;
}
