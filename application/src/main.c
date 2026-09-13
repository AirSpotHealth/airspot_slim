/*
 * AirSpot Slim Firmware - Main Application
 *
 * Full-featured firmware with BLE NUS, sensor management, and data logging.
 *
 * Copyright (c) 2026 AirSpot
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>
#include <services/ota_service.h>
#include <services/health_service.h>
#include <services/diagnostics.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/dfu/mcuboot.h>

/* AirSpot includes */
#include <airspot/version.h>

/* Driver includes */
#include <drivers/battery.h>
#include <drivers/button_driver.h>
#include <drivers/led_driver.h>
#include <drivers/qi_charger.h>
#include <drivers/spi_flash.h>

/* Service includes */
#include <services/ble_mode_service.h>
#include <services/ble_service.h>
#include <services/calibration_service.h>
#include <services/software_asc.h>
#include <services/data_log.h>
#include <services/factory_test.h>
#include <services/led_indicator.h>
#include <services/power_manager.h>
#include <services/protocol.h>
#include <services/sensor_manager.h>
#include <services/user_settings.h>

LOG_MODULE_REGISTER(main, CONFIG_AIRSPOT_LOG_LEVEL);

/* Board GPIO hogs initialize flash control pins before application startup.
 * Do not duplicate these pins here: P1.13 is Qi EN2 on Rev3A.
 */
BUILD_ASSERT(IS_ENABLED(CONFIG_GPIO_HOGS), "Flash WP/HOLD require GPIO hogs");
BUILD_ASSERT(DT_NODE_HAS_PROP(DT_NODELABEL(flash_wp), output_high),
             "Flash WP must be high before SPI access");
BUILD_ASSERT(DT_NODE_HAS_PROP(DT_NODELABEL(flash_hold), output_high),
             "Flash HOLD must be high before SPI access");

/* Measurement interval (milliseconds) */
#define MEASUREMENT_INTERVAL_MS 5000

/* Data log interval - store CO2 every 5 minutes */
#define DATA_LOG_INTERVAL_MS (5 * 60 * 1000)

/* Time increment interval (1 second) */
#define TIME_INCREMENT_INTERVAL_MS 1000

/* Timers */
static struct k_timer time_timer;
static struct k_timer data_log_timer;

/* Flags */
static volatile bool data_log_due = false;

/* =============================================================================
 * Watchdog Timer - Auto-recovery from hard locks
 * =============================================================================
 */

/* WDT timeout in milliseconds
 * 5 seconds gives margin for long operations (flash erase ~100ms/sector)
 * while still providing quick recovery from actual hangs
 */
#define WDT_TIMEOUT_MS 5000

/* WDT device and channel */
static const struct device *wdt_dev;
static int wdt_channel_id = -1;
static k_tid_t watchdog_owner;

/**
 * @brief Initialize watchdog timer
 *
 * Sets up WDT with a WDT_TIMEOUT_MS (5-second) timeout. If firmware hangs and
 * doesn't call wdt_feed() within that window, the device automatically resets.
 *
 * @return 0 on success, negative errno on failure
 */
static int watchdog_init(void) {
  watchdog_owner = k_current_get();
  /* Get WDT device */
  wdt_dev = DEVICE_DT_GET_OR_NULL(DT_ALIAS(watchdog0));
  if (!wdt_dev) {
    /* Try alternate node name */
    wdt_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(wdt0));
  }
  if (!wdt_dev) {
    wdt_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(wdt));
  }

  if (!wdt_dev || !device_is_ready(wdt_dev)) {
    LOG_WRN("Watchdog device not found or not ready");
    return -ENODEV;
  }

  /* Configure WDT window */
  struct wdt_timeout_cfg wdt_config = {
      .window.min = 0,                /* No minimum wait */
      .window.max = WDT_TIMEOUT_MS,   /* 5 second timeout */
      .callback = NULL,               /* No callback, just reset */
      .flags = WDT_FLAG_RESET_SOC,    /* Reset entire SoC */
  };

  /* Install timeout configuration */
  wdt_channel_id = wdt_install_timeout(wdt_dev, &wdt_config);
  if (wdt_channel_id < 0) {
    LOG_ERR("Failed to install WDT timeout: %d", wdt_channel_id);
    return wdt_channel_id;
  }

  /* Start WDT - after this, must feed regularly! */
  int ret = wdt_setup(wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
  if (ret < 0) {
    wdt_channel_id = -1;
    LOG_ERR("Failed to start WDT: %d", ret);
    return ret;
  }

  LOG_INF("Watchdog initialized: %d ms timeout", WDT_TIMEOUT_MS);
  return 0;
}

/**
 * @brief Feed the watchdog to prevent reset
 *
 * Must be called at least once every WDT_TIMEOUT_MS (5 seconds).
 * Called from main loop and during long operations.
 */
void watchdog_feed(void) {
  /* Check expiry BEFORE renewal, including keepalives from long operations.
   * A different worker can never renew the main thread's deadline. */
  uint32_t failed = health_failed();
  if (failed) { diagnostics_watchdog(failed); return; }
  if (k_current_get() == watchdog_owner) health_progress(HEALTH_MAIN, 5000);
  if (wdt_dev && wdt_channel_id >= 0) {
    wdt_feed(wdt_dev, wdt_channel_id);
  }
}

/* Set from the sensor callback (system workqueue) when the pack drops below the
 * lockout threshold while discharging. Handled in the main loop so the actual
 * teardown + blocking lockout loop run in the main thread, not the workqueue. */
