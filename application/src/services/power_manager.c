/*
 * Copyright (c) 2026 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Power Manager Implementation
 *
 * Handles System OFF (shipping mode) and power gesture detection.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>

#include <airspot/board.h>
#include <drivers/battery.h>
#include <drivers/button_driver.h>
#include <drivers/led_driver.h>
#include <drivers/qi_charger.h>
#include <services/ble_service.h>
#include <services/power_manager.h>
#include <services/sensor_manager.h>

LOG_MODULE_REGISTER(power_manager, LOG_LEVEL_INF);

/* Button GPIO for direct access during wake validation */
#define BUTTON_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec button_gpio =
    GPIO_DT_SPEC_GET_OR(BUTTON_NODE, gpios, {0});

/* Gesture state machine */
typedef enum {
  GESTURE_STATE_IDLE,
  GESTURE_STATE_TAP_DETECTED, /* First tap done, waiting for second press */
  GESTURE_STATE_HOLDING,      /* Button held, counting to 3s */
} gesture_state_t;

static gesture_state_t gesture_state = GESTURE_STATE_IDLE;
static int64_t tap_time = 0;
static int64_t hold_start_time = 0;
static bool initialized = false;

/* Work item for gesture timeout */
static struct k_work_delayable gesture_timeout_work;

/* Forward declarations */
static void gesture_timeout_handler(struct k_work *work);

int power_manager_init(void) {
  if (!gpio_is_ready_dt(&button_gpio)) {
    LOG_WRN("Button GPIO not ready for power manager");
  }

  k_work_init_delayable(&gesture_timeout_work, gesture_timeout_handler);

  gesture_state = GESTURE_STATE_IDLE;
  initialized = true;

  LOG_INF("Power manager initialized");
  return 0;
}

bool power_manager_is_wakeup_from_off(void) {
  /* Use Zephyr hwinfo API to detect reset cause */
  uint32_t reset_cause = 0;
  int ret = hwinfo_get_reset_cause(&reset_cause);

  if (ret < 0) {
    LOG_WRN("Failed to get reset cause: %d", ret);
    return false;
  }

  LOG_INF("Reset cause: 0x%08X", reset_cause);

  /* Check if reset was due to wake from low power (System OFF) */
  if (reset_cause & RESET_LOW_POWER_WAKE) {
    LOG_INF("Detected wake from System OFF via GPIO");
    /* Clear the reset cause */
    hwinfo_clear_reset_cause();
    return true;
  }

  /* Check for auto-wake condition (charging from depleted state) */
  if (qi_charger_is_charging()) {
    /* Initialize battery to check voltage */
    battery_init();
    /* If battery is present and charging, allow wake up */
    /* Note: We don't check for critical here because we WANT to wake up and
     * show charging LED */
    LOG_INF("Detected wake from Qi charger insertion");
    return true;
  }

  return false;
}

bool power_manager_validate_wakeup_gesture(void) {
  LOG_INF("Validating wake-up gesture...");

  /* Auto-wake if charging */
  if (qi_charger_is_charging()) {
    LOG_INF("Charging detected - skipping gesture validation (Auto Wake)");
    return true;
  }

  /*
   * Wake-up gesture: Tap → Tap-and-hold 3s
   *
   * At this point, the button was pressed to wake us up.
   * We need to:
   * 1. Wait for button release (first tap)
   * 2. Wait for button press again within 500ms
   * 3. Count 3s hold with LED feedback
   */

  /* Wait for initial button release (debounce from wake) */
  LOG_INF("Waiting for button release (tap completion)...");

  int timeout_ms = 1000; /* Max 1s for tap release */
  while (gpio_pin_get_dt(&button_gpio) && timeout_ms > 0) {
    k_msleep(10);
    timeout_ms -= 10;
  }

  if (timeout_ms <= 0) {
    LOG_WRN("Button stuck - invalid gesture");
    return false;
  }

  LOG_INF("Button released - tap completed");

  /* Wait for second press within window */
  LOG_INF("Waiting for second press within %dms...", POWER_GESTURE_WINDOW_MS);

  int64_t window_start = k_uptime_get();
  bool second_press = false;

  while ((k_uptime_get() - window_start) < POWER_GESTURE_WINDOW_MS) {
    if (gpio_pin_get_dt(&button_gpio)) {
      second_press = true;
      break;
    }
    k_msleep(10);
  }

  if (!second_press) {
    LOG_INF("No second press - returning to off");
    return false;
  }

  LOG_INF("Second press detected - hold for 3 seconds");

  /* Start LED feedback and count the hold */
  int64_t hold_start = k_uptime_get();
  int led_state = 0;
  int64_t last_blink = hold_start;

  while (gpio_pin_get_dt(&button_gpio)) {
    int64_t now = k_uptime_get();
    int64_t held_ms = now - hold_start;

    /* Check if hold complete */
    if (held_ms >= POWER_GESTURE_HOLD_DURATION_MS) {
      LOG_INF("Hold complete! Power ON gesture valid");

      /* Solid LED to indicate success */
      led_driver_set(1, true);
      k_msleep(200);
      led_driver_set(1, false);

      return true;
    }

    /* Blink LED to show progress */
    if ((now - last_blink) >= POWER_GESTURE_LED_BLINK_MS) {
      led_state = !led_state;
      led_driver_set(1, led_state);
      last_blink = now;
    }

    k_msleep(10);
  }

  /* Button released before 3s */
  LOG_INF("Button released too early - invalid gesture");
  led_driver_set(1, false);
  return false;
}

