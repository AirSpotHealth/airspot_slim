/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AirSpot Protocol Handler
 *
 * Implements the binary protocol for app communication.
 * Frame format: [0xFF][0xAA][CMD][LEN][DATA...][CHECKSUM]
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include <airspot/version.h>
#include <services/airspot_nus.h>
#include <services/ble_mode_service.h>
#include <services/data_log.h>
#include <services/protocol.h>
#include <services/sensor_manager.h>
#include <services/user_settings.h>

LOG_MODULE_REGISTER(protocol, CONFIG_AIRSPOT_LOG_LEVEL);

/* Frame buffer for receiving partial frames */
#define RX_FRAME_MAX_LEN 64
static uint8_t rx_frame[RX_FRAME_MAX_LEN];
static uint8_t rx_frame_idx = 0;

/* Hardware/Software version strings */
#define HARDWARE_VERSION "SLIM_1.0"
#define SOFTWARE_VERSION AIRSPOT_FIRMWARE_VERSION_STRING

/* Forward declarations */
static void frame_received_handler(const uint8_t *frame, uint16_t len);
static void set_frame_checksum(uint8_t *frame, uint16_t len);
static uint8_t verify_frame_checksum(const uint8_t *frame, uint16_t len);

/* NUS RX callback - accumulates data into frames */
static void nus_rx_callback(const uint8_t *data, uint16_t len) {
  for (uint16_t i = 0; i < len; i++) {
    if (rx_frame_idx >= RX_FRAME_MAX_LEN) {
      LOG_WRN("RX frame overflow, resetting");
      rx_frame_idx = 0;
    }

    rx_frame[rx_frame_idx++] = data[i];

    /* Check for valid frame header */
    if (rx_frame_idx == 1 && rx_frame[0] != CMD_FIRST_BYTE) {
      rx_frame_idx = 0;
      continue;
    }
    if (rx_frame_idx == 2 && rx_frame[1] != CMD_SECOND_BYTE) {
      rx_frame_idx = 0;
      continue;
    }

    /* Check if frame is complete: 4 header + payload + 1 checksum */
    if (rx_frame_idx >= 4) {
      uint16_t expected_len = 4 + rx_frame[3] + 1;
      if (rx_frame_idx == expected_len) {
        frame_received_handler(rx_frame, rx_frame_idx);
        rx_frame_idx = 0;
      }
    }
  }
}

int protocol_init(void) {
  int ret = airspot_nus_init(nus_rx_callback);
  if (ret) {
    LOG_ERR("Failed to init NUS: %d", ret);
    return ret;
  }

  LOG_INF("Protocol handler initialized");
  return 0;
}

/**
 * @brief Calculate and set checksum for a frame
 */
static void set_frame_checksum(uint8_t *frame, uint16_t len) {
  uint32_t sum = 0;
  for (uint16_t i = 0; i < len - 1; i++) {
    sum += frame[i];
  }
  frame[len - 1] = (uint8_t)(sum & 0xFF);
}

/**
 * @brief Verify frame checksum
 */
static uint8_t verify_frame_checksum(const uint8_t *frame, uint16_t len) {
  uint32_t sum = 0;
  for (uint16_t i = 0; i < len - 1; i++) {
    sum += frame[i];
  }
  return ((uint8_t)(sum & 0xFF)) == frame[len - 1];
}

/**
 * @brief Send a frame via NUS
 */
static void send_frame(const uint8_t *frame, uint16_t len) {
  int ret = airspot_nus_send(frame, len);
  if (ret) {
    LOG_WRN("Failed to send frame: %d", ret);
  }
}

/**
 * @brief Handle a complete received frame
 */