static volatile bool low_battery_lockout_requested = false;

/**
 * @brief Enter low-power low-battery lockout (does not return until reboot).
 *
 * Tears down the high-drain subsystems (BLE, sensors, logging) and parks in a
 * slow poll loop that only watches the battery/charger and drives a red LED
 * indication. Once charged back above BATTERY_RECOVER_MV, performs a clean cold
 * reboot so every driver/service re-initializes from scratch.
 *
 * @param teardown true if BLE/sensors/timers are already running and must be
 *                 stopped first; false when called from the boot gate (nothing
 *                 has been started yet).
 */
static void enter_low_battery_lockout(bool teardown) {
  LOG_WRN("LOW BATTERY LOCKOUT — charge to %d mV to resume",
          BATTERY_RECOVER_MV);

  if (teardown) {
    /* Tell the app we're entering lockout BEFORE dropping the link, so it can
     * show a "low battery — charging required" state instead of an error. */
    if (ble_service_is_connected()) {
      protocol_send_low_battery_lockout();
      k_msleep(50); /* let the notification flush before we tear down BLE */
    }
    ble_service_set_advertising_suppressed(true);
    sensor_manager_stop();
    k_timer_stop(&data_log_timer);
    k_timer_stop(&time_timer);
    if (ble_service_is_connected()) {
      ble_service_disconnect();
    }
    ble_service_stop_advertising();
  }

  bool led_charging = false;
  bool led_first = true;

  while (1) {
    ota_service_poll();
    bool charging = qi_charger_is_charging();
    int32_t mv = battery_read_voltage_mv();

    /* Update the LED only when the charge state flips, so we don't restart the
     * pattern every poll. On charger: slow red flash. Off charger: a brief red
     * fade then dark to conserve what little charge is left. */
    if (led_first || charging != led_charging) {
      if (charging) {
        led_indicator_show_charging_low_battery();
      } else {
        led_indicator_show_low_battery();
      }
      led_charging = charging;
      led_first = false;
    }

    LOG_INF("Lockout: %d mV, %s", mv, charging ? "charging" : "idle");

    /* Resume only after charging back above the recovery threshold. */
    if (charging && mv >= BATTERY_RECOVER_MV) {
      LOG_INF("Battery recovered (%d mV) — cold reboot for clean start", mv);
      led_indicator_show_color(LED_COLOR_GREEN, 500);
      k_msleep(600);
      sys_reboot(SYS_REBOOT_COLD); /* does not return */
    }

    watchdog_feed(); /* no-op until the WDT is initialized */
    /* The disconnected() callback re-schedules advertising ~100 ms after the
     * link drops, which can land after the teardown's stop — keep it off. */
    ble_service_stop_advertising();
    k_msleep(1000);
  }
}

/* Timer callbacks */
static void time_timer_handler(struct k_timer *timer) {
  ARG_UNUSED(timer);
  user_settings_inc_second();
}

static void data_log_timer_handler(struct k_timer *timer) {
  ARG_UNUSED(timer);
  data_log_due = true;
}

/* Button tap/gesture detection.
 *
 * Single vs double vs 5-press are disambiguated by COUNTING short taps and
 * deferring the action until the burst settles: each completed short tap
 * (re)arms a decision timer, and when it fires (TAP_DECISION_WINDOW_MS after
 * the last tap) we act on the final count. This stops a 5-press reset from also
 * firing the single-tap status and double-tap CO2 readout on its way to 5 (the
 * old code acted on every press immediately). Taps are counted on RELEASE and
 * only when short, so a long power-off hold never counts as a tap. */
static int tap_count = 0; /* presses in the current burst */

/* Settle time after the last RELEASE before acting on a single/double tap. The
 * window is re-armed on each release, so it measures the pause BETWEEN taps
 * (release→next-press), not press→press — a press that lingers a little can't
 * fragment a deliberate multi-press burst. */
#define TAP_DECISION_WINDOW_MS  500
#define RAPID_PRESS_RESET_COUNT 5 /* presses for reset gesture (while charging) */

static void show_device_status(void) {
  /* Device-status colors (red = low battery, blue = currently connected to a
   * phone, white = not connected), sustained 5 s then off. Keyed off the LIVE
   * BLE link state, NOT the persistent "ever paired" flag — so blue means a
   * phone is connected right now, white means it isn't. */
  led_indicator_show_status(battery_is_low(), battery_is_critical(),
                            ble_service_is_connected());
}

/* Power-off gesture state. With System OFF enabled, teardown ends in GPIO
 * wake; the fallback keeps interrupt-driven button/Qi input available. */
static volatile bool soft_off_requested = false;
static volatile bool soft_off_active = false;
static volatile bool soft_on_requested = false;

/* Power-on gesture progress (only meaningful while soft_off_active). Set on the
 * button PRESS edge while charging; the soft-off loop uses the hold duration to
 * drive the fade-in. */
static volatile bool soft_on_holding = false;
static volatile uint32_t soft_on_press_uptime = 0;

/* Soft power-off / power-on light cues. The power-on fade-in plays over the LAST
 * POWER_ON_FADE_MS of the hold so the light reaches full just as the device
 * powers on (symmetric with the fade-out). */
#define SOFT_OFF_FADE_MS 1000
#define POWER_ON_HOLD_MS CONFIG_AIRSPOT_BUTTON_LONG_PRESS_TIMEOUT_MS /* 5000 */
#define POWER_ON_FADE_MS 3000
#define POWER_ON_FADE_DELAY_MS (POWER_ON_HOLD_MS - POWER_ON_FADE_MS) /* 2000 */

