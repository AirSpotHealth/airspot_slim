/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sensor Manager - Coordinates all sensor readings
 *
 * Measurement workflow:
 * 1. Read SHT40 (temp/humidity) - required for STCC4 compensation
 * 2. Update STCC4 ambient conditions
 * 3. Read STCC4 (CO2)
 * 4. Read LPS22HH (pressure)
 * 5. Read battery voltage/percentage
 * 6. Read Qi charging status
 */

#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sensor data structure - all current readings */
typedef struct {
  /* Environmental */
  float temperature_c; /* °C from SHT40 */
  float humidity_rh;   /* %RH from SHT40 */
  uint16_t co2_ppm;    /* ppm from STCC4 */
  float pressure_hpa;  /* hPa from LPS22HH */

  /* Power */
  uint16_t battery_mv;     /* mV */
  uint8_t battery_percent; /* 0-100% */
  bool is_charging;        /* Qi status */
  bool battery_low;        /* <15% */
  bool battery_critical;   /* <5% */

  /* Status */
  bool valid;            /* All sensors read successfully */
  uint32_t timestamp_ms; /* Uptime when measured */
} sensor_data_t;

/* Callback for new sensor data */
typedef void (*sensor_data_callback_t)(const sensor_data_t *data);

/**
 * @brief Initialize all sensors
 *
 * Initializes in correct order: I2C -> SHT40 -> STCC4 -> LPS22HH -> Battery ->
 * Qi
 *
 * @return 0 on success, negative errno on failure
 */
int sensor_manager_init(void);

/**
 * @brief Start periodic measurements
 *
 * @param interval_ms Measurement interval in milliseconds (min 1000)
 * @return 0 on success
 */
int sensor_manager_start(uint32_t interval_ms);

/**
 * @brief Stop periodic measurements
 */
void sensor_manager_stop(void);

/**
 * @brief Trigger single measurement
 *
 * @return 0 on success
 */
int sensor_manager_measure_now(void);

/**
 * @brief Get latest sensor data
 *
 * @param data Pointer to store data
 * @return 0 on success, -ENODATA if no measurement yet
 */
int sensor_manager_get_data(sensor_data_t *data);

/**
 * @brief Register callback for new data
 *
 * @param callback Function called when new data available
 */
void sensor_manager_register_callback(sensor_data_callback_t callback);

/**
 * @brief Set power mode (changes measurement interval)
 *
 * @param power_mode 0=on-demand (manual only), 1=low (3min), 2=mid (1min),
 * 3=high (5sec)
 */
void sensor_manager_set_power_mode(uint8_t power_mode);

/**
 * @brief Check if sensors are initialized
 */
bool sensor_manager_is_ready(void);

/**
 * @brief Reset CO2 sensor (STCC4)
 *
 * @return 0 on success, negative error code on failure
 */
int sensor_manager_reset_co2_sensor(void);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_MANAGER_H */
