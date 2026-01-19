/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SHT40 Temperature & Humidity Sensor Driver
 * Sensirion SHT40 - I2C Address 0x44
 *
 * Reference: Sensirion SHT4x Datasheet v6.4
 * Required by STCC4 CO2 sensor for accurate compensation.
 */

#ifndef SHT40_H
#define SHT40_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /* Measurement precision modes */
  typedef enum
  {
    SHT40_PRECISION_HIGH,   /* 8.2ms measurement time */
    SHT40_PRECISION_MEDIUM, /* 4.5ms measurement time */
    SHT40_PRECISION_LOW     /* 1.7ms measurement time */
  } sht40_precision_t;

  /* Heater modes for condensation removal */
  typedef enum
  {
    SHT40_HEATER_OFF,
    SHT40_HEATER_200MW_1S, /* 200mW for 1 second */
    SHT40_HEATER_200MW_100MS,
    SHT40_HEATER_110MW_1S,
    SHT40_HEATER_110MW_100MS,
    SHT40_HEATER_20MW_1S,
    SHT40_HEATER_20MW_100MS
  } sht40_heater_t;

  /* Measurement data */
  typedef struct
  {
    float temperature; /* Temperature in °C */
    float humidity;    /* Relative humidity in %RH */
  } sht40_data_t;

  /**
   * @brief Initialize SHT40 sensor
   *
   * @return 0 on success, negative errno on failure
   */
  int sht40_init(void);

  /**
   * @brief Perform a soft reset
   *
   * @return 0 on success, negative errno on failure
   */
  int sht40_soft_reset(void);

  /**
   * @brief Read serial number
   *
   * @param serial Pointer to store 32-bit serial number
   * @return 0 on success, negative errno on failure
   */
  int sht40_read_serial(uint32_t *serial);

  /**
   * @brief Measure temperature and humidity
   *
   * @param data Pointer to store measurement data
   * @param precision Measurement precision mode
   * @return 0 on success, negative errno on failure
   */
  int sht40_measure(sht40_data_t *data, sht40_precision_t precision);

  /**
   * @brief Measure with heater activation (for condensation removal)
   *
   * @param data Pointer to store measurement data
   * @param heater Heater mode
   * @return 0 on success, negative errno on failure
   */
  int sht40_measure_with_heater(sht40_data_t *data, sht40_heater_t heater);

  /**
   * @brief Get last valid temperature (for STCC4 compensation)
   *
   * @return Last temperature in °C, or NAN if not available
   */
  float sht40_get_temperature(void);

  /**
   * @brief Get last valid humidity (for STCC4 compensation)
   *
   * @return Last humidity in %RH, or NAN if not available
   */
  float sht40_get_humidity(void);

#ifdef __cplusplus
}
#endif

#endif /* SHT40_H */