/* Qi charger callback — placing the device on the charger acts like a single
 * button press: show the device status. Suppressed while soft-off so the
 * device stays dark until the power-on gesture. */
static void qi_charger_callback(bool is_charging) {
  LOG_INF("Qi charger status changed: %s",
          is_charging ? "CHARGING" : "NOT CHARGING");
  if (soft_off_active) {
    return; /* stay dark while soft-off */
  }
  if (is_charging) {
    show_device_status();
  } else {
    led_indicator_stop();
  }
}

/* Double tap → CO2 readout. Flash the LAST reading's color while a fresh
 * measurement runs, then switch to the new reading's color if it changed.
 * measure_now() is synchronous (~600 ms), so get_data() returns the new
 * value right after it.
 *
 * Runs on its OWN workqueue, not the system queue: the LED flash animation
 * (led_indicator's pattern_work) lives on the system workqueue, so blocking it
 * for the ~600 ms measurement would freeze the flash mid-cycle — the readout
 * appeared as a static color instead of flashing. A dedicated queue keeps the
 * blocking measure off the system queue so the flash animates throughout. It
 * also can't run on the button queue, which must never block or rapid presses
 * would be missed mid-gesture. */
#define CO2_READOUT_FLASH_PERIOD_MS 500
#define CO2_READOUT_DURATION_MS 5000

#define CO2_READOUT_WQ_STACK_SIZE 2048
#define CO2_READOUT_WQ_PRIORITY K_PRIO_PREEMPT(9)
static K_KERNEL_STACK_DEFINE(co2_readout_wq_stack, CO2_READOUT_WQ_STACK_SIZE);
static struct k_work_q co2_readout_wq;

static void co2_readout_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  health_progress(HEALTH_READOUT, 35000);
  sensor_data_t data;

  /* Phase 1: flash the LAST reading's color for the full readout window as
   * immediate feedback while a fresh measurement is taken. */
  led_color_t last_color = LED_COLOR_GREEN;
  if (sensor_manager_get_data(&data) == 0) {
    last_color = led_indicator_get_co2_color(
        user_settings_apply_co2_correction(data.co2_ppm));
  }
  int64_t phase1_start = k_uptime_get();
  led_indicator_flash(last_color, CO2_READOUT_FLASH_PERIOD_MS,
                      CO2_READOUT_DURATION_MS);

  /* Take the fresh measurement (~600 ms). This handler runs on its own
   * workqueue, so the blocking measure overlaps phase 1 without stalling the
   * button queue or the system queue that animates the flash. */
  sensor_manager_measure_now();

  led_color_t new_color = last_color;
  if (sensor_manager_get_data(&data) == 0) {
    uint16_t corrected = user_settings_apply_co2_correction(data.co2_ppm);
    new_color = led_indicator_get_co2_color(corrected);
    LOG_INF("Double tap: new reading CO2 %u ppm", corrected);
  }

  /* Let phase 1 (blinking the last reading) finish its full window before
   * switching, so the user sees the last reading blink for the whole 5 s and
   * then the fresh reading SOLID for the whole 5 s (the spec's 10 s
   * double-press budget). Phase 2 always runs — even when the level is
   * unchanged the user expects the fresh result to display. */
  int64_t elapsed = k_uptime_get() - phase1_start;
  if (elapsed < CO2_READOUT_DURATION_MS) {
    k_msleep(CO2_READOUT_DURATION_MS - elapsed);
  }

  /* Phase 2: fresh reading shown SOLID (steady, not blinking) for 5 s. */
  led_indicator_show_color(new_color, CO2_READOUT_DURATION_MS);
  health_progress(HEALTH_READOUT, 0);
}

static K_WORK_DEFINE(co2_readout_work, co2_readout_work_handler);

/* Fires TAP_DECISION_WINDOW_MS after the last short tap, once the burst has
 * settled, and acts on the final tap count. Runs on the system workqueue; both
 * actions are non-blocking (they hand off to led_indicator / co2_readout_wq). */
static void tap_decision_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  int count = tap_count;
  tap_count = 0;

  if (count == 1) {
    /* Single tap → device status (blue/red/white per UX spec). */
    show_device_status();
  } else if (count == 2) {
    /* Double tap → CO2 readout (own queue: the measurement blocks ~600 ms). */
    k_work_submit_to_queue(&co2_readout_wq, &co2_readout_work);
  } else {
    /* 3+ presses that didn't reach the 5-press reset (e.g. not charging, or an
     * aborted burst) → no gesture. */
    LOG_DBG("Tap burst of %d ignored (no matching gesture)", count);
  }
}

static K_WORK_DELAYABLE_DEFINE(tap_decision_work, tap_decision_work_handler);

