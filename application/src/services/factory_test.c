/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Factory Test Service Implementation
 */

#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/battery.h>
#include <drivers/led_driver.h>
#include <drivers/lps22hh.h>
#include <drivers/qi_charger.h>
#include <drivers/sht40.h>
#include <drivers/spi_flash.h>
#include <drivers/stcc4.h>
#include <services/ble_service.h>
#include <services/factory_test.h>
#include <services/sensor_manager.h>

LOG_MODULE_REGISTER(factory_test, CONFIG_AIRSPOT_LOG_LEVEL);

static bool test_running = false;
static factory_test_report_t last_report = {0};

/* Blink sequences */
static void blink_pass(void) {
  /* Blink Green 3 times */
  led_driver_all_off();
  for (int i = 0; i < 3; i++) {
    led_driver_set(1, true); /* Green */
    k_msleep(200);
    led_driver_set(1, false);
    k_msleep(200);
  }
}

static void blink_fail(void) {
  /* Blink Red 5 times fast */
  led_driver_all_off();
  for (int i = 0; i < 5; i++) {
    led_driver_set(0, true); /* Red */
    k_msleep(100);
    led_driver_set(0, false);
    k_msleep(100);
  }
}

int factory_test_init(void) {
  LOG_INF("Factory Test Service init checking...");
  /* Nothing special to init, relies on drivers being ready */
  return 0;
}

int factory_test_start_auto(void) {
  LOG_INF("Starting Factory Auto Test...");
  test_running = true;

  /* Reset report */
  memset(&last_report, 0, sizeof(last_report));

  /* 1. SPI Flash Check */
  /* Assuming spi_flash is strictly needed, check if ready */
  /* Note: spi_flash driver might only expose init. We'll assume if init
     passed during boot, it's okay, or try a dummy op?
     Actually, `spi_flash_init` acts as a check. */
  if (spi_flash_init() == 0) {
    last_report.flash_status = 1;
  } else {
    LOG_ERR("Factory Test: SPI Flash FAIL");
    last_report.flash_status = 0;
  }

  /* 2. Battery Check */
  uint16_t mv = battery_read_voltage_mv();
  last_report.battery_mv = mv;
  if (mv > 2000 && mv < 4500) { /* Plausible range */
    last_report.battery_status = 1;
  } else {
    LOG_ERR("Factory Test: Battery FAIL (%u mV)", mv);
    last_report.battery_status = 0;
  }

  /* Sensor command sequences share the measurement owner, including probes. */
  sensor_manager_factory_probe(&last_report.sht40_status,
      &last_report.lps22hh_status, &last_report.stcc4_status);

  /* 6. Qi Charger Info (Not a fail condition) */
  last_report.qi_status = qi_charger_is_charging() ? 1 : 0;

  /* Aggregate Result */
  bool pass = last_report.flash_status && last_report.battery_status &&
              last_report.sht40_status && last_report.stcc4_status &&
              last_report.lps22hh_status;

  if (pass) {
    LOG_INF("Factory Test: PASS");
    blink_pass();
  } else {
    LOG_ERR("Factory Test: FAIL");
    blink_fail();
  }

  /* Send result via BLE? The app will poll or request result via 0xDD */
  /* Done */
  test_running = false;
  return pass ? 0 : -1;
}

void factory_test_stop(void) {
  test_running = false;
  led_driver_all_off();
}

bool factory_test_is_running(void) { return test_running; }

int factory_test_get_report(factory_test_report_t *report) {
  if (!report)
    return -1;
  *report = last_report;
  return 0;
}