static void frame_received_handler(const uint8_t *frame, uint16_t len) {
  if (!verify_frame_checksum(frame, len)) {
    LOG_WRN("Frame checksum failed");
    return;
  }

  uint8_t cmd = frame[2];
  LOG_DBG("Received cmd: 0x%02X, len: %d", cmd, len);

  switch (cmd) {
  case CMD_GET_CO2_VALUE:
    protocol_send_realtime_co2();
    break;

  case CMD_REFRESH_CO2_VALUE:
    /* Trigger immediate measurement then send */
    sensor_manager_measure_now();
    protocol_send_realtime_co2();
    break;

  case CMD_GET_DEVICE_STATE:
    protocol_send_device_state();
    break;

  case CMD_GET_BATTERY_LEVEL:
    protocol_send_battery_level();
    break;

  case CMD_GET_HARDWARE_VERSION:
    protocol_send_hardware_version();
    break;

  case CMD_GET_SOFTWARE_VERSION:
    protocol_send_software_version();
    break;

  case CMD_GET_SYSTEM_TIME:
    /* Send current time (app-synced or uptime as fallback) */
    {
      uint8_t tx_frame[16] = {0};
      uint32_t time;
      if (user_settings_is_time_set()) {
        time = user_settings_get_time_now();
      } else {
        time = k_uptime_get_32() / 1000; /* fallback: seconds since boot */
      }
      tx_frame[0] = CMD_FIRST_BYTE;
      tx_frame[1] = CMD_SECOND_BYTE;
      tx_frame[2] = CMD_GET_SYSTEM_TIME;
      tx_frame[3] = 0x04;
      tx_frame[4] = (uint8_t)(time >> 24);
      tx_frame[5] = (uint8_t)(time >> 16);
      tx_frame[6] = (uint8_t)(time >> 8);
      tx_frame[7] = (uint8_t)(time);
      set_frame_checksum(tx_frame, 9);
      send_frame(tx_frame, 9);
    }
    break;

  case CMD_SET_TIMEBASE:
    /* Set device time (synced from app) */
    if (len >= 9) {
      uint32_t time = ((uint32_t)frame[4] << 24) | ((uint32_t)frame[5] << 16) |
                      ((uint32_t)frame[6] << 8) | (uint32_t)frame[7];
      user_settings_set_timebase(time);
      LOG_INF("Timebase set: %u", time);

      /* Send ACK */
      uint8_t tx_frame[16] = {0};
      tx_frame[0] = CMD_FIRST_BYTE;
      tx_frame[1] = CMD_SECOND_BYTE;
      tx_frame[2] = CMD_SET_TIMEBASE;
      tx_frame[3] = 0x01;
      tx_frame[4] = 0x01; /* Success */
      set_frame_checksum(tx_frame, 6);
      send_frame(tx_frame, 6);
    }
    break;

  case CMD_RESET_DEVICE:

    LOG_INF("Reset device command received");
    k_sleep(K_MSEC(100));
    sys_reboot(SYS_REBOOT_COLD);
    break;

  case CMD_SET_BUZZER_STATE:
    if (len >= 6) {
      user_settings_set_buzzer(frame[4]);
      LOG_INF("Buzzer set: %d (stub)", frame[4]);
    }
    protocol_send_device_state();
    break;

  case CMD_SET_VIBRATOR_STATE:
    if (len >= 6) {
      user_settings_set_vibrator(frame[4]);
      LOG_INF("Vibrator set: %d (stub)", frame[4]);
    }
    protocol_send_device_state();
    break;

  case CMD_SET_POWER_MODE:
    if (len >= 6) {
      user_settings_set_power_mode(frame[4]);
      sensor_manager_set_power_mode(frame[4]);
      LOG_INF("Power mode set: %d", frame[4]);
    }
    protocol_send_device_state();
    break;

  case CMD_SET_FLIGHT_MODE:
    if (len >= 6) {
      user_settings_set_flight_mode(frame[4]);
      LOG_INF("Flight mode set: %d", frame[4]);
    }
    protocol_send_device_state();
    break;

  case CMD_GET_CURRENT_HALF_PAGE:
    /* Get current half-page number for history */
    protocol_send_current_half_page();
    break;

  case CMD_HISTORY_REQUEST:
    /* Request history data for a specific half-page */
    if (len >= 7) {
      uint16_t half_page = ((uint16_t)frame[4] << 8) | frame[5];
      LOG_INF("History request: half-page %d", half_page);
      data_log_request(half_page);
    }
    break;

  case CMD_ERASE_HISTORY:
    /* Erase all history data */
    LOG_WRN("Erase history command received");
    data_log_request_erase_all();
    break;

  default:
    LOG_WRN("Unknown command: 0x%02X", cmd);
    break;
  }
}

