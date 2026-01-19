/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * LPS22HH Pressure Sensor Driver
 * STMicroelectronics LPS22HH - I2C Address 0x5D
 *
 * Reference: ST LPS22HH Datasheet
 * https://www.st.com/resource/en/datasheet/lps22hh.pdf
 */

#ifndef LPS22HH_H
#define LPS22HH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /* Output data rate options */
  typedef enum
  {
    LPS22HH_ODR_ONE_SHOT, /* One-shot mode */
    LPS22HH_ODR_1HZ,
    LPS22HH_ODR_10HZ,
    LPS22HH_ODR_25HZ,
    LPS22HH_ODR_50HZ,
    LPS22HH_ODR_75HZ,
    LPS22HH_ODR_100HZ,
    LPS22HH_ODR_200HZ
  } lps22hh_odr_t;

  /* Measurement data */
  typedef struct
  {
    float pressure;    /* Pressure in hPa */
    float temperature; /* Temperature in °C */
  } lps22hh_data_t;

  /**
   * @brief Initialize LPS22HH pressure sensor
   *
   * @return 0 on success, negative errno on failure
   */
  int lps22hh_init(void);

  /**
   * @brief Set output data rate
   *
   * @param odr Output data rate
   * @return 0 on success, negative errno on failure
   */
  int lps22hh_set_odr(lps22hh_odr_t odr);

  /**
   * @brief Trigger one-shot measurement
   *
   * @return 0 on success, negative errno on failure
   */
  int lps22hh_trigger_one_shot(void);

  /**
   * @brief Read pressure and temperature
   *
   * @param data Pointer to store measurement
   * @return 0 on success, negative errno on failure
   */
  int lps22hh_read(lps22hh_data_t *data);

  /**
   * @brief Get last valid pressure
   *
   * @return Pressure in hPa, or 0 if not available
   */
  float lps22hh_get_pressure(void);

  /**
   * @brief Get last valid temperature
   *
   * @return Temperature in °C, or NAN if not available
   */
  float lps22hh_get_temperature(void);

  /**
   * @brief Read WHO_AM_I register
   *
   * @param id Pointer to store device ID (should be 0xB3)
   * @return 0 on success, negative errno on failure
   */
  int lps22hh_read_id(uint8_t *id);

  /**
   * @brief Software reset
   *
   * @return 0 on success, negative errno on failure
   */
  int lps22hh_soft_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* LPS22HH_H */
