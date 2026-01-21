/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Factory Test Service
 *
 * Manages manufacturing test sequence and diagnostics.
 */

#ifndef FACTORY_TEST_H
#define FACTORY_TEST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test Result Codes
 */
typedef enum {
  FACTORY_TEST_PASS = 0x00,
  FACTORY_TEST_FAIL_FLASH = 0x01,
  FACTORY_TEST_FAIL_BATTERY = 0x02,
  FACTORY_TEST_FAIL_SENSORS = 0x03,
  FACTORY_TEST_FAIL_BLE = 0x04, /* BLE self-test? */
  FACTORY_TEST_FAIL_OTHER = 0xFF
} factory_test_result_t;

/**
 * @brief Detailed Test Report Structure
 */
typedef struct {
  uint8_t flash_status;   /* 1=OK, 0=Fail */
  uint8_t battery_status; /* 1=OK, 0=Fail */
  uint8_t sht40_status;   /* 1=OK, 0=Fail */
  uint8_t stcc4_status;   /* 1=OK, 0=Fail */
  uint8_t lps22hh_status; /* 1=OK, 0=Fail */
  uint8_t qi_status;      /* 1=Present, 0=Not Present (Info only) */
  uint16_t battery_mv;    /* Battery voltage in mV */
} factory_test_report_t;

/**
 * @brief Initialize factory test service
 *
 * @return 0 on success
 */
int factory_test_init(void);

/**
 * @brief Start factory auto-test sequence
 *
 * Runs through all hardware checks and reports result via BLE and LEDs.
 * LEDs: Green blink = PASS, Red blink = FAIL.
 *
 * @return 0 on start success
 */
int factory_test_start_auto(void);

/**
 * @brief Stop/Abort factory test
 */
void factory_test_stop(void);

/**
 * @brief Check if factory test is currently running
 */
bool factory_test_is_running(void);

/**
 * @brief Get last test report
 *
 * @param report Pointer to report structure
 * @return 0 on success
 */
int factory_test_get_report(factory_test_report_t *report);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_TEST_H */
