/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AirSpot NUS Service - Nordic UART Service wrapper
 */

#ifndef AIRSPOT_NUS_H
#define AIRSPOT_NUS_H

#include <stdbool.h>
#include <stdint.h>

/* Forward declaration for BLE connection handle */
struct bt_conn;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback for data received via NUS RX characteristic
 *
 * @param data Received data buffer
 * @param len Data length
 */
typedef void (*airspot_nus_rx_callback_t)(const uint8_t *data, uint16_t len);

/**
 * @brief Initialize NUS service
 *
 * Must be called after bt_enable() but before advertising starts.
 *
 * @param rx_callback Callback for received data
 * @return 0 on success, negative errno on failure
 */
int airspot_nus_init(airspot_nus_rx_callback_t rx_callback);

/**
 * @brief Send data via NUS TX characteristic
 *
 * @param data Data to send
 * @param len Data length
 * @return 0 on success, negative errno on failure
 */
int airspot_nus_send(const uint8_t *data, uint16_t len);
int airspot_nus_send_session(const uint8_t *data, uint16_t len, uint32_t generation);
uint32_t airspot_nus_generation(void);

/**
 * @brief Check if NUS is ready to send
 *
 * @return true if connected and notifications enabled
 */
bool airspot_nus_is_ready(void);

/**
 * @brief Set connection handle for NUS operations
 *
 * Called by ble_service when connection state changes.
 *
 * @param conn Connection handle (NULL if disconnected)
 */
void airspot_nus_set_conn(struct bt_conn *conn);

#ifdef __cplusplus
}
#endif

#endif /* AIRSPOT_NUS_H */
