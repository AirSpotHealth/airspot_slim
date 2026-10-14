/* Production P0.02 button: GPIO SENSE interrupts, debounced off the ISR. */
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <drivers/button_driver.h>
#include <drivers/button_hold.h>

LOG_MODULE_REGISTER(button_driver, CONFIG_AIRSPOT_LOG_LEVEL);

#define BUTTON_DEBOUNCE_MS 20
#define LONG_PRESS_TIMEOUT_MS CONFIG_AIRSPOT_BUTTON_LONG_PRESS_TIMEOUT_MS
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback gpio_callback;
static button_callback_t user_callback;
static struct k_work_delayable debounce_work;
static struct k_work_delayable long_press_work;
static atomic_t button_pressed;
static atomic_t last_edge_ms;
static atomic_t enabled;
static atomic_t ignore_until_release;
static bool initialized;
static uint32_t press_started;
K_SEM_DEFINE(button_changed, 0, 1);

/* Sensor reads block the system workqueue; button gestures must not wait for it. */
static K_KERNEL_STACK_DEFINE(button_wq_stack, 2048);
static struct k_work_q button_wq;

static void long_press_handler(struct k_work *work)
{
  ARG_UNUSED(work);
  if (!atomic_get(&enabled) || !atomic_get(&button_pressed) ||
      atomic_get(&ignore_until_release)) { return; }
  /* A release may have occurred while its debounce work was still pending. */
  if (gpio_pin_get_dt(&button) != 1) { return; }
  uint32_t elapsed = k_uptime_get_32() - press_started;
  button_event_t event;
  uint32_t next;
  if (elapsed >= BUTTON_RECOVERY_EXPIRED_MS) {
    event = BUTTON_EVENT_RECOVERY_CANCELLED;
    next = 0;
  } else if (elapsed >= BUTTON_RECOVERY_READY_MS) {
    event = BUTTON_EVENT_RECOVERY_READY;
    next = BUTTON_RECOVERY_EXPIRED_MS;
  } else if (elapsed >= BUTTON_RECOVERY_WARNING_MS) {
    event = BUTTON_EVENT_RECOVERY_WARNING;
    next = BUTTON_RECOVERY_READY_MS;
  } else {
    event = BUTTON_EVENT_LONG_PRESS;
    next = BUTTON_RECOVERY_WARNING_MS;
  }
  if (user_callback) user_callback(event);
  if (next) {
    k_work_reschedule_for_queue(&button_wq, &long_press_work, K_MSEC(next - elapsed));
  }
}

static void debounce_handler(struct k_work *work)
{
  ARG_UNUSED(work);
  if (!atomic_get(&enabled)) { return; }
  uint32_t edge = (uint32_t)atomic_get(&last_edge_ms);
  uint32_t age = k_uptime_get_32() - edge;
  if (age < BUTTON_DEBOUNCE_MS) {
    k_work_reschedule_for_queue(&button_wq, &debounce_work,
                               K_MSEC(BUTTON_DEBOUNCE_MS - age));
    return;
  }
  int state = gpio_pin_get_dt(&button);
  if (state < 0) {
    LOG_ERR("Button GPIO read failed: %d", state);
    return; /* Do not turn a GPIO failure into a press or an endless poll. */
  }
  if (edge != (uint32_t)atomic_get(&last_edge_ms)) {
    k_work_reschedule_for_queue(&button_wq, &debounce_work, K_MSEC(BUTTON_DEBOUNCE_MS));
    return;
  }
  if (state == atomic_get(&button_pressed)) { return; }
  atomic_set(&button_pressed, state);
  LOG_INF("Button: %s", state ? "pressed" : "released");
  if (atomic_get(&ignore_until_release)) {
    if (!state) { atomic_clear(&ignore_until_release); }
    return;
  }
  if (state) {
    press_started = edge;
    k_work_reschedule_for_queue(&button_wq, &long_press_work, K_MSEC(LONG_PRESS_TIMEOUT_MS));
  } else {
    k_work_cancel_delayable(&long_press_work);
    /* Use the physical release edge, not delayed worker execution time. */
    uint32_t elapsed = edge - press_started;
    if (user_callback && button_hold_release(elapsed, false) == BUTTON_HOLD_RECOVER) {
      user_callback(BUTTON_EVENT_RECOVERY_CONFIRMED);
    } else if (user_callback && elapsed >= BUTTON_RECOVERY_WARNING_MS) {
      user_callback(BUTTON_EVENT_RECOVERY_CANCELLED);
    }
  }
  if (user_callback) {
    user_callback(state ? BUTTON_EVENT_PRESSED : BUTTON_EVENT_RELEASED);
  }
}