void protocol_send_realtime_co2(void) {
  uint8_t tx_frame[16] = {0};
  sensor_data_t data;

  tx_frame[0] = CMD_FIRST_BYTE;
  tx_frame[1] = CMD_SECOND_BYTE;
  tx_frame[2] = CMD_GET_CO2_VALUE;
  tx_frame[3] = 0x06; /* payload length */

  if (sensor_manager_get_data(&data) == 0 && data.valid) {
    /* Timestamp (32-bit, big-endian) */
    uint32_t time = data.timestamp_ms / 1000;
    tx_frame[4] = (uint8_t)(time >> 24);
    tx_frame[5] = (uint8_t)(time >> 16);
    tx_frame[6] = (uint8_t)(time >> 8);
    tx_frame[7] = (uint8_t)(time);

    /* CO2 value (16-bit, big-endian) */
    tx_frame[8] = (uint8_t)(data.co2_ppm >> 8);
    tx_frame[9] = (uint8_t)(data.co2_ppm & 0xFF);
  } else {
    /* Error state: 0xFFFF for CO2 */
    tx_frame[4] = 0x00;
    tx_frame[5] = 0x00;
    tx_frame[6] = 0x00;
    tx_frame[7] = 0x00;
    tx_frame[8] = 0xFF;
    tx_frame[9] = 0xFF;
  }

  set_frame_checksum(tx_frame, 11);
  send_frame(tx_frame, 11);
}

void protocol_send_battery_level(void) {
  uint8_t tx_frame[16] = {0};
  sensor_data_t data;

  tx_frame[0] = CMD_FIRST_BYTE;
  tx_frame[1] = CMD_SECOND_BYTE;
  tx_frame[2] = CMD_GET_BATTERY_LEVEL;
  tx_frame[3] = 0x02; /* payload length */

  if (sensor_manager_get_data(&data) == 0) {
    tx_frame[4] = data.battery_percent;
    tx_frame[5] = data.is_charging ? 1 : 0;
  } else {
    tx_frame[4] = 0;
    tx_frame[5] = 0;
  }

  set_frame_checksum(tx_frame, 7);
  send_frame(tx_frame, 7);
}

void protocol_send_device_state(void) {
  uint8_t tx_frame[32] = {0};
  sensor_data_t data;

  tx_frame[0] = CMD_FIRST_BYTE;
  tx_frame[1] = CMD_SECOND_BYTE;
  tx_frame[2] = CMD_GET_DEVICE_STATE;

  /* Simplified device state for Slim:
   * - Buzzer state (1 byte) - default 0
   * - Vibrator state (1 byte) - default 0
   * - Power mode (1 byte) - default 0
   * - Device type (1 byte) - DEVICE_TYPE_SLIM
   * - Battery level (1 byte)
   * - Charging (1 byte)
   */
  int offset = 4;

  tx_frame[offset++] = user_settings_get_buzzer();   /* buzzer state */
  tx_frame[offset++] = user_settings_get_vibrator(); /* vibrator state */
  tx_frame[offset++] = user_settings_get_power_mode();
  tx_frame[offset++] = DEVICE_TYPE_SLIM;

  if (sensor_manager_get_data(&data) == 0) {
    tx_frame[offset++] = data.battery_percent;
    tx_frame[offset++] = data.is_charging ? 1 : 0;
  } else {
    tx_frame[offset++] = 0;
    tx_frame[offset++] = 0;
  }

  tx_frame[3] = offset - 4; /* payload length */

  set_frame_checksum(tx_frame, offset + 1);
  send_frame(tx_frame, offset + 1);
}

void protocol_send_hardware_version(void) {
  uint8_t tx_frame[32] = {0};
  const char *ver = HARDWARE_VERSION;
  uint8_t ver_len = strlen(ver);

  tx_frame[0] = CMD_FIRST_BYTE;
  tx_frame[1] = CMD_SECOND_BYTE;
  tx_frame[2] = CMD_GET_HARDWARE_VERSION;
  tx_frame[3] = ver_len;
  memcpy(&tx_frame[4], ver, ver_len);

  set_frame_checksum(tx_frame, 5 + ver_len);
  send_frame(tx_frame, 5 + ver_len);
}

void protocol_send_software_version(void) {
  uint8_t tx_frame[32] = {0};
  const char *ver = SOFTWARE_VERSION;
  uint8_t ver_len = strlen(ver);

  tx_frame[0] = CMD_FIRST_BYTE;
  tx_frame[1] = CMD_SECOND_BYTE;
  tx_frame[2] = CMD_GET_SOFTWARE_VERSION;
  tx_frame[3] = ver_len;
  memcpy(&tx_frame[4], ver, ver_len);

  set_frame_checksum(tx_frame, 5 + ver_len);
  send_frame(tx_frame, 5 + ver_len);
}

