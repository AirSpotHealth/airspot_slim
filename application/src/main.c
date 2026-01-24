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
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

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
#include <services/data_log.h>
#include <services/factory_test.h>
#include <services/led_indicator.h>
#include <services/power_manager.h>
#include <services/protocol.h>
#include <services/sensor_manager.h>
#include <services/user_settings.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Flash control pins - must be configured before SPI NOR driver init */
#define FLASH_WP_PIN                                                           \
  13 /* P1.13 - Write Protect (active low, HIGH = not protected) */
#define FLASH_HOLD_PIN 11 /* P1.11 - Hold (active low, HIGH = not held) */

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

/**
 * @brief Initialize watchdog timer
 *
 * Sets up WDT with 2-second timeout. If firmware hangs and doesn't
 * call wdt_feed() within 2 seconds, device will automatically reset.
 *
 * @return 0 on success, negative errno on failure
 */
static int watchdog_init(void) {
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
      .window.max = WDT_TIMEOUT_MS,   /* 2 second timeout */
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
    LOG_ERR("Failed to start WDT: %d", ret);
    return ret;
  }

  LOG_INF("Watchdog initialized: %d ms timeout", WDT_TIMEOUT_MS);
  return 0;
}

/**
 * @brief Feed the watchdog to prevent reset
 *
 * Must be called at least once every WDT_TIMEOUT_MS (2 seconds).
 * Called from main loop and during long operations.
 */
void watchdog_feed(void) {
  if (wdt_dev && wdt_channel_id >= 0) {
    wdt_feed(wdt_dev, wdt_channel_id);
  }
}

/* Early initialization - runs before device drivers (PRE_KERNEL_1) */
static int flash_pins_early_init(void) {
  const struct device *gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
  if (!device_is_ready(gpio1)) {
    return 0;
  }

  /* Configure WP and HOLD pins HIGH */
  gpio_pin_configure(gpio1, FLASH_WP_PIN, GPIO_OUTPUT_HIGH);
  gpio_pin_configure(gpio1, FLASH_HOLD_PIN, GPIO_OUTPUT_HIGH);

  return 0;
}

SYS_INIT(flash_pins_early_init, PRE_KERNEL_1, 0);

/* Timer callbacks */
static void time_timer_handler(struct k_timer *timer) {
  ARG_UNUSED(timer);
  user_settings_inc_second();
}

static void data_log_timer_handler(struct k_timer *timer) {
  ARG_UNUSED(timer);
  data_log_due = true;
}

/* Track button state for tap detection */
static int64_t last_press_time = 0;
static bool tap_completed = false;

/* Qi charger callback - handle LED feedback on charger placement */
static void qi_charger_callback(bool is_charging) {
  LOG_INF("Qi charger status changed: %s",
          is_charging ? "CHARGING" : "NOT CHARGING");

  if (is_charging) {
    if (battery_is_low() || battery_is_critical()) {
      /* Low battery while charging - flashing red */
      led_indicator_show_charging_low_battery();
    } else {
      /* Normal battery - show white then CO2 color */
      sensor_data_t data;
      sensor_manager_get_data(&data);
      /* Device is "on" if not in on-demand mode (power_mode != 0) */
      bool device_on = (user_settings_get_power_mode() != 0);
      led_indicator_show_charging_normal(device_on, data.co2_ppm);
    }
  } else {
    /* Charger removed - stop charging LED pattern */
    led_indicator_stop();
  }
}