static void edge_handler(const struct device *port, struct gpio_callback *callback,
                         uint32_t pins)
{
  ARG_UNUSED(port);
  ARG_UNUSED(callback);
  ARG_UNUSED(pins);
  atomic_set(&last_edge_ms, k_uptime_get_32());
  k_sem_give(&button_changed);
  if (atomic_get(&enabled)) {
    k_work_reschedule_for_queue(&button_wq, &debounce_work, K_MSEC(BUTTON_DEBOUNCE_MS));
  }
}

int button_driver_init(void)
{
  if (initialized) { return 0; }
  if (!gpio_is_ready_dt(&button)) { return -ENODEV; }
  int rc = gpio_pin_configure_dt(&button, GPIO_INPUT);
  if (rc) { return rc; }
  int state = gpio_pin_get_dt(&button);
  if (state < 0) { return state; }
  atomic_set(&button_pressed, state);
  k_work_init_delayable(&debounce_work, debounce_handler);
  k_work_init_delayable(&long_press_work, long_press_handler);
  gpio_init_callback(&gpio_callback, edge_handler, BIT(button.pin));
  rc = gpio_add_callback(button.port, &gpio_callback);
  if (rc) { return rc; }
  /* Initialize the queue before enabling any interrupt that can submit work. */
  k_work_queue_init(&button_wq);
  k_work_queue_start(&button_wq, button_wq_stack, K_KERNEL_STACK_SIZEOF(button_wq_stack),
                     K_PRIO_PREEMPT(8), NULL);
  k_thread_name_set(&button_wq.thread, "button_wq");
  rc = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
  if (rc) {
    LOG_ERR("Button interrupt setup failed: %d", rc);
    return rc;
  }
  initialized = true;
  atomic_set(&enabled, 1);
  /* Reconcile an edge occurring between the initial read and interrupt enable. */
  atomic_set(&last_edge_ms, k_uptime_get_32());
  k_work_reschedule_for_queue(&button_wq, &debounce_work, K_MSEC(BUTTON_DEBOUNCE_MS));
  LOG_INF("Button initialized: GPIO SENSE interrupts on P0.02, debounce %d ms", BUTTON_DEBOUNCE_MS);
  return 0;
}

void button_driver_register_callback(button_callback_t callback)
{
  /* A button held at boot/wake is baseline, not a new power-off gesture. */
  if (initialized) {
    atomic_set(&ignore_until_release, gpio_pin_get_dt(&button) != 0);
    k_work_cancel_delayable(&long_press_work);
  }
  user_callback = callback;
}

int button_driver_get_state(bool *pressed)
{
  if (!initialized || !pressed) { return -ENODEV; }
  *pressed = atomic_get(&button_pressed) != 0;
  return 0;
}

int button_driver_read_now(bool *pressed)
{
  if (!initialized || !pressed) { return -ENODEV; }
  int state = gpio_pin_get_dt(&button);
  if (state < 0) { return state; }
  *pressed = state == 1;
  return 0;
}

int button_driver_wait_for_change(k_timeout_t timeout)
{
  return k_sem_take(&button_changed, timeout);
}

int button_driver_prepare_wakeup(void)
{
  /* Main-thread shutdown only. Keep IRQ edges for release waiting until here. */
  atomic_clear(&enabled);
  int rc = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_DISABLE);
  if (rc) { return rc; }
  struct k_work_sync sync;
  k_work_cancel_delayable_sync(&debounce_work, &sync);
  k_work_cancel_delayable_sync(&long_press_work, &sync);
  user_callback = NULL;
  return 0;
}
