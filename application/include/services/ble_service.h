/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <stdbool.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file ble_service.h
 * @brief BLE service interface
 *
 * This module provides BLE stack initialization, advertising,
 * and connection management.
 */

/**
 * @brief Initialize BLE service
 *
 * @return 0 on success, negative error code on failure
 */
int ble_service_init(void);

/**
 * @brief Get the BLE device name
 *
 * Returns the device name in format "AirSpot-XXXXXX" where XXXXXX
 * is derived from the BLE MAC address.
 *
 * @return Pointer to null-terminated device name string
 */
const char *ble_service_get_device_name(void);

/**
 * @brief Check if BLE is connected
 *
 * @return true if connected, false otherwise
 */
bool ble_service_is_connected(void);

/**
 * @brief Disconnect BLE connection
 */
void ble_service_disconnect(void);

/**
 * @brief Start BLE advertising
 *
 * @return 0 on success, negative error code on failure
 */
int ble_service_start_advertising(void);

/**
 * @brief Stop BLE advertising
 *
 * @return 0 on success, negative error code on failure
 */
int ble_service_stop_advertising(void);

/**
 * @brief Update the Battery Service (BAS) battery level
 *
 * @param level Battery level in percent (0-100)
 */
void ble_service_update_battery_level(uint8_t level);

#ifdef __cplusplus
}
#endif

#endif /* BLE_SERVICE_H */