void protocol_send_current_half_page(void) {
  uint8_t tx_frame[16] = {0};
  uint16_t half_page = data_log_get_current_half_page();

  LOG_DBG("Sending current half-page: %d", half_page);

  tx_frame[0] = CMD_FIRST_BYTE;
  tx_frame[1] = CMD_SECOND_BYTE;
  tx_frame[2] = CMD_HISTORY_REQUEST; /* Same command for response */
  tx_frame[3] = 0x02;                /* 2 bytes payload */
  tx_frame[4] = (uint8_t)(half_page >> 8);
  tx_frame[5] = (uint8_t)(half_page & 0xFF);

  set_frame_checksum(tx_frame, 7);
  send_frame(tx_frame, 7);
}

void protocol_send_history_data(const uint8_t *data, uint8_t record_count,
                                uint16_t half_page_number) {
  /* Max frame size: header(4) + 16 records * 8 bytes + page_num(2) +
   * checksum(1) = 4 + 128 + 2 + 1 = 135 bytes */
  static uint8_t tx_frame[140];

  const uint8_t record_size = 8; /* sizeof(record_t) */
  uint8_t data_len = record_count * record_size;

  LOG_DBG("Sending history: %d records for half-page %d", record_count,
          half_page_number);

  /* Build frame header */
  tx_frame[0] = CMD_FIRST_BYTE;
  tx_frame[1] = CMD_SECOND_BYTE;
  tx_frame[2] = CMD_HISTORY_REQUEST;
  tx_frame[3] = data_len + 2; /* Data + 2 bytes for page number */

  /* Copy record data */
  uint8_t offset = 4;
  for (uint8_t i = 0; i < record_count; i++) {
    const record_t *rec = (const record_t *)&data[i * record_size];

    /* Timestamp (big-endian) */
    tx_frame[offset++] = (uint8_t)(rec->timestamp >> 24);
    tx_frame[offset++] = (uint8_t)(rec->timestamp >> 16);
    tx_frame[offset++] = (uint8_t)(rec->timestamp >> 8);
    tx_frame[offset++] = (uint8_t)(rec->timestamp);

    /* Value (already stored big-endian in record) */
    tx_frame[offset++] = (uint8_t)(rec->value & 0xFF);
    tx_frame[offset++] = (uint8_t)(rec->value >> 8);

    /* Type */
    tx_frame[offset++] = rec->type;

    /* Reserved */
    tx_frame[offset++] = rec->reserved;
  }

  /* Append half-page number */
  tx_frame[offset++] = (uint8_t)(half_page_number >> 8);
  tx_frame[offset++] = (uint8_t)(half_page_number & 0xFF);

  /* Calculate and append checksum */
  set_frame_checksum(tx_frame, offset + 1);

  send_frame(tx_frame, offset + 1);
}

void protocol_send_erase_done(void) {
  uint8_t tx_frame[16] = {0};

  LOG_INF("Sending erase done notification");

  tx_frame[0] = CMD_FIRST_BYTE;
  tx_frame[1] = CMD_SECOND_BYTE;
  tx_frame[2] = CMD_ERASE_HISTORY;
  tx_frame[3] = 0x01;
  tx_frame[4] = 0x01; /* Success */

  set_frame_checksum(tx_frame, 6);
  send_frame(tx_frame, 6);
}

void protocol_process_history(void) {
  /* Skip history processing in mesh mode - NUS is not available */
  if (ble_mode_get_current() == BLE_MODE_MESH) {
    return;
  }

  /* Handle erase all request */
  if (data_log_is_erase_pending()) {
    LOG_INF("Processing history erase...");
    int ret = data_log_erase_all();
    if (ret == 0) {
      protocol_send_erase_done();
    }
    return;
  }

  /* Handle history data request */
  if (data_log_has_pending_request()) {
    record_page_t page;
    uint8_t count;
    uint16_t half_page;

    int ret = data_log_process_request(&page, &count, &half_page);
    if (ret == 0) {
      protocol_send_history_data(page.buf, count, half_page);
    } else {
      LOG_WRN("History request failed: %d", ret);
    }
  }
}