/* Button event handler with power gesture and LED indicator support */
static void button_event_handler(button_event_t event) {
  int64_t now = k_uptime_get();

  switch (event) {
  case BUTTON_EVENT_PRESSED:
    LOG_INF("Button pressed");

    /* Check if this is a second press after a tap (for power-off gesture) */
    if (tap_completed && (now - last_press_time) <= POWER_GESTURE_WINDOW_MS) {
      LOG_INF("Second press detected - checking for power-off hold");
      power_manager_on_button_pressed();
    } else {
      if (calibration_service_is_active()) {
        LOG_INF("Calibration active - skipping manual measurement");
        break;
      }
      /* Check battery state first */
      if (battery_is_low() || battery_is_critical()) {
        /* Low battery mode - show fading red for 3 seconds */
        LOG_INF("Low battery - showing fading red indicator");
        led_indicator_show_low_battery();
      } else if (ble_service_is_connected()) {
        /* Connected - show white then CO2 color */
        sensor_data_t data;
        sensor_manager_get_data(&data);
        led_indicator_show_connected(data.co2_ppm);
        sensor_manager_measure_now();
      } else {
        /* Normal press - trigger measurement with simple LED feedback */
        led_driver_set(LED_GREEN, true);
        sensor_manager_measure_now();
      }
    }

    last_press_time = now;
    tap_completed = false;
    break;

  case BUTTON_EVENT_RELEASED:
    LOG_INF("Button released");

    /* Turn off simple LED if we used it (not using LED indicator) */
    if (!led_indicator_is_active()) {
      led_driver_set(LED_GREEN, false);
    }

    /* Check if this was a short tap (< 300ms) */
    if ((now - last_press_time) <= POWER_GESTURE_TAP_MAX_MS) {
      LOG_INF("Tap detected! Now press-and-hold 3s within 500ms to power off");
      tap_completed = true;
      power_manager_on_tap_detected();
    } else {
      LOG_INF("Button held too long (%lld ms) - not a tap",
              (now - last_press_time));
    }

    /* Notify power manager of button release */
    if (power_manager_is_gesture_pending()) {
      power_manager_on_button_released();
    }
    break;

  case BUTTON_EVENT_LONG_PRESS:
    /* Only toggle LED if not in power gesture */
    if (!power_manager_is_gesture_pending()) {
      LOG_INF("Button long press - toggling LED 2");
      led_driver_toggle(LED_BLUE);
    }
    break;
  }
}