/* Button event handler */
static void button_event_handler(button_event_t event) {
  if (calibration_service_is_active()) {
    LOG_INF("Calibration active - ignoring button event");
    return;
  }

  /* Software fallback: ignore everything except the power-on gesture (hold 5 s while on
   * the Qi charger). Track the press/release so the soft-off loop can fade the
   * light in over the hold; the loop performs the actual reboot. LONG_PRESS is a
   * fallback that still honors a completed hold if the loop's fade didn't run
   * (e.g. a just-came-on-charger race). */
  if (soft_off_active) {
    switch (event) {
    case BUTTON_EVENT_PRESSED:
      if (qi_charger_is_charging()) {
        soft_on_press_uptime = k_uptime_get_32();
        soft_on_holding = true;
      }
      break;
    case BUTTON_EVENT_RELEASED:
      soft_on_holding = false;
      break;
    case BUTTON_EVENT_LONG_PRESS:
      if (qi_charger_is_charging()) {
        LOG_INF("Power ON gesture (5s hold on charger)");
        soft_on_requested = true;
      }
      break;
    }
    return;
  }

  switch (event) {
  case BUTTON_EVENT_PRESSED:
    /* Count every press edge (hold duration is irrelevant — a press that
     * lingers still counts). A long hold also lands here as a single count, but
     * it never schedules a tap action: the decision timer is armed on RELEASE,
     * and a hold ends in BUTTON_EVENT_LONG_PRESS which clears the count. */
    tap_count++;

    /* 5 quick presses while on the charger → factory reset, fired immediately
     * on the 5th press (don't wait for the decision window). */
    if (tap_count >= RAPID_PRESS_RESET_COUNT && qi_charger_is_charging()) {
      LOG_INF("5-press reset while charging");
      k_work_cancel_delayable(&tap_decision_work);
      tap_count = 0;
      led_indicator_show_color(LED_COLOR_WHITE, 300);
      k_msleep(300);
      sys_reboot(SYS_REBOOT_COLD);
      return;
    }
    break;

  case BUTTON_EVENT_RELEASED:
    /* A press completed — (re)arm the single/double decision. Re-arming on
     * release means the window measures the pause between taps, so the burst
     * isn't fragmented by a press that lingers (the old bug). */
    if (tap_count > 0) {
      k_work_reschedule(&tap_decision_work, K_MSEC(TAP_DECISION_WINDOW_MS));
    }
    break;

  case BUTTON_EVENT_LONG_PRESS:
    /* A hold is not a tap: drop any pending tap decision and reset the count
     * so a stray status flash can't appear mid power-off gesture. */
    k_work_cancel_delayable(&tap_decision_work);
    tap_count = 0;
    /* Hold 5 s while on the Qi charger → soft power off (charger requirement
     * prevents accidental pocket activation). Off the charger it stays a
     * no-op: the charger gate is part of the existing gesture contract. */
    if (qi_charger_is_charging()) {
      LOG_INF("Power OFF gesture (5s hold on charger)");
      soft_off_requested = true;
    } else {
      LOG_DBG("Long press off charger - no action");
    }
    break;
  }
}

/* Sensor data callback - called when new measurement is available */
static void sensor_data_callback(const sensor_data_t *data) {
  if (!data) {
    return;
  }

  ota_service_observe_sample(data);

  /* Low-battery lockout: once the pack drops below the lockout threshold
   * (battery_low == <BATTERY_LOCKOUT_MV) while NOT on the charger, request the
   * lockout. The main loop performs the teardown + blocking lockout loop so it
   * runs in the main thread rather than blocking this workqueue callback. */
  if (data->battery_low && !data->is_charging) {
    LOG_WRN("Battery below lockout threshold — requesting low-power lockout");
    low_battery_lockout_requested = true;
  } else if (data->battery_critical && !data->is_charging &&
             !led_indicator_is_active()) {
    /* Belt-and-suspenders: if we somehow get here still draining, flag it. */
    LOG_WRN("BATTERY CRITICAL!");
    low_battery_lockout_requested = true;
  }

  if (!data->valid) {
    if (ble_service_is_connected()) protocol_send_realtime_co2();
    return;
  }

  /* Calibration must never accumulate a cached failed sensor reading. */
  software_asc_process_measurement(data->co2_ppm);
  calibration_service_process_measurement(data->co2_ppm);

  uint16_t corrected_co2 = user_settings_apply_co2_correction(data->co2_ppm);

  LOG_INF("CO2=%uppm T=%.1f°C H=%.0f%% P=%.0fhPa Batt=%u%%%s", corrected_co2,
          (double)data->temperature_c, (double)data->humidity_rh,
          (double)data->pressure_hpa, data->battery_percent,
          data->is_charging ? " [CHG]" : "");

  /* Send CO2 data over BLE if connected */
  if (ble_service_is_connected()) {
    protocol_send_realtime_co2();
    LOG_DBG("Sent CO2 data over BLE");
  }

  /* Update battery service */
  /* Note: BAS is automatically updated via CONFIG_BT_BAS */


}

/**
 * @brief Enter soft power-off (does not return until reboot).
 *
 * Fades the LED out, tears down BLE/sensors/timers and parks in a low-rate
 * loop with only the button poll and Qi detection alive. The power-on gesture
 * (hold 5 s while on the Qi charger, detected by button_event_handler) sets
 * soft_on_requested; we then cold-reboot so every driver/service starts
 * fresh — the boot LED sequence doubles as the "light comes on" cue.
 */
/* Polled once per frame by led_indicator_ramp() during the power-on fade-in:
 * keep fading only while the button is STILL physically held and on the charger.
 * Uses a live GPIO read (button_driver_read_now) because the driver's cached
 * state can't refresh mid-ramp — the ramp busy-waits on the main thread, which
 * outranks the button workqueue. Also feeds the watchdog, since the ramp blocks
 * for the full POWER_ON_FADE_MS. */
static bool power_on_hold_active(void) {
  watchdog_feed();
  bool pressed = false;
  if (button_driver_read_now(&pressed) != 0) {
    return false;
  }
  return pressed && qi_charger_is_charging();
}

