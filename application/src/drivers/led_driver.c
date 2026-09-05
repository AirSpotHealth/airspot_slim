/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <airspot/board.h>
#include <drivers/led_driver.h>

LOG_MODULE_REGISTER(led_driver, CONFIG_AIRSPOT_LOG_LEVEL);

/* For gpio-leds, the device is the parent leds node, not individual led nodes
 */
#define LED_NODE DT_NODELABEL(leds)

static const struct device *led_dev = DEVICE_DT_GET(LED_NODE);
static bool initialized = false;
static bool led_states[AIRSPOT_LED_COUNT] = {false};

int led_driver_init(void) {
  if (!device_is_ready(led_dev)) {
    LOG_ERR("LED device not ready");
    return -ENODEV;
  }

  initialized = true;
  LOG_INF("LED driver initialized");
  return 0;
}

int led_driver_set(uint8_t led_id, bool state) {
  if (!initialized) {
    return -ENODEV;
  }

  if (led_id >= AIRSPOT_LED_COUNT) {
    return -EINVAL;
  }

  led_states[led_id] = state;
  return led_set_brightness(led_dev, led_id, state ? 100 : 0);
}

int led_driver_toggle(uint8_t led_id) {
  if (!initialized) {
    return -ENODEV;
  }

  if (led_id >= AIRSPOT_LED_COUNT) {
    return -EINVAL;
  }

  /* Toggle using tracked state */
  return led_driver_set(led_id, !led_states[led_id]);
}

int led_driver_set_brightness(uint8_t led_id, uint8_t brightness) {
  if (!initialized) {
    return -ENODEV;
  }

  if (led_id >= AIRSPOT_LED_COUNT) {
    return -EINVAL;
  }

  if (brightness > 100) {
    return -EINVAL;
  }

  return led_set_brightness(led_dev, led_id, brightness);
}

int led_driver_get_state(uint8_t led_id, bool *state) {
  if (!initialized) {
    return -ENODEV;
  }

  if (led_id >= AIRSPOT_LED_COUNT || state == NULL) {
    return -EINVAL;
  }

  /* Return tracked state */
  *state = led_states[led_id];
  return 0;
}

void led_driver_startup_sequence(void) {
  if (!initialized) {
    return;
  }

  /* Blink all LEDs in sequence */
  for (uint8_t i = 0; i < AIRSPOT_LED_COUNT; i++) {
    led_driver_set(i, true);
    k_sleep(K_MSEC(100));
    led_driver_set(i, false);
    k_sleep(K_MSEC(100));
  }

  /* All LEDs on briefly */
  for (uint8_t i = 0; i < AIRSPOT_LED_COUNT; i++) {
    led_driver_set(i, true);
  }
  k_sleep(K_MSEC(200));

  /* All LEDs off */
  led_driver_all_off();
}

void led_driver_find_device_sequence(void) {
  if (!initialized) {
    return;
  }

  /* Rapid flash all LEDs for ~5 seconds to help locate device */
  for (int cycle = 0; cycle < 17; cycle++) {
    /* All LEDs on */
    for (uint8_t i = 0; i < AIRSPOT_LED_COUNT; i++) {
      led_driver_set(i, true);
    }
    k_sleep(K_MSEC(150));

    /* All LEDs off */
    for (uint8_t i = 0; i < AIRSPOT_LED_COUNT; i++) {
      led_driver_set(i, false);
    }
    k_sleep(K_MSEC(150));
  }
}

void led_driver_all_off(void) {
  if (!initialized) {
    return;
  }

  for (uint8_t i = 0; i < AIRSPOT_LED_COUNT; i++) {
    led_driver_set(i, false);
  }
}