/* Sensor data callback - called when new measurement is available */
static void sensor_data_callback(const sensor_data_t *data) {
  if (!data || !data->valid) {
    return;
  }

  /* Feed calibration accumulator with raw value */
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

  /* Check battery warnings */
  if (data->battery_critical) {
    LOG_WRN("BATTERY CRITICAL!");
    /* Flash red LED */
    led_driver_set(0, true);
    k_msleep(100);
    led_driver_set(0, false);
  } else if (data->battery_low) {
    LOG_WRN("Battery low");
  }
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

int main(void) {
  int ret;

  /*
   * ========================================
   * Power-on Validation (must be first!)
   * ========================================
   *
   * If device woke from System OFF, validate the wake-up gesture
   * (tap + tap-hold 3s). Invalid gesture → return to Power OFF.
   *
   * NOTE: Currently DISABLED because button is on P2.10 which doesn't
   * support GPIO SENSE/interrupt for System OFF wake. Once hardware
   * moves button to P0 or P1, uncomment this validation.
   */
#if 0 /* TODO: Enable once button is on P0/P1 */
  if (power_manager_is_wakeup_from_off()) {
    LOG_INF("Woke from Power Off - validating gesture...");

    /* Need LED driver for feedback */
    led_driver_init();

    if (!power_manager_validate_wakeup_gesture()) {
      LOG_INF("Invalid wake gesture - returning to Power Off");
      power_manager_enter_off_state();
      /* Does not return */
    }

    LOG_INF("Wake gesture valid - continuing boot");
  }
#endif

  /* Boot banner */
  printk("\n\n");
  printk("========================================\n");
  printk("AirSpot Slim Firmware v%s\n", AIRSPOT_FIRMWARE_VERSION_STRING);
  printk("Phase 10: Power Off Mode (Shipping)\n");
  printk("========================================\n");

  LOG_INF("========================================");
  LOG_INF("AirSpot Slim Firmware v%s", AIRSPOT_FIRMWARE_VERSION_STRING);
  LOG_INF("Phase 10: Power Off Mode (Shipping)");
  LOG_INF("========================================");

  /* Ensure flash control pins are HIGH */
  const struct device *gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
  if (device_is_ready(gpio1)) {
    gpio_pin_configure(gpio1, FLASH_WP_PIN, GPIO_OUTPUT_HIGH);
    gpio_pin_configure(gpio1, FLASH_HOLD_PIN, GPIO_OUTPUT_HIGH);
  }

  /*
   * ========================================
   * Driver Initialization
   * ========================================
   */

  /* Initialize LED driver */
  ret = led_driver_init();
  if (ret) {
    LOG_ERR("LED driver init failed: %d", ret);
  } else {
    LOG_INF("LED driver initialized");
    led_driver_startup_sequence();
  }

  /* Initialize button driver */
  ret = button_driver_init();
  if (ret) {
    LOG_ERR("Button driver init failed: %d", ret);
  } else {
    LOG_INF("Button driver initialized");
    button_driver_register_callback(button_event_handler);
  }

  /* Initialize Battery driver */
  ret = battery_init();
  if (ret) {
    LOG_ERR("Battery init failed: %d", ret);
  } else {
    LOG_INF("Battery driver initialized");
  }

  /* Initialize Qi Charger driver */
  ret = qi_charger_init();
  if (ret) {
    LOG_ERR("Qi charger init failed: %d", ret);
  } else {
    LOG_INF("Qi charger driver initialized");
  }

  /* Initialize LED indicator service */
  ret = led_indicator_init();
  if (ret) {
    LOG_ERR("LED indicator init failed: %d", ret);
  } else {
    LOG_INF("LED indicator service initialized");
  }

  /*
   * ========================================
   * Service Initialization (order matters!)
   * ========================================
   */

  /* 1. User settings (loads from NVS) */
  ret = user_settings_init();
  if (ret) {
    LOG_WRN("User settings init failed: %d (using defaults)", ret);
  } else {
    LOG_INF("User settings initialized");
  }

  /* Initialize Factory Test Service */
  factory_test_init();

  /* Initialize Power Manager */
  ret = power_manager_init();
  if (ret) {
    LOG_WRN("Power manager init failed: %d", ret);
  } else {
    LOG_INF("Power manager initialized");
  }

  /* 2. BLE service (enables Bluetooth stack) */
  ret = ble_service_init();
  if (ret) {
    LOG_ERR("BLE service init failed: %d", ret);
  } else {
    LOG_INF("BLE service initialized");
  }

  /* 3. Protocol handler (initializes NUS) */
  ret = protocol_init();
  if (ret) {
    LOG_ERR("Protocol init failed: %d", ret);
  } else {
    LOG_INF("Protocol handler initialized");
  }

  /* 4. Sensor manager (initializes all sensors) */
  ret = sensor_manager_init();
  if (ret) {
    LOG_ERR("Sensor manager init failed: %d", ret);
  } else {
    LOG_INF("Sensor manager initialized");
    sensor_manager_register_callback(sensor_data_callback);
  }

  /* Register Qi charger callback for LED feedback */
  qi_charger_register_callback(qi_charger_callback);

  /* Check initial charging state */
  if (qi_charger_is_charging()) {
    qi_charger_callback(true);
  }

  /* 5. Data log (requires SPI flash) */
  ret = data_log_init();
  if (ret) {
    LOG_WRN("Data log init failed: %d (history disabled)", ret);
  } else {
    LOG_INF("Data log initialized");
  }

  /* 6. BLE mode service (starts advertising) */
  ret = ble_mode_init();
  if (ret) {
    LOG_ERR("BLE mode init failed: %d", ret);
    /* Try starting advertising directly */
    ble_service_start_advertising();
  } else {
    LOG_INF("BLE mode service initialized");
  }

  /* Show pairing indicator if not connected */
  if (!ble_service_is_connected()) {
    led_indicator_show_pairing();
  }

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
    sensor_manager_measure_now();
  } else {
    ret = sensor_manager_start(interval_ms);
    if (ret) {
      LOG_ERR("Failed to start sensor measurements: %d", ret);
    } else {
      LOG_INF("Sensor measurements started (interval: %u ms)", interval_ms);
    }
  }

  /*
   * ========================================
   * Watchdog Timer
   * ========================================
   */

  ret = watchdog_init();
  if (ret < 0) {
    LOG_WRN("Watchdog init failed: %d (continuing without WDT)", ret);
  }

  /*
   * ========================================
   * Initialization Complete
   * ========================================
   */

  LOG_INF("========================================");
  LOG_INF("System initialized successfully");
  LOG_INF("BLE advertising as: %s", ble_service_get_device_name());
  LOG_INF("Watchdog: %s", (wdt_channel_id >= 0) ? "ENABLED (5s)" : "DISABLED");
  LOG_INF("Press button to toggle LED");
  LOG_INF("Long press (3s) for secondary action");
  LOG_INF("========================================");

  /* Signal ready with green LED */
  led_driver_set(1, true);
  k_msleep(500);
  led_driver_set(1, false);

  /*
   * ========================================
   * Main Loop
   * ========================================
   */

  while (1) {
    /* Feed watchdog first - must happen at least every 2 seconds */
    watchdog_feed();

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
     * - Feed WDT regularly (100ms << 2s timeout)
     */
    if (data_log_wait_for_request(K_MSEC(100)) == 0) {
      /* Semaphore signaled - history request pending, loop immediately */
      continue;
    }
    /* Timeout - no request, that's fine, just loop again */
  }

  return 0;
}
