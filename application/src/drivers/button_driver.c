/*
 * Copyright (c) 2026 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Button driver (polling only).
 *
 * Legacy hardware uses P2.10 without GPIOTE; Rev3A uses P0.02. Retain 20 Hz
 * polling and the existing debounce/gesture behavior for both boards during
 * bring-up. Interrupt-driven input and System OFF wake need hardware validation.
 */

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/button_driver.h>

LOG_MODULE_REGISTER(button_driver, LOG_LEVEL_INF);

#define BUTTON_NODE DT_ALIAS(sw0)
#ifndef CONFIG_AIRSPOT_BUTTON_LONG_PRESS_TIMEOUT_MS
#define LONG_PRESS_TIMEOUT_MS 3000 /* Default 3 seconds */
#else
#define LONG_PRESS_TIMEOUT_MS CONFIG_AIRSPOT_BUTTON_LONG_PRESS_TIMEOUT_MS
#endif

/* Poll cadence and debounce. 20 Hz (50 ms) is needed to catch quick taps: a
 * deliberate press is only ~100-300 ms, and the multi-tap gestures (double tap
 * for CO2, 5-press reset) require each press AND release edge to be sampled.
 * At the old 200 ms cadence a quick tap could fall entirely between polls and
 * a registered tap took >=400 ms — longer than the 300 ms tap classifier — so
 * multi-tap gestures could never fire. At 50 ms/poll, 2 stable polls = ~100 ms
 * of stability before a state change is accepted. */
#define BUTTON_POLL_INTERVAL_MS 50
#define BUTTON_DEBOUNCE_POLLS 2

/* Warm-up: P2.10 (shared with nRESET) can read a transient level during early
 * boot before it settles. For the first BUTTON_WARMUP_POLLS polls (~1.2 s) we
 * adopt the reading as the baseline WITHOUT emitting events, so no phantom
 * press/long-press fires at startup. */
#define BUTTON_WARMUP_POLLS 24

static const struct gpio_dt_spec button =
    GPIO_DT_SPEC_GET_OR(BUTTON_NODE, gpios, {0});
static button_callback_t user_callback = NULL;
static struct k_work_delayable long_press_work;
static struct k_work_delayable poll_work;
static bool button_pressed = false;
static bool initialized = false;
static uint8_t warmup_remaining = BUTTON_WARMUP_POLLS;

/* Dedicated workqueue for polling and event delivery. The system workqueue
 * blocks for ~600 ms during a synchronous CO2 measurement, which at 20 Hz
 * polling would swallow presses mid-gesture (e.g. the 5-press reset). Event
 * callbacks run on this queue, so they must defer blocking work elsewhere. */
#define BUTTON_WQ_STACK_SIZE 2048
#define BUTTON_WQ_PRIORITY K_PRIO_PREEMPT(8)
static K_KERNEL_STACK_DEFINE(button_wq_stack, BUTTON_WQ_STACK_SIZE);
static struct k_work_q button_wq;

static void button_long_press_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  if (!button_pressed || !user_callback) {
    return;
  }

  /* Re-confirm the button is *physically* still held before acting. P2.10 is
   * shared with nRESET and electrically noisy, so the 20 Hz poll can miss a
   * release falling-edge, leaving button_pressed stuck true. Trusting that
   * cached flag alone would fire a spurious long-press (e.g. an unwanted soft
   * power-off) after a quick tap. Reading the pin here is the safety net: if it
   * now reads released, treat it as a missed release rather than a long press. */
  int pin_state = gpio_pin_get_dt(&button);
  if (pin_state != 1) {
    LOG_WRN("Long press timer fired but button not held (pin=%d) - "
            "treating as missed release",
            pin_state);
    button_pressed = false;
    user_callback(BUTTON_EVENT_RELEASED);
    return;
  }

  LOG_INF("Button long press timer fired");
  user_callback(BUTTON_EVENT_LONG_PRESS);
}