/* Called only from main, after shutdown, or on a rejected System OFF wake. */
static void enter_system_off(bool peripherals_started)
{
  const struct gpio_dt_spec wake_button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
  const struct gpio_dt_spec qi = GPIO_DT_SPEC_GET(DT_NODELABEL(qi_chg_status), gpios);
  bool pressed = true;
  LOG_INF("System OFF: waiting for button release before arming P0.02");
  /* The off gesture ends while held. Sleeping immediately on an active SENSE
   * would wake immediately; wait on edges and only wake periodically to feed WDT. */
  for (;;) {
    watchdog_feed();
    int rc = button_driver_read_now(&pressed);
    if (rc) { LOG_ERR("Cannot read wake pin: %d", rc); sys_reboot(SYS_REBOOT_COLD); }
    if (!pressed) {
      k_msleep(20); /* Require a released debounce window before arming. */
      if (button_driver_read_now(&pressed) == 0 && !pressed) { break; }
    }
    button_driver_wait_for_change(K_SECONDS(1));
  }
  if (peripherals_started) {
    int rc = spi_flash_power_down();
    if (rc) { LOG_WRN("Flash sleep failed: %d", rc); }
  }
  led_driver_all_off();
  int rc = gpio_pin_interrupt_configure_dt(&qi, GPIO_INT_DISABLE);
  if (rc) { LOG_ERR("Cannot disable Qi interrupt: %d", rc); sys_reboot(SYS_REBOOT_COLD); }
  rc = button_driver_prepare_wakeup();
  if (rc) { LOG_ERR("Cannot prepare wake button: %d", rc); sys_reboot(SYS_REBOOT_COLD); }
  LOG_INF("Entering System OFF; wake source=P0.02 active high");
  /* No ISR may service/clear the asserted level between wake arming and OFF.
   * A press in that final window must wake us, not be lost. */
  unsigned int key = irq_lock();
  rc = gpio_pin_interrupt_configure_dt(&wake_button, GPIO_INT_LEVEL_ACTIVE);
  if (rc) {
    irq_unlock(key);
    LOG_ERR("Cannot arm System OFF wake: %d", rc);
    sys_reboot(SYS_REBOOT_COLD);
  }
  sys_poweroff();
}

/* System OFF resumes through reset/MCUboot. Qualify the existing charger + hold
 * gesture before starting BLE or sensors, so a tap cannot turn the product on. */
static void validate_system_off_wake(uint32_t hold_start)
{
  soft_off_active = true;
  LOG_INF("GPIO wake: validating charger and five-second button hold");
  /* Qi starts with a false cached value until its asynchronous debounce runs. */
  if (qi_charger_wait_ready(K_SECONDS(1)) != 0) {
    LOG_ERR("GPIO wake: charger initial read unavailable");
    enter_system_off(false);
  }
  while (power_on_hold_active()) {
    uint32_t elapsed = k_uptime_get_32() - hold_start;
    if (elapsed >= POWER_ON_FADE_DELAY_MS) {
      uint32_t remaining = elapsed < POWER_ON_HOLD_MS ? POWER_ON_HOLD_MS - elapsed : 0;
      if (led_indicator_ramp(LED_COLOR_WHITE, 0, 100, remaining, power_on_hold_active)) {
        LOG_INF("GPIO wake gesture accepted");
        soft_off_active = false;
        return;
      }
      break;
    }
    k_msleep(20); /* Only during an active wake gesture, never while off/idle. */
  }
  LOG_INF("GPIO wake gesture rejected; returning to System OFF");
  enter_system_off(false);
}

static void enter_soft_off(void) {
  LOG_INF("Entering soft power-off");
  soft_off_active = true;

  /* Real fade-out (spec: "light fades off when turning off"). Software-PWM ramp
   * — the gpio-leds can't dim otherwise. Feed the watchdog first: the ramp
   * busy-waits for ~1 s and teardown follows, all under the 5 s WDT window. */
  watchdog_feed();
  led_indicator_ramp(LED_COLOR_WHITE, 100, 0, SOFT_OFF_FADE_MS, NULL);
  led_driver_all_off();

  /* Tear down the high-drain subsystems (same set as the battery lockout). */
  sensor_manager_stop();
  k_timer_stop(&data_log_timer);
  k_timer_stop(&time_timer);
  /* Block advertising BEFORE disconnecting: otherwise the disconnect callback
   * re-advertises within ~100 ms and the phone reconnects in the race window
   * (observed: link back up 13 ms after our disconnect). With advertising
   * suppressed the disconnect is final until the power-on cold reboot. */
  ble_service_set_advertising_suppressed(true);
  if (ble_service_is_connected()) {
    ble_service_disconnect();
  }

  if (IS_ENABLED(CONFIG_AIRSPOT_POWER_OFF_ENABLED)) {
    enter_system_off(true);
  }

  /* Park until the power-on gesture completes. While the button is held on the
   * charger, once the hold passes POWER_ON_FADE_DELAY_MS we fade the white light
   * IN over the remaining hold as live feedback; completing the fade == the full
   * hold, so we power on. Releasing (or leaving the charger) mid-fade aborts it
   * and requires a fresh press — same 5 s commitment as before, now visible. */
  while (!soft_on_requested) {
    ota_service_poll();
    watchdog_feed();

    if (soft_on_holding && qi_charger_is_charging()) {
      uint32_t held = k_uptime_get_32() - soft_on_press_uptime;
      if (held >= POWER_ON_FADE_DELAY_MS) {
        if (led_indicator_ramp(LED_COLOR_WHITE, 0, 100, POWER_ON_FADE_MS,
                               power_on_hold_active)) {
          soft_on_requested = true; /* held the full duration → power on */
        } else {
          led_driver_all_off();
          soft_on_holding = false; /* aborted → require a fresh press */
        }
        continue;
      }
    }

    k_msleep(50);
  }

  LOG_INF("Power ON — cold reboot for clean start");
  sys_reboot(SYS_REBOOT_COLD); /* does not return */
}