void power_manager_enter_off_state(void) {
  LOG_INF("========================================");
  LOG_INF("Entering Power Off (shipping mode)...");
  LOG_INF("========================================");

  /* Show power-off LED sequence (quick blinks) */
  for (int i = 0; i < 6; i++) {
    led_driver_set(0, i % 2); /* Red LED blink */
    k_msleep(100);
  }
  led_driver_all_off();

  /* 1. Stop BLE */
  LOG_INF("Stopping BLE advertising...");
  ble_service_stop_advertising();
  k_msleep(50);

  /* 2. Stop sensor manager */
  LOG_INF("Stopping sensor manager...");
  sensor_manager_stop();

  /* 3. Power down all LEDs */
  LOG_INF("Turning off LEDs...");
  led_driver_all_off();

  /* 4. Configure button as wake source with interrupt */
  LOG_INF("Configuring button GPIO as wake source...");
  if (gpio_is_ready_dt(&button_gpio)) {
    int rc;

    /* Configure GPIO as input */
    rc = gpio_pin_configure_dt(&button_gpio, GPIO_INPUT);
    if (rc < 0) {
      LOG_ERR("Could not configure button GPIO: %d", rc);
    }

    /* Enable interrupt for wake-up (level active = pressed state) */
    rc = gpio_pin_interrupt_configure_dt(&button_gpio, GPIO_INT_LEVEL_ACTIVE);
    if (rc < 0) {
      LOG_ERR("Could not configure GPIO interrupt: %d", rc);
    }
  }

  LOG_INF("Entering System OFF - goodbye!");
  LOG_INF("Wake with: Tap -> Tap-and-hold 3s");

  /* Clear reset cause before entering off */
  hwinfo_clear_reset_cause();

  /* Give time for logs to flush */
  k_msleep(100);

  /* Enter System OFF - this will not return */
  sys_poweroff();

  /* Should never reach here */
  LOG_ERR("Failed to enter System OFF!");
}

/* --- Power OFF gesture detection (when device is ON) --- */

static void gesture_timeout_handler(struct k_work *work) {
  ARG_UNUSED(work);

  if (gesture_state == GESTURE_STATE_TAP_DETECTED) {
    LOG_DBG("Gesture window expired");
    gesture_state = GESTURE_STATE_IDLE;
  }
}

void power_manager_on_tap_detected(void) {
  if (!initialized) {
    return;
  }

  /* Record tap time and start window timer */
  tap_time = k_uptime_get();
  gesture_state = GESTURE_STATE_TAP_DETECTED;

  /* Start timeout for gesture window */
  k_work_schedule(&gesture_timeout_work, K_MSEC(POWER_GESTURE_WINDOW_MS));

  LOG_DBG("Tap detected - waiting for hold gesture");
}

void power_manager_on_button_pressed(void) {
  if (!initialized) {
    return;
  }

  if (gesture_state == GESTURE_STATE_TAP_DETECTED) {
    /* Second press within window - start counting hold */
    int64_t now = k_uptime_get();
    if ((now - tap_time) <= POWER_GESTURE_WINDOW_MS) {
      hold_start_time = now;
      gesture_state = GESTURE_STATE_HOLDING;
      k_work_cancel_delayable(&gesture_timeout_work);
      LOG_INF("Hold started - keep holding for power off");
    }
  }
}

void power_manager_on_button_released(void) {
  if (!initialized) {
    return;
  }

  if (gesture_state == GESTURE_STATE_HOLDING) {
    int64_t held_ms = k_uptime_get() - hold_start_time;

    if (held_ms >= POWER_GESTURE_HOLD_DURATION_MS) {
      LOG_INF("Power OFF gesture complete!");
      led_driver_all_off();
      gesture_state = GESTURE_STATE_IDLE;

      /* Enter power off state */
      power_manager_enter_off_state();
      /* Does not return */
    } else {
      LOG_DBG("Hold too short (%lldms) - gesture cancelled", held_ms);
      led_driver_all_off();
      gesture_state = GESTURE_STATE_IDLE;
    }
  } else if (gesture_state == GESTURE_STATE_TAP_DETECTED) {
    /* Button released - this was the first tap, already recorded */
    LOG_DBG("First tap released");
  }
}

bool power_manager_is_gesture_pending(void) {
  return gesture_state != GESTURE_STATE_IDLE;
}
