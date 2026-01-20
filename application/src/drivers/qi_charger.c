/*
 * Copyright (c) 2026 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Qi Charger Detection Driver Implementation
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/qi_charger.h>

LOG_MODULE_REGISTER(qi_charger, LOG_LEVEL_INF);

/* Qi charger status pin from device tree */
#define QI_CHG_NODE DT_NODELABEL(qi_chg_status)

#if DT_NODE_EXISTS(QI_CHG_NODE)
static const struct gpio_dt_spec qi_chg_gpio =
    GPIO_DT_SPEC_GET(QI_CHG_NODE, gpios);
#define HAS_QI_CHG_GPIO 1
#else
/* Fallback to hardcoded pin if not in device tree */
#define QI_CHG_PORT_NODE DT_NODELABEL(gpio0)
#define QI_CHG_PIN 0 /* P0.00 */
#define HAS_QI_CHG_GPIO 0
static const struct device *qi_chg_dev;
#endif

static struct gpio_callback gpio_cb;
static qi_charger_callback_t user_callback = NULL;
static bool initialized = false;

static void qi_charger_gpio_handler(const struct device *dev,
                                    struct gpio_callback *cb, uint32_t pins) {
  ARG_UNUSED(dev);
  ARG_UNUSED(cb);
  ARG_UNUSED(pins);

  bool charging = qi_charger_is_charging();
  LOG_INF("Qi charging status changed: %s",
          charging ? "CHARGING" : "NOT CHARGING");

  if (user_callback) {
    user_callback(charging);
  }
}

int qi_charger_init(void) {
  int ret;

  if (initialized) {
    return 0;
  }

  LOG_INF("Initializing Qi charger detection...");

#if HAS_QI_CHG_GPIO
  if (!gpio_is_ready_dt(&qi_chg_gpio)) {
    LOG_ERR("Qi charger GPIO device not ready");
    return -ENODEV;
  }

  /* Configure as input - device tree should have GPIO_ACTIVE_LOW set */
  ret = gpio_pin_configure_dt(&qi_chg_gpio, GPIO_INPUT);
  if (ret < 0) {
    LOG_ERR("Failed to configure Qi GPIO: %d", ret);
    return ret;
  }

  /* Configure interrupt for both edges */
  ret = gpio_pin_interrupt_configure_dt(&qi_chg_gpio, GPIO_INT_EDGE_BOTH);
  if (ret < 0) {
    LOG_WRN("Failed to configure Qi interrupt: %d (using polling)", ret);
    /* Continue without interrupt - polling will still work */
  } else {
    /* Setup callback */
    gpio_init_callback(&gpio_cb, qi_charger_gpio_handler, BIT(qi_chg_gpio.pin));
    ret = gpio_add_callback(qi_chg_gpio.port, &gpio_cb);
    if (ret < 0) {
      LOG_WRN("Failed to add Qi callback: %d", ret);
    }
  }
#else
  /* Fallback to hardcoded configuration */
  qi_chg_dev = DEVICE_DT_GET(QI_CHG_PORT_NODE);
  if (!device_is_ready(qi_chg_dev)) {
    LOG_ERR("GPIO0 device not ready");
    return -ENODEV;
  }

  ret = gpio_pin_configure(qi_chg_dev, QI_CHG_PIN, GPIO_INPUT | GPIO_PULL_UP);
  if (ret < 0) {
    LOG_ERR("Failed to configure Qi GPIO: %d", ret);
    return ret;
  }

  /* Try to configure interrupt */
  ret =
      gpio_pin_interrupt_configure(qi_chg_dev, QI_CHG_PIN, GPIO_INT_EDGE_BOTH);
  if (ret < 0) {
    LOG_WRN("Failed to configure Qi interrupt: %d (using polling)", ret);
  } else {
    gpio_init_callback(&gpio_cb, qi_charger_gpio_handler, BIT(QI_CHG_PIN));
    ret = gpio_add_callback(qi_chg_dev, &gpio_cb);
    if (ret < 0) {
      LOG_WRN("Failed to add Qi callback: %d", ret);
    }
  }
#endif

  initialized = true;

  bool charging = qi_charger_is_charging();
  LOG_INF("Qi charger initialized (currently %s)",
          charging ? "CHARGING" : "not charging");

  return 0;
}

bool qi_charger_is_charging(void) {
  if (!initialized) {
    return false;
  }

  int val;

#if HAS_QI_CHG_GPIO
  /* gpio_pin_get_dt returns logical value (accounting for GPIO_ACTIVE_LOW) */
  val = gpio_pin_get_dt(&qi_chg_gpio);
#else
  val = gpio_pin_get(qi_chg_dev, QI_CHG_PIN);
  /* Invert for active-low */
  val = (val == 0) ? 1 : 0;
#endif

  if (val < 0) {
    LOG_ERR("Failed to read Qi GPIO: %d", val);
    return false;
  }

  /* With GPIO_ACTIVE_LOW in device tree, val=1 means charging */
  return (val == 1);
}

void qi_charger_register_callback(qi_charger_callback_t callback) {
  user_callback = callback;
}
