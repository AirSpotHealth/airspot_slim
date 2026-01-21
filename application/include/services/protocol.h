/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AirSpot Protocol Handler
 *
 * Binary protocol matching the existing AirSpot device for app compatibility.
 * Frame format: [0xFF][0xAA][CMD][LEN][DATA...][CHECKSUM]
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Protocol frame header */
#define CMD_FIRST_BYTE 0xFF
#define CMD_SECOND_BYTE 0xAA

/* Protocol Commands - matching old firmware */
#define CMD_GET_CO2_VALUE 0x01
#define CMD_SET_BUZZER_STATE 0x02
#define CMD_SET_VIBRATOR_STATE 0x03
#define CMD_SET_TIMEBASE 0x04
#define CMD_SET_POWER_MODE 0x05
#define CMD_SET_BLE_DISCONNECT 0x06
#define CMD_GET_DEVICE_STATE 0x08
#define CMD_GET_DEVICE_NAME 0x09
#define CMD_SET_DEVICE_NAME 0x0A
#define CMD_GET_CURRENT_HALF_PAGE                                              \
  0x0B                           /* History: get current write position        \
                                  */
#define CMD_HISTORY_REQUEST 0x0C /* History: request data for half-page */
#define CMD_GET_HARDWARE_VERSION 0x12
#define CMD_GET_SOFTWARE_VERSION 0x13
#define CMD_GET_BATTERY_LEVEL 0x20
#define CMD_REFRESH_CO2_VALUE 0x21
#define CMD_SET_DND 0x22
#define CMD_RESET_SENSOR 0x24 /* Reset CO2 sensor */
#define CMD_RESET_DEVICE 0x27
#define CMD_FIND_MY_AIRSPOT 0x10 /* Blink LEDs */
#define CMD_SET_FLIGHT_MODE 0x32
#define CMD_ENTER_FACTORY_TEST 0xD0
#define CMD_FACTORY_TEST_RESULT 0xDD
#define CMD_SET_DEVICE_SLEEP 0xEE /* Enter Power Off (shipping) mode */
#define CMD_FACTORY_RESET 0xFA
#define CMD_ERASE_HISTORY 0xFD /* History: erase all data */
#define CMD_GET_SYSTEM_TIME 0xFE

/* Slim-specific: Device type indicator for app differentiation */
#define DEVICE_TYPE_SLIM 0x02 /* 0x01 = Original, 0x02 = Slim */

/**
 * @brief Initialize protocol handler
 *
 * Sets up NUS service and registers RX callback.
 *
 * @return 0 on success, negative errno on failure
 */
int protocol_init(void);

/**
 * @brief Process received data
 *
 * Called by NUS RX callback. Parses frames and dispatches commands.
 *
 * @param data Received data
 * @param len Data length
 */
void protocol_process_data(const uint8_t *data, uint16_t len);

/**
 * @brief Send realtime CO2 value
 *
 * Sends current sensor data to the connected device.
 */
void protocol_send_realtime_co2(void);

/**
 * @brief Send battery level
 */
void protocol_send_battery_level(void);

/**
 * @brief Send device state/configuration
 */
void protocol_send_device_state(void);

/**
 * @brief Send hardware version
 */
void protocol_send_hardware_version(void);

/**
 * @brief Send software version
 */
void protocol_send_software_version(void);

/**
 * @brief Send current half-page number (where history is being written)
 */
void protocol_send_current_half_page(void);

/**
 * @brief Send history data for a specific half-page
 *
 * @param data Page data buffer (128 bytes for half-page)
 * @param record_count Number of valid records (0-16)
 * @param half_page_number The half-page number being sent
 */
void protocol_send_history_data(const uint8_t *data, uint8_t record_count,
                                uint16_t half_page_number);

/**
 * @brief Send erase history complete notification
 */
void protocol_send_erase_done(void);

/**
 * @brief Process pending history requests
 *
 * Should be called periodically from the main loop.
 * Handles reading from flash and sending history data.
 */
void protocol_process_history(void);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_H */
