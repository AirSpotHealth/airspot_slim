/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BLE Mode Service - Dual-Mode BLE (Peripheral + Mesh)
 * Allows switching between traditional BLE and Bluetooth Mesh at runtime.
 */

#ifndef BLE_MODE_SERVICE_H
#define BLE_MODE_SERVICE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BLE operating modes
 */
typedef enum {
  BLE_MODE_PERIPHERAL = 0, /**< Traditional BLE peripheral mode (default) */
  BLE_MODE_MESH = 1        /**< Bluetooth Mesh mode */
} ble_mode_t;

/**
 * @brief Callback for mode change events
 * @param new_mode The mode that was switched to
 */
typedef void (*ble_mode_change_callback_t)(ble_mode_t new_mode);

/**
 * @brief Initialize the BLE mode service
 *
 * Reads the stored mode from flash and initializes BLE in that mode.
 * Default mode is BLE_MODE_PERIPHERAL if no mode is stored.
 *
 * @return 0 on success, negative errno on failure
 */
int ble_mode_init(void);

/**
 * @brief Switch BLE operating mode
 *
 * Gracefully shuts down current mode and starts the new mode.
 * The new mode is persisted to flash.
 *
 * @param mode The target mode to switch to
 * @return 0 on success, negative errno on failure
 */
int ble_mode_switch(ble_mode_t mode);

/**
 * @brief Get the current BLE operating mode
 *
 * @return Current BLE mode
 */
ble_mode_t ble_mode_get_current(void);

/**
 * @brief Check if device is provisioned (mesh mode only)
 *
 * @return true if device is provisioned in mesh network
 */
bool ble_mode_is_provisioned(void);

/**
 * @brief Register callback for mode change events
 *
 * @param callback Function to call when mode changes
 */
void ble_mode_register_callback(ble_mode_change_callback_t callback);

/**
 * @brief Factory reset - clear all mesh provisioning data
 *
 * Clears mesh keys and returns device to unprovisioned state.
 * Also resets mode to BLE_MODE_PERIPHERAL.
 *
 * @return 0 on success, negative errno on failure
 */
int ble_mode_factory_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_MODE_SERVICE_H */
