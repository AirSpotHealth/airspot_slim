/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mesh Sensor Model - CO2 Sensor Data over Bluetooth Mesh
 *
 * Publishes CO2, temperature, humidity, and pressure data over Bluetooth Mesh
 * using the standard Sensor Model (0x1100).
 */

#ifndef MESH_SENSOR_H
#define MESH_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Bluetooth Mesh Sensor Property IDs (from Mesh Device Properties spec) */
#define SENSOR_PROP_CO2_CONCENTRATION 0x0090        /* CO2 Concentration (ppm) */
#define SENSOR_PROP_PRESENT_AMBIENT_TEMP 0x004F     /* Temperature (0.01°C) */
#define SENSOR_PROP_PRESENT_AMBIENT_HUMIDITY 0x0050 /* Humidity (0.01%) */
#define SENSOR_PROP_PRESSURE 0x0073                 /* Pressure (0.1 Pa) */

  /**
   * @brief Sensor data structure for mesh publishing
   */
  typedef struct
  {
    uint16_t co2_ppm;         /* CO2 in PPM */
    int16_t temperature_c100; /* Temperature in 0.01°C units */
    uint16_t humidity_c100;   /* Humidity in 0.01% units */
    uint32_t pressure_pa10;   /* Pressure in 0.1 Pa units */
    bool valid;               /* Data validity flag */
  } mesh_sensor_data_t;

  /**
   * @brief Initialize the mesh sensor model
   *
   * Should be called after bt_mesh_init() when mesh mode is active.
   *
   * @return 0 on success, negative errno on failure
   */
  int mesh_sensor_init(void);

  /**
   * @brief Update sensor data for mesh publishing
   *
   * Call this with new sensor readings. The data will be published
   * according to the configured publish period.
   *
   * @param data Pointer to sensor data structure
   * @return 0 on success, negative errno on failure
   */
  int mesh_sensor_update(const mesh_sensor_data_t *data);

  /**
   * @brief Publish sensor data immediately
   *
   * Forces immediate publication of current sensor data.
   * Use sparingly as frequent publishing can flood the mesh network.
   *
   * @return 0 on success, negative errno on failure
   */
  int mesh_sensor_publish_now(void);

  /**
   * @brief Get the mesh composition (elements and models)
   *
   * Used by ble_mode_service to initialize mesh with sensor models.
   *
   * @return Pointer to the mesh composition structure
   */
  const struct bt_mesh_comp *mesh_sensor_get_composition(void);

  /**
   * @brief Check if mesh sensor is ready
   *
   * @return true if initialized and ready to publish
   */
  bool mesh_sensor_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* MESH_SENSOR_H */