/* Polling handler - checks button state periodically and reports edges. */
static void button_poll_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  static bool cand_state = false; /* candidate (pending) level */
  static uint8_t cand_count = 0;  /* consecutive polls at candidate level */

  if (!initialized) {
    return;
  }

  int pin_state = gpio_pin_get_dt(&button);
  if (pin_state < 0) {
    /* Error reading pin - reschedule and try again. */
    LOG_ERR("Button poll: failed to read pin: %d", pin_state);
    k_work_schedule_for_queue(&button_wq, &poll_work, K_MSEC(BUTTON_POLL_INTERVAL_MS));
    return;
  }

  /* gpio_pin_get_dt() returns the logical level: 1 == active == pressed,
   * accounting for the GPIO_ACTIVE_* flag configured in the device tree. */
  bool current_state = (pin_state == 1);

  /* Warm-up: silently adopt the startup level as the baseline so a transient
   * boot reading doesn't generate a phantom press/long-press. */
  if (warmup_remaining > 0) {
    warmup_remaining--;
    button_pressed = current_state;
    cand_state = current_state;
    cand_count = 0;
    if (warmup_remaining == 0) {
      LOG_INF("Button baseline established: %s",
              button_pressed ? "pressed" : "released");
    }
    k_work_schedule_for_queue(&button_wq, &poll_work, K_MSEC(BUTTON_POLL_INTERVAL_MS));
    return;
  }

  if (current_state != button_pressed) {
    /* Debounce: only accept the change after it persists across polls. */
    if (current_state == cand_state) {
      cand_count++;
    } else {
      cand_state = current_state;
      cand_count = 1;
    }

    if (cand_count >= BUTTON_DEBOUNCE_POLLS) {
      button_pressed = current_state;
      cand_count = 0;

      if (button_pressed) {
        LOG_INF("Button: pressed");
        if (user_callback) {
          user_callback(BUTTON_EVENT_PRESSED);
        }
        k_work_schedule_for_queue(&button_wq, &long_press_work, K_MSEC(LONG_PRESS_TIMEOUT_MS));
      } else {
        LOG_INF("Button: released");
        k_work_cancel_delayable(&long_press_work);
        if (user_callback) {
          user_callback(BUTTON_EVENT_RELEASED);
        }
      }
    }
  } else {
    /* Matches the accepted state - clear any pending candidate. */
    cand_count = 0;
  }

  /* Reschedule polling. */
  k_work_schedule_for_queue(&button_wq, &poll_work, K_MSEC(BUTTON_POLL_INTERVAL_MS));
}

int button_driver_init(void) {
  int ret;

  if (!gpio_is_ready_dt(&button)) {
    LOG_ERR("Button device not ready");
    return -ENODEV;
  }

  ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
  if (ret < 0) {
    LOG_ERR("Failed to configure button GPIO: %d", ret);
    return ret;
  }

  k_work_init_delayable(&long_press_work, button_long_press_work_handler);
  k_work_init_delayable(&poll_work, button_poll_work_handler);

  if (!initialized) { /* the workqueue thread can only be started once */
    k_work_queue_init(&button_wq);
    k_work_queue_start(&button_wq, button_wq_stack,
                       K_KERNEL_STACK_SIZEOF(button_wq_stack),
                       BUTTON_WQ_PRIORITY, NULL);
    k_thread_name_set(&button_wq.thread, "button_wq");
  }

  initialized = true;
  /* Polling starts once the callback is registered (see register_callback). */
  LOG_INF("Button driver initialized (polling %s pin %u)",
          button.port->name, button.pin);
  return 0;
}

void button_driver_register_callback(button_callback_t callback) {
  user_callback = callback;
  LOG_DBG("Button callback registered: %p", (void *)callback);

  if (initialized && callback != NULL) {
    warmup_remaining = BUTTON_WARMUP_POLLS; /* re-arm baseline warm-up */
    int ret = k_work_schedule_for_queue(&button_wq, &poll_work, K_MSEC(BUTTON_POLL_INTERVAL_MS));
    if (ret < 0) {
      LOG_ERR("Failed to schedule button polling: %d", ret);
    } else {
      LOG_INF("Button polling started (%d ms)", BUTTON_POLL_INTERVAL_MS);
    }
  }
}

int button_driver_get_state(bool *pressed) {
  if (!initialized || pressed == NULL) {
    return -ENODEV;
  }

  *pressed = button_pressed;
  return 0;
}

int button_driver_read_now(bool *pressed) {
  if (!initialized || pressed == NULL) {
    return -ENODEV;
  }

  /* Fresh read of the logical level (1 == active == pressed), honoring the
   * GPIO_ACTIVE_* flag — same source the debounce poll samples. */
  int pin_state = gpio_pin_get_dt(&button);
  if (pin_state < 0) {
    return pin_state;
  }

  *pressed = (pin_state == 1);
  return 0;
}
