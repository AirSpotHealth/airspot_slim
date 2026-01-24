/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * STCC4 CO2 Sensor Driver
 * Sensirion STCC4 - I2C Address 0x64
 *
 * Reference: Sensirion STCC4 Datasheet
 * Note: Requires SHT40 for temperature/humidity compensation
 */

#ifndef STCC4_H
#define STCC4_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Measurement mode */
typedef enum {
  STCC4_MODE_CONTINUOUS, /* Continuous measurement */
  STCC4_MODE_SINGLE_SHOT /* Single-shot (power saving) */
} stcc4_mode_t;

/* CO2 measurement data */
typedef struct {
  uint16_t co2_ppm;  /* CO2 concentration in ppm */
  float temperature; /* Temperature from STCC4 (if available) */
  float humidity;    /* Relative humidity from STCC4 (if available) */
  uint16_t status;   /* Sensor status bits */
  bool valid;        /* True if measurement is valid */
} stcc4_data_t;

/**
 * @brief Initialize STCC4 CO2 sensor
 *
 * NOTE: SHT40 MUST be initialized first as STCC4 requires
 * external temperature/humidity for accurate compensation.
 *
 * @return 0 on success, negative errno on failure
 */
int stcc4_init(void);

/**
 * @brief Start continuous measurement mode
 *
 * @return 0 on success, negative errno on failure
 */
int stcc4_start_continuous(void);

/**
 * @brief Stop measurement
 *
 * @return 0 on success, negative errno on failure
 */
int stcc4_stop(void);

/**
 * @brief Trigger single-shot measurement
 *
 * @return 0 on success, negative errno on failure
 */
int stcc4_trigger_single_shot(void);

/**
 * @brief Read CO2 measurement
 *
 * @param data Pointer to store measurement
 * @return 0 on success, negative errno on failure
 */
int stcc4_read(stcc4_data_t *data);

/**
 * @brief Set ambient conditions for compensation
 *
 * Should be called with SHT40 readings before each measurement.
 *
 * @param temperature Temperature in °C
 * @param humidity Relative humidity in %RH
 * @return 0 on success, negative errno on failure
 */
int stcc4_set_ambient(float temperature, float humidity);

/**
 * @brief Set reference pressure for compensation
 *
 * Should be called with LPS22HH readings if available.
 *
 * @param pressure_hpa Pressure in hPa (mbar)
 * @return 0 on success, negative errno on failure
 */
int stcc4_set_pressure(uint16_t pressure_hpa);

/**
 * @brief Get last valid CO2 reading
 *
 * @return CO2 in ppm, or 0 if not available
 */
uint16_t stcc4_get_co2(void);

/**
 * @brief Check if sensor is ready for reading
 *
 * @return true if data is ready
 */
bool stcc4_data_ready(void);

/**
 * @brief Get product ID (for testing/verification)
 *
 * Sends GetProductID command (0x365B) to verify sensor communication.
 * Per working minimal uart code implementation.
 *
 * @param product_id Buffer to store product ID (should be at least 9 bytes)
 * @param len Length of buffer
 * @return 0 on success, negative errno on failure
 */
int stcc4_get_product_id(uint8_t *product_id, size_t len);

/**
 * @brief Get current measurement mode
 *
 * @return Current STCC4 mode (continuous or single-shot)
 */
stcc4_mode_t stcc4_get_mode(void);

/**
 * @brief Enter low-power sleep mode
 *
 * Puts the sensor in deep sleep for maximum power savings.
 * Note: After 3+ hours in sleep, conditioning should be performed.
 *
 * @return 0 on success, negative errno on failure
 */
int stcc4_enter_sleep_mode(void);

/**
 * @brief Exit low-power sleep mode
 *
 * Wakes the sensor from deep sleep. Sensor may NACK this command,
 * which is expected behavior.
 *
 * @return 0 (always success, NACK is expected)
 */
int stcc4_exit_sleep_mode(void);

/**
 * @brief Perform sensor conditioning
 *
 * Required after 3+ hours in sleep mode to restore accuracy.
 * This is a blocking operation that takes ~22 seconds.
 *
 * @return 0 on success, negative errno on failure
 */
int stcc4_perform_conditioning(void);

/**
 * @brief Enable testing mode (pauses ASC updates)
 *
 * This pauses the STCC4 ASC algorithm and prevents updates to ASC state.
 * Use this to disable auto-calibration when custom calibration is used.
 *
 * @return 0 on success, negative errno on failure
 */
int stcc4_enable_testing_mode(void);

/**
 * @brief Disable testing mode (resumes ASC updates)
 *
 * This resumes the STCC4 ASC algorithm and re-enables auto-calibration.
 *
 * @return 0 on success, negative errno on failure
 */
int stcc4_disable_testing_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* STCC4_H */