/* Store CO2 data to flash */
static void store_co2_data(void) {
  sensor_data_t data;

  if (sensor_manager_get_data(&data) != 0 || !data.valid) {
    LOG_WRN("No valid sensor data for logging");
    return;
  }

  /* Get timestamp - use synced time if available, otherwise uptime */
  uint32_t timestamp;
  if (user_settings_is_time_set()) {
    timestamp = user_settings_get_time_now();
  } else {
    timestamp = k_uptime_get_32() / 1000;
  }

  /* Store record */
  uint16_t corrected = user_settings_apply_co2_correction(data.co2_ppm);
  int ret = data_log_store_record(timestamp, corrected, RECORD_TYPE_CO2);
  if (ret != 0) {
    LOG_ERR("Failed to store CO2 record: %d", ret);
  } else {
    LOG_DBG("Stored CO2: %u ppm at t=%u", data.co2_ppm, timestamp);
  }
}

/**
 * First-run indicator on P2.09 / P2.08 / P2.06 (see overlay + PIN_ASSIGNMENTS.md).
 */
static void boot_early_led_pulse(void) {
  const struct device *gpio2 = DEVICE_DT_GET(DT_NODELABEL(gpio2));

  for (int i = 0; i < 200; i++) {
    if (device_is_ready(gpio2)) {
      break;
    }
    k_msleep(10);
  }

  if (!device_is_ready(gpio2)) {
    return;
  }

  static const uint8_t led_pins[] = {9, 8, 6};
  for (size_t i = 0; i < ARRAY_SIZE(led_pins); i++) {
    gpio_pin_configure(gpio2, led_pins[i], GPIO_OUTPUT_INACTIVE);
  }

  for (int round = 0; round < 2; round++) {
    for (size_t i = 0; i < ARRAY_SIZE(led_pins); i++) {
      gpio_pin_set(gpio2, led_pins[i], 1);
      k_msleep(80);
      gpio_pin_set(gpio2, led_pins[i], 0);
      k_msleep(80);
    }
  }
}

static void boot_wait_for_console(void) {
#if defined(CONFIG_UART_CONSOLE)
  const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

  for (int i = 0; i < 100; i++) {
    if (device_is_ready(console)) {
      return;
    }
    k_msleep(10);
  }
#endif
}

/* First failing subsystem + errno are retained; later failures stay in RTT. */
static bool startup_check(uint16_t subsystem, int rc) {
  if (rc) diagnostics_record(DIAG_STARTUP_FAILED,
      ((uint32_t)subsystem << 16) | ((uint32_t)(rc < 0 ? -rc : rc) & 0xffff));
  return rc == 0;
}

