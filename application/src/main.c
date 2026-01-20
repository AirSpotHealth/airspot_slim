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
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

/* AirSpot includes */
#include <airspot/version.h>

/* Driver includes */
#include <drivers/button_driver.h>
#include <drivers/led_driver.h>
#include <drivers/spi_flash.h>

/* Service includes */
#include <services/ble_mode_service.h>
#include <services/ble_service.h>
#include <services/data_log.h>
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

/* Button event handler */
static void button_event_handler(button_event_t event) {
  switch (event) {
  case BUTTON_EVENT_PRESSED:
    LOG_INF("Button pressed - triggering measurement");
    led_driver_set(1, true); /* LED on while measuring */
    sensor_manager_measure_now();
    break;

  case BUTTON_EVENT_RELEASED:
    LOG_DBG("Button released");
    led_driver_set(1, false); /* LED off */
    break;

  case BUTTON_EVENT_LONG_PRESS:
    LOG_INF("Button long press - toggling LED 2");
    led_driver_toggle(2);
    break;
  }
}

/* Sensor data callback - called when new measurement is available */
static void sensor_data_callback(const sensor_data_t *data) {
  if (!data || !data->valid) {
    return;
  }

  LOG_INF("CO2=%uppm T=%.1f°C H=%.0f%% P=%.0fhPa Batt=%u%%%s", data->co2_ppm,
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
  int ret = data_log_store_record(timestamp, data.co2_ppm, RECORD_TYPE_CO2);
  if (ret != 0) {
    LOG_ERR("Failed to store CO2 record: %d", ret);
  } else {
    LOG_DBG("Stored CO2: %u ppm at t=%u", data.co2_ppm, timestamp);
  }
}

int main(void) {
  int ret;

  /* Boot banner */
  printk("\n\n");
  printk("========================================\n");
  printk("AirSpot Slim Firmware v%s\n", AIRSPOT_FIRMWARE_VERSION_STRING);
  printk("Phase 9: MCUboot DFU + OTA Updates\n");
  printk("========================================\n");

  LOG_INF("========================================");
  LOG_INF("AirSpot Slim Firmware v%s", AIRSPOT_FIRMWARE_VERSION_STRING);
  LOG_INF("Phase 9: MCUboot DFU + OTA Updates");
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
   * Initialization Complete
   * ========================================
   */

  LOG_INF("========================================");
  LOG_INF("System initialized successfully");
  LOG_INF("BLE advertising as: %s", ble_service_get_device_name());
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
    /* Process any pending history requests */
    protocol_process_history();

    /* Store CO2 data if due */
    if (data_log_due) {
      data_log_due = false;
      store_co2_data();
    }

    /* Sleep longer for power savings
     * The system wakes on:
     * - BLE events (handled by BLE stack)
     * - Sensor timer (handled by work queue)
     * - Button press (handled by interrupt/polling)
     *
     * We only need to wake periodically for:
     * - Processing history requests
     * - Checking data_log_due flag
     */
    k_sleep(K_SECONDS(10)); /* Wake every 10 seconds instead of 1 */
  }

  return 0;
}
