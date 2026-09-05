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

LOG_MODULE_REGISTER(qi_charger, CONFIG_AIRSPOT_LOG_LEVEL);

#define QI_DEBOUNCE_SAMPLES 3
#define QI_DEBOUNCE_INTERVAL_US 30000
#define QI_BOOT_SETTLE_MS 250
#define QI_GPIO_DEBOUNCE_MS 80

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
static struct k_work_delayable debounce_work;
static qi_charger_callback_t user_callback = NULL;
static bool initialized = false;
static bool stable_charging;

static bool read_gpio_charging(void)
{
  int val;

#if HAS_QI_CHG_GPIO
  val = gpio_pin_get_dt(&qi_chg_gpio);
#else
  val = gpio_pin_get(qi_chg_dev, QI_CHG_PIN);
  val = (val == 0) ? 1 : 0;
#endif

  if (val < 0) {
    LOG_ERR("Failed to read Qi GPIO: %d", val);
    return false;
  }

  return (val == 1);
}

static void run_debounce(void)
{
  int votes = 0;

  for (int i = 0; i < QI_DEBOUNCE_SAMPLES; i++) {
    if (read_gpio_charging()) {
      votes++;
    }
    if (i < (QI_DEBOUNCE_SAMPLES - 1)) {
      k_usleep(QI_DEBOUNCE_INTERVAL_US);
    }
  }

  bool charging = votes >= 2;
  if (charging == stable_charging) {
    return;
  }

  stable_charging = charging;
  LOG_INF("Qi charging (debounced): %s",
          charging ? "CHARGING" : "NOT CHARGING");

  if (user_callback) {
    user_callback(charging);
  }
}

static void debounce_work_handler(struct k_work *work)
{
  ARG_UNUSED(work);
  run_debounce();
}

static void qi_charger_gpio_handler(const struct device *dev,
                                    struct gpio_callback *cb, uint32_t pins)
{
  ARG_UNUSED(dev);
  ARG_UNUSED(cb);
  ARG_UNUSED(pins);

  k_work_schedule(&debounce_work, K_MSEC(QI_GPIO_DEBOUNCE_MS));
}

int qi_charger_init(void)
{
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

  ret = gpio_pin_configure_dt(&qi_chg_gpio, GPIO_INPUT);
  if (ret < 0) {
    LOG_ERR("Failed to configure Qi GPIO: %d", ret);
    return ret;
  }

  ret = gpio_pin_interrupt_configure_dt(&qi_chg_gpio, GPIO_INT_EDGE_BOTH);
  if (ret < 0) {
    LOG_WRN("Failed to configure Qi interrupt: %d (using polling)", ret);
  } else {
    gpio_init_callback(&gpio_cb, qi_charger_gpio_handler, BIT(qi_chg_gpio.pin));
    ret = gpio_add_callback(qi_chg_gpio.port, &gpio_cb);
    if (ret < 0) {
      LOG_WRN("Failed to add Qi callback: %d", ret);
    }
  }
#else
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

  k_work_init_delayable(&debounce_work, debounce_work_handler);
  stable_charging = false;
  initialized = true;

  /* Let the Qi status line settle before the first read (avoids false CHARGING at boot). */
  k_work_schedule(&debounce_work, K_MSEC(QI_BOOT_SETTLE_MS));

  return 0;
}

bool qi_charger_is_charging(void)
{
  if (!initialized) {
    return false;
  }

  return stable_charging;
}

void qi_charger_register_callback(qi_charger_callback_t callback)
{
  user_callback = callback;
}