int main(void) {
  int ret;
  bool startup_healthy = true;
  ota_service_init();
  ret = watchdog_init();
  startup_healthy &= startup_check(1, ret);
  if (ret) LOG_ERR("Watchdog unavailable: %d; trial confirmation disabled", ret);
  uint32_t wake_hold_start = k_uptime_get_32();
  uint32_t initial_reset_cause = 0;
  hwinfo_get_reset_cause(&initial_reset_cause);
  bool waking_from_off = IS_ENABLED(CONFIG_AIRSPOT_POWER_OFF_ENABLED) &&
                        (initial_reset_cause & RESET_LOW_POWER_WAKE) &&
                        boot_is_img_confirmed();

  if (!waking_from_off) { boot_early_led_pulse(); }
  boot_wait_for_console();

  /* Boot banner */
  printk("\n\n");
  printk("========================================\n");
  printk("AirSpot Slim Firmware v%s\n", AIRSPOT_FIRMWARE_VERSION_STRING);
#if defined(CONFIG_UART_CONSOLE)
  printk("Logs: UART P1.02 TX @ 115200 (airspot logs uart)\n");
#elif defined(CONFIG_RTT_CONSOLE)
  printk("Logs: SEGGER RTT over SWD\n");
#endif
  printk("========================================\n");

  LOG_INF("========================================");
  LOG_INF("AirSpot Slim Firmware v%s", AIRSPOT_FIRMWARE_VERSION_STRING);
  LOG_INF("========================================");

  /* Log why we just booted. A BROWNOUT here points at a power-rail sag (e.g.
   * Qi-charger inrush or the white charging LED current spike); WATCHDOG points
   * at a stall; POR/PIN are normal power-on/reset. Cleared so each boot is
   * fresh. */
  {
    uint32_t reset_cause = 0;
    if (hwinfo_get_reset_cause(&reset_cause) == 0) {
      LOG_INF("Reset cause: 0x%08X%s%s%s%s%s%s", reset_cause,
              (reset_cause & RESET_POR) ? " POR" : "",
              (reset_cause & RESET_BROWNOUT) ? " BROWNOUT" : "",
              (reset_cause & RESET_WATCHDOG) ? " WATCHDOG" : "",
              (reset_cause & RESET_SOFTWARE) ? " SOFTWARE" : "",
              (reset_cause & RESET_PIN) ? " PIN" : "",
              (reset_cause & RESET_LOW_POWER_WAKE) ? " LOWPOWER_WAKE" : "");
      hwinfo_clear_reset_cause();
    }
  }

  /*
   * ========================================
   * Driver Initialization
   * ========================================
   */

  /* Initialize LED driver */
  ret = led_driver_init();
  startup_healthy &= startup_check(2, ret);
  if (ret) {
    LOG_ERR("LED driver init failed: %d", ret);
  } else {
    LOG_INF("LED driver initialized");
#ifdef CONFIG_AIRSPOT_BOOT_LED_SELFTEST
    /* Boot LED color sweep — factory-test builds only. Normal devices boot
     * silently (UX rule: never light up without a reason). */
    led_driver_startup_sequence();
#endif
  }

  /* Initialize button driver */
  ret = button_driver_init();
  startup_healthy &= startup_check(3, ret);
  if (ret) {
    LOG_ERR("Button driver init failed: %d", ret);
  } else {
    LOG_INF("Button driver initialized");
    /* Register gestures after the wake gate; a held wake button is baseline. */
  }

  /* Initialize Battery driver */
  ret = battery_init();
  startup_healthy &= startup_check(4, ret);
  if (ret) {
    LOG_ERR("Battery init failed: %d", ret);
  } else {
    LOG_INF("Battery driver initialized");
  }

  /* Initialize Qi Charger driver */
  ret = qi_charger_init();
  startup_healthy &= startup_check(5, ret);
  if (ret) {
    LOG_ERR("Qi charger init failed: %d", ret);
  } else {
    LOG_INF("Qi charger driver initialized");
  }

  /* Initialize LED indicator service */
  ret = led_indicator_init();
  startup_healthy &= startup_check(6, ret);
  if (ret) {
    LOG_ERR("LED indicator init failed: %d", ret);
  } else {
    LOG_INF("LED indicator service initialized");
  }

  if (waking_from_off) {
    validate_system_off_wake(wake_hold_start);
  }
  button_driver_register_callback(button_event_handler);

  /* Dedicated workqueue for the double-tap CO2 readout so its blocking ~600 ms
   * measurement never stalls the system workqueue (LED flash animation). */
  k_work_queue_init(&co2_readout_wq);
  k_work_queue_start(&co2_readout_wq, co2_readout_wq_stack,
                     K_KERNEL_STACK_SIZEOF(co2_readout_wq_stack),
                     CO2_READOUT_WQ_PRIORITY, NULL);
  k_thread_name_set(&co2_readout_wq.thread, "co2_readout_wq");

  /*
   * ========================================
   * Boot-time Low-Battery Gate
   * ========================================
   *
   * If the pack is already below the lockout threshold, do NOT bring up BLE or
   * the sensors — that current draw is exactly what browns out a near-empty
   * cell and causes the boot→advertise→reset death-loop. Park in the lockout
   * loop (LED + charge watch) until charged above the recovery threshold, then
   * cold-reboot into a normal start. Battery/charger/LED drivers are already up.
   */
  {
    /* Two reads to let the EMA filter settle after a cold boot. */
    (void)battery_read_voltage_mv();
    int32_t boot_mv = battery_read_voltage_mv();
    LOG_INF("Boot battery: %d mV (lockout <%d, recover >=%d)", boot_mv,
            BATTERY_LOCKOUT_MV, BATTERY_RECOVER_MV);
    if (boot_mv > 0 && boot_mv < BATTERY_LOCKOUT_MV) {
      enter_low_battery_lockout(false); /* does not return until reboot */
    }
  }

  /*
   * ========================================
   * Service Initialization (order matters!)
   * ========================================
   */

  /* 1. User settings (loads from NVS) */
  ret = user_settings_init();
  startup_healthy &= startup_check(7, ret);
  if (ret) {
    LOG_WRN("User settings init failed: %d (using defaults)", ret);
  } else {
    LOG_INF("User settings initialized");
    LOG_INF("CO2 calibration correction loaded: %d ppm",
            user_settings_get_co2_correction());
  }

  diagnostics_init(initial_reset_cause);

  /* Initialize Factory Test Service */
  factory_test_init();

  /* Initialize Power Manager */
  ret = power_manager_init();
  startup_healthy &= startup_check(8, ret);
  if (ret) {
    LOG_WRN("Power manager init failed: %d", ret);
  } else {
    LOG_INF("Power manager initialized");
  }

  /* 2. BLE service (enables Bluetooth stack) */
  ret = ble_service_init();
  startup_healthy &= startup_check(9, ret);
  if (ret) {
    LOG_ERR("BLE service init failed: %d", ret);
  } else {
    LOG_INF("BLE service initialized");
  }

  /* 3. Protocol handler (initializes NUS) */
  software_asc_init();
  ret = protocol_init();
  startup_healthy &= startup_check(10, ret);
  if (ret) {
    LOG_ERR("Protocol init failed: %d", ret);
  } else {
    LOG_INF("Protocol handler initialized");
  }

  /* The watchdog already covers startup. Conditioning keepalives are bounded
   * by the main owner and cannot hide another worker's expired deadline. */
  /* 4. Sensor manager (initializes all sensors) */
  ret = sensor_manager_init();
  sensor_manager_register_callback(sensor_data_callback);
  startup_healthy &= startup_check(11, ret);
  if (ret) {
    LOG_ERR("Sensor manager init failed: %d", ret);
  } else {
    LOG_INF("Sensor manager initialized");
  }

  /* Register Qi charger callback for LED feedback (debounced in qi_charger.c) */
  qi_charger_register_callback(qi_charger_callback);

  /* 5. Data log (requires SPI flash) */
  ret = data_log_init();
  startup_healthy &= startup_check(12, ret);
  if (ret) {
    LOG_WRN("Data log init failed: %d (history disabled)", ret);
  } else {
    LOG_INF("Data log initialized");
  }

  /* 6. BLE mode service (starts advertising) */
  ret = ble_mode_init();
  startup_healthy &= startup_check(13, ret);
  if (ret) {
    LOG_ERR("BLE mode init failed: %d", ret);
    /* Try starting advertising directly */
    ble_service_start_advertising();
  } else {
    LOG_INF("BLE mode service initialized");
  }

  /* No persistent LED at boot — startup sequence already ran */

  /*
   * ========================================
   * Start Timers
   * ========================================
   */

  /* Time increment timer (1 second) */
  k_timer_init(&time_timer, time_timer_handler, NULL);
  k_timer_start(&time_timer, K_MSEC(TIME_INCREMENT_INTERVAL_MS),
                K_MSEC(TIME_INCREMENT_INTERVAL_MS));

  /* Data log timer (5 minutes) */
  k_timer_init(&data_log_timer, data_log_timer_handler, NULL);
  k_timer_start(&data_log_timer, K_MSEC(DATA_LOG_INTERVAL_MS),
                K_MSEC(DATA_LOG_INTERVAL_MS));

  /*
   * ========================================
   * Start Sensor Measurements
   * ========================================
   */

  /* Get measurement interval from power mode (matches old firmware):
   * 0 = On Demand - manual only (button press or BLE request)
   * 1 = Low (3 min) - battery saving
   * 2 = Mid (1 min) - balanced
   * 3 = High (5 sec) - real-time
   */
  uint8_t power_mode = user_settings_get_power_mode();
  uint32_t interval_ms;
  switch (power_mode) {
  case 0: /* On demand - no automatic measurements */
    interval_ms = 0;
    break;
  case 1:                 /* Low power */
    interval_ms = 180000; /* 3 minutes */
    break;
  case 2:                /* Mid */
    interval_ms = 60000; /* 1 minute */
    break;
  case 3:               /* High */
    interval_ms = 5000; /* 5 seconds */
    break;
  default:
    interval_ms = 180000; /* Default to low power */
    break;
  }

  if (interval_ms == 0) {
    LOG_INF("Sensor mode: ON DEMAND (manual measurements only)");
    /* Do one initial measurement */
    ret = sensor_manager_measure_now();
    startup_healthy &= startup_check(14, ret);
  } else {
    ret = sensor_manager_start(interval_ms);
    startup_healthy &= startup_check(15, ret);
    if (ret) {
      LOG_ERR("Failed to start sensor measurements: %d", ret);
    } else {
      LOG_INF("Sensor measurements started (interval: %u ms)", interval_ms);
    }
  }

  /* The interval alone does not select STCC4 continuous operation. Restore
   * the saved hardware mode too, including when its interval is already 5 s. */
  ret = sensor_manager_set_power_mode(power_mode);
  startup_healthy &= startup_check(16, ret);

  /*
   * ========================================
   * Initialization Complete
   * ========================================
   */

  LOG_INF("========================================");
  LOG_INF("System initialized %s", startup_healthy ? "successfully" : "with errors");
  LOG_INF("BLE advertising as: %s", ble_service_get_device_name());
  LOG_INF("Watchdog: %s", (wdt_channel_id >= 0) ? "ENABLED (5s)" : "DISABLED");
  LOG_INF("Button: 1x status, 2x CO2 readout; on charger: hold 5s power "
          "on/off, 5x reset");
  LOG_INF("========================================");


  /*
   * ========================================
   * Main Loop
   * ========================================
   */

  health_start();
  ota_service_startup_complete(startup_healthy);

  while (1) {
    ota_service_poll();
    software_asc_tick();
    /* Feed watchdog first - must happen at least every WDT_TIMEOUT_MS (5s) */
    watchdog_feed();

    /* Low-battery lockout requested by the sensor callback: tear down the
     * high-drain subsystems and park here until charged (does not return). */
    if (low_battery_lockout_requested) {
      low_battery_lockout_requested = false;
      enter_low_battery_lockout(true);
    }

    /* Soft power-off requested by the button gesture (5 s hold on charger):
     * runs in the main thread, does not return until the power-on reboot. */
    if (boot_is_img_confirmed() &&
        (soft_off_requested || power_manager_take_off_request())) {
      soft_off_requested = false;
      enter_soft_off();
    }

    /* No idle/resting LED: per the UX light rule the device never stays
     * glowing — the LED only animates for explicit, timed indications. */

    /* Process any pending history requests - non-blocking check */
    protocol_process_history();

    /* Store CO2 data if due */
    if (data_log_due) {
      data_log_due = false;
      store_co2_data();
    }

    /* Non-blocking: just yield to let other threads run
     *
     * All work is event-driven:
     * - BLE events: handled by BLE stack threads
     * - Sensor measurements: handled by sensor_manager work queue
     * - Button press: handled by interrupt callback
     * - History requests: signaled via semaphore, processed here
     *
     * We use a short wait on the semaphore to:
     * - Process history requests immediately when they arrive
     * - Yield CPU when idle (power savings)
     * - Feed WDT regularly (100ms << 5s timeout)
     */
    if (data_log_wait_for_request(K_MSEC(100)) == 0) {
      /* Semaphore signaled - history request pending, loop immediately */
      continue;
    }
    /* Timeout - no request, that's fine, just loop again */
  }

  return 0;
}
