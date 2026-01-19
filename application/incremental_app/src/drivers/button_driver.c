/*
 * Copyright (c) 2026 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/button_driver.h>

LOG_MODULE_REGISTER(button_driver, LOG_LEVEL_DBG);

#define BUTTON_NODE DT_ALIAS(sw0)
#ifndef CONFIG_AIRSPOT_BUTTON_LONG_PRESS_TIMEOUT_MS
#define LONG_PRESS_TIMEOUT_MS 3000 /* Default 3 seconds */
#else
#define LONG_PRESS_TIMEOUT_MS CONFIG_AIRSPOT_BUTTON_LONG_PRESS_TIMEOUT_MS
#endif

static const struct gpio_dt_spec button =
    GPIO_DT_SPEC_GET_OR(BUTTON_NODE, gpios, {0});
static struct gpio_callback button_cb_data;
static button_callback_t user_callback = NULL;
static struct k_work_delayable long_press_work;
static struct k_work_delayable poll_work;
static bool button_pressed = false;
static bool initialized = false;
static bool use_polling = false;

static void button_long_press_work_handler(struct k_work *work)
{
  if (button_pressed && user_callback)
  {
    LOG_INF("Button long press timer fired");
    user_callback(BUTTON_EVENT_LONG_PRESS);
  }
}

/* Polling mode handler - checks button state periodically */
static void button_poll_work_handler(struct k_work *work)
{
  static uint32_t poll_count = 0;

  if (!initialized || !use_polling)
  {
    LOG_ERR("Button poll: not initialized or not in polling mode!");
    return;
  }

  poll_count++;

  int pin_state = gpio_pin_get_dt(&button);
  if (pin_state < 0)
  {
    /* Error reading pin - reschedule and try again */
    LOG_ERR("Button poll: failed to read pin: %d", pin_state);
    k_work_schedule(&poll_work, K_MSEC(50));
    return;
  }

  bool current_state = (pin_state == 1); /* GPIO_ACTIVE_HIGH: 1 = pressed */

  /* Detect state changes */
  if (current_state != button_pressed)
  {
    button_pressed = current_state;

    if (button_pressed)
    {
      /* Button just pressed */
      printk("\n>>> BUTTON PRESSED (polling) <<<\n");
      LOG_INF("Button poll: pressed (pin_state=1, poll #%u)", poll_count);
      if (user_callback)
      {
        user_callback(BUTTON_EVENT_PRESSED);
      }
      else
      {
        LOG_WRN("Button poll: callback not registered!");
      }
      /* Start long press timer */
      k_work_schedule(&long_press_work, K_MSEC(LONG_PRESS_TIMEOUT_MS));
    }
    else
    {
      /* Button just released */
      printk("\n>>> BUTTON RELEASED (polling) <<<\n");
      LOG_INF("Button poll: released (pin_state=0, poll #%u)", poll_count);
      /* Cancel long press timer */
      k_work_cancel_delayable(&long_press_work);
      if (user_callback)
      {
        user_callback(BUTTON_EVENT_RELEASED);
      }
      else
      {
        LOG_WRN("Button poll: callback not registered!");
      }
    }
  }
  else if (button_pressed)
  {
    /* Button still pressed - check if long press timer expired */
    /* (This is handled by the long_press_work handler) */
  }

  /* Reschedule polling (50ms = 20Hz polling rate) */
  k_work_schedule(&poll_work, K_MSEC(50));
}

static void button_pressed_handler(const struct device *dev,
                                   struct gpio_callback *cb, uint32_t pins)
{
  uint32_t pin_state = gpio_pin_get_dt(&button);

  /* Button is GPIO_ACTIVE_HIGH, so pressed = HIGH (1), released = LOW (0) */
  if (pin_state == 1)
  {
    /* Button pressed */
    button_pressed = true;
    LOG_INF("Button interrupt: pressed (pin_state=1, GPIO_ACTIVE_HIGH)");

    if (user_callback)
    {
      user_callback(BUTTON_EVENT_PRESSED);
    }

    /* Start long press timer */
    k_work_schedule(&long_press_work, K_MSEC(LONG_PRESS_TIMEOUT_MS));
  }
  else
  {
    /* Button released */
    button_pressed = false;
    LOG_INF("Button interrupt: released (pin_state=0, GPIO_ACTIVE_HIGH)");

    /* Cancel long press timer */
    k_work_cancel_delayable(&long_press_work);

    if (user_callback)
    {
      user_callback(BUTTON_EVENT_RELEASED);
    }
  }
}

int button_driver_init(void)
{
  int ret;

  if (!gpio_is_ready_dt(&button))
  {
    LOG_ERR("Button device not ready");
    return -ENODEV;
  }

  ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
  if (ret < 0)
  {
    LOG_ERR("Failed to configure button GPIO: %d", ret);
    return ret;
  }

  k_work_init_delayable(&long_press_work, button_long_press_work_handler);
  k_work_init_delayable(&poll_work, button_poll_work_handler);

  /* Configure interrupt for both edges (press and release) */
  /* Note: GPIO_INT_DEBOUNCE might not be supported on all GPIO ports */
  ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
  if (ret < 0)
  {
    LOG_ERR("Failed to configure button interrupt: %d", ret);
    LOG_ERR("Button pin: %d, port: %s", button.pin, button.port->name);
    LOG_WRN("Button will work but without interrupt support - using polling mode");
    /* Use polling mode instead */
    use_polling = true;
    initialized = true;
    LOG_INF("Button driver initialized (polling mode) on pin %d", button.pin);
    LOG_INF("Polling will start after callback is registered");
    /* Don't start polling here - wait for callback registration */
    return 0;
  }

  /* Interrupt mode - set up callback */
  gpio_init_callback(&button_cb_data, button_pressed_handler, BIT(button.pin));
  gpio_add_callback(button.port, &button_cb_data);

  use_polling = false;
  initialized = true;
  LOG_INF("Button driver initialized (interrupt mode) on pin %d", button.pin);
  return 0;
}

void button_driver_register_callback(button_callback_t callback)
{
  user_callback = callback;

  LOG_INF("Button callback registered: %p", (void *)callback);
  LOG_INF("Button driver state: initialized=%d, use_polling=%d", initialized, use_polling);

  /* If in polling mode, start polling now that callback is registered */
  if (initialized && use_polling && callback != NULL)
  {
    LOG_INF("Starting button polling (callback registered)");
    LOG_INF("Polling will check button every 50ms");
    int ret = k_work_schedule(&poll_work, K_MSEC(50));
    if (ret < 0)
    {
      LOG_ERR("Failed to schedule polling work: %d", ret);
    }
    else
    {
      LOG_INF("Polling work scheduled successfully");
    }
  }
  else
  {
    LOG_WRN("Polling NOT started: initialized=%d, use_polling=%d, callback=%p",
            initialized, use_polling, (void *)callback);
  }
}

int button_driver_get_state(bool *pressed)
{
  if (!initialized || pressed == NULL)
  {
    return -ENODEV;
  }

  *pressed = button_pressed;
  return 0;
}
