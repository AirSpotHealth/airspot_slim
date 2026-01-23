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

#include <drivers/led_driver.h>

#include <airspot/version.h>
#include <services/airspot_nus.h>
#include <services/ble_mode_service.h>
#include <services/ble_service.h>
#include <services/data_log.h>
#include <services/factory_test.h>
#include <services/power_manager.h>
#include <services/protocol.h>
#include <services/sensor_manager.h>
#include <services/user_settings.h>

/* Additional commands from old firmware */
#define CMD_GET_SENSOR_DETAILS 0x30

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

  case CMD_GET_DEVICE_NAME:
    /* Return device name (e.g., "AirSpot-167CD0") */
    {
      const char *name = ble_service_get_device_name();
      uint8_t name_len = strlen(name);
      uint8_t tx_frame[32] = {0};

      tx_frame[0] = CMD_FIRST_BYTE;
      tx_frame[1] = CMD_SECOND_BYTE;
      tx_frame[2] = CMD_GET_DEVICE_NAME;
      tx_frame[3] = name_len;
      memcpy(&tx_frame[4], name, name_len);

      set_frame_checksum(tx_frame, 5 + name_len);
      send_frame(tx_frame, 5 + name_len);
      LOG_INF("Sent device name: %s", name);
    }
    break;

  case CMD_SET_DEVICE_NAME:
    /* Set device name - not fully supported on Slim, just ACK */
    LOG_INF("Set device name (not supported on Slim)");
    {
      uint8_t tx_frame[8] = {0};
      tx_frame[0] = CMD_FIRST_BYTE;
      tx_frame[1] = CMD_SECOND_BYTE;
      tx_frame[2] = CMD_SET_DEVICE_NAME;
      tx_frame[3] = 0x01;
      tx_frame[4] = 0x01; /* ACK */
      set_frame_checksum(tx_frame, 6);
      send_frame(tx_frame, 6);
    }
    break;

  case CMD_GET_SENSOR_DETAILS:
    /* Return sensor details - Slim uses STCC4, not SCD4x */
    {
      uint8_t tx_frame[32] = {0};
      sensor_data_t data;
      sensor_manager_get_data(&data);

      tx_frame[0] = CMD_FIRST_BYTE;
      tx_frame[1] = CMD_SECOND_BYTE;
      tx_frame[2] = CMD_GET_SENSOR_DETAILS;
      tx_frame[3] = 16; /* Payload length */

      int offset = 4;

      /* Temperature Offset (uint16_t, 2 bytes, big-endian) - default 0 */
      tx_frame[offset++] = 0x00;
      tx_frame[offset++] = 0x00;

      /* Sensor Altitude (uint16_t, 2 bytes, big-endian) - default 0 */
      tx_frame[offset++] = 0x00;
      tx_frame[offset++] = 0x00;

      /* Ambient Pressure (uint16_t, mBar, big-endian) - from LPS22HH */
      uint16_t pressure_mbar = (uint16_t)data.pressure_hpa;
      tx_frame[offset++] = (uint8_t)(pressure_mbar >> 8);
      tx_frame[offset++] = (uint8_t)(pressure_mbar & 0xFF);

      /* ASC Enabled (1 byte) - STCC4 doesn't support this */
      tx_frame[offset++] = 0x00;

      /* ASC Target (uint16_t, 2 bytes) */
      tx_frame[offset++] = 0x00;
      tx_frame[offset++] = 0x00;

      /* Serial Number (6 bytes) - placeholder for Slim */
      tx_frame[offset++] = 'S';
      tx_frame[offset++] = 'L';
      tx_frame[offset++] = 'I';
      tx_frame[offset++] = 'M';
      tx_frame[offset++] = '0';
      tx_frame[offset++] = '1';

      /* Sensor Variant (1 byte) - 2 = STCC4 (custom) */
      tx_frame[offset++] = 0x02;

      set_frame_checksum(tx_frame, offset + 1);
      send_frame(tx_frame, offset + 1);
      LOG_INF("Sent sensor details");
    }
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

  case CMD_RESET_SENSOR:
    /* Reset CO2 sensor (STCC4) */
    LOG_INF("Reset sensor command received");
    sensor_manager_reset_co2_sensor();
    /* ACK with same command */
    {
      uint8_t tx_frame[8] = {0};
      tx_frame[0] = CMD_FIRST_BYTE;
      tx_frame[1] = CMD_SECOND_BYTE;
      tx_frame[2] = CMD_RESET_SENSOR;
      tx_frame[3] = 0x01;
      tx_frame[4] = 0x01; /* Success */
      set_frame_checksum(tx_frame, 6);
      send_frame(tx_frame, 6);
    }
    break;

  case CMD_FIND_MY_AIRSPOT:
    /* Blink LEDs to locate device */
    LOG_INF("Find My AirSpot command received");
    led_driver_startup_sequence(); /* Re-use startup blink */
    break;

  case CMD_SET_BLE_DISCONNECT:
    /* Force BLE disconnect */
    LOG_INF("Force disconnect command received");
    ble_service_disconnect();
    break;

  case CMD_SET_DND:
    /* Set DND mode - just store settings for now */
    /* payload: [enabled(1), startH(1), startM(1), endH(1), endM(1)] */
    if (len >= 9) {
      uint8_t enabled = frame[4];
      uint8_t start_h = frame[5];
      uint8_t start_m = frame[6];
      uint8_t end_h = frame[7];
      uint8_t end_m = frame[8];

      user_settings_set_dnd(enabled, start_h, start_m, end_h, end_m);
      LOG_INF("DND set: %d, %02d:%02d-%02d:%02d", enabled, start_h, start_m,
              end_h, end_m);
    }
    protocol_send_device_state();
    break;

  case CMD_ENTER_FACTORY_TEST:
    /* Enter factory test mode */
    LOG_INF("Enter Factory Test Mode command received");
    /* Ack first */
    {
      uint8_t tx_frame[8] = {0};
      tx_frame[0] = CMD_FIRST_BYTE;
      tx_frame[1] = CMD_SECOND_BYTE;
      tx_frame[2] = CMD_ENTER_FACTORY_TEST;
      tx_frame[3] = 0x01;
      tx_frame[4] = 0x01; /* Success */
      set_frame_checksum(tx_frame, 6);
      send_frame(tx_frame, 6);
    }

    /* Start test sequence */
    factory_test_start_auto();
    break;

  case CMD_FACTORY_TEST_RESULT:
    /* Send last test report */
    {
      factory_test_report_t report;
      if (factory_test_get_report(&report) == 0) {
        uint8_t tx_frame[32] = {0};
        tx_frame[0] = CMD_FIRST_BYTE;
        tx_frame[1] = CMD_SECOND_BYTE;
        tx_frame[2] = CMD_FACTORY_TEST_RESULT; /* 0xDD */
        tx_frame[3] = 10;                      /* Fixed length */

        tx_frame[4] = report.flash_status;
        tx_frame[5] = report.battery_status;
        tx_frame[6] = report.sht40_status;
        tx_frame[7] = report.stcc4_status;
        tx_frame[8] = report.lps22hh_status;
        tx_frame[9] = report.qi_status;
        tx_frame[10] = (uint8_t)(report.battery_mv >> 8);
        tx_frame[11] = (uint8_t)(report.battery_mv & 0xFF);
        tx_frame[12] = 0; /* Reserved */
        tx_frame[13] = 0; /* Reserved */

        set_frame_checksum(tx_frame, 15);
        send_frame(tx_frame, 15);
      }
    }
    break;

  case CMD_SET_DEVICE_SLEEP:
    /* Enter Power Off (shipping) mode */
    LOG_INF("Device sleep (power off) command received");
    /* Send ACK first */
    {
      uint8_t tx_frame[8] = {0};
      tx_frame[0] = CMD_FIRST_BYTE;
      tx_frame[1] = CMD_SECOND_BYTE;
      tx_frame[2] = CMD_SET_DEVICE_SLEEP;
      tx_frame[3] = 0x01;
      tx_frame[4] = 0x01; /* ACK */
      set_frame_checksum(tx_frame, 6);
      send_frame(tx_frame, 6);
    }
    /* Small delay for ACK to send */
    k_msleep(100);
    /* Enter power off - does not return */
    power_manager_enter_off_state();
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
  /* NOTE: Old firmware has tx_frame[3] = 0x02 (bug) but app expects it! */
  tx_frame[3] = 0x02;

  if (sensor_manager_get_data(&data) == 0 && data.valid) {
    /* Timestamp (32-bit, big-endian) - use synced time if available */
    uint32_t time;
    if (user_settings_is_time_set()) {
      time = user_settings_get_time_now();
    } else {
      time = k_uptime_get_32() / 1000;
    }
    tx_frame[4] = (uint8_t)(time >> 24);
    tx_frame[5] = (uint8_t)(time >> 16);
    tx_frame[6] = (uint8_t)(time >> 8);
    tx_frame[7] = (uint8_t)(time);

    /* CO2 value (16-bit, big-endian) */
    tx_frame[8] = (uint8_t)(data.co2_ppm >> 8);
    tx_frame[9] = (uint8_t)(data.co2_ppm & 0xFF);
  } else {
    /* Error state: 0xFFFF for CO2, error info in time bytes */
    tx_frame[4] = 0x00; /* error code placeholder */
    tx_frame[5] = 0x00; /* recovery attempts placeholder */
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
  /* NOTE: Old firmware has tx_frame[3] = 0x01 but sends 2 bytes (bug) */
  tx_frame[3] = 0x01;

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
  /*
   * Device state payload format (must match old firmware for app
   * compatibility):
   * - Buzzer state (1 byte)
   * - Vibrator state (1 byte)
   * - Power mode (1 byte)
   * - Yellow start (2 bytes) - N/A for Slim, send defaults
   * - Red start (2 bytes) - N/A for Slim, send defaults
   * - Screen const on (1 byte) - N/A for Slim
   * - Auto calibrate (1 byte) - N/A for STCC4
   * - DND mode (1 byte)
   * - DND start hour/min (2 bytes)
   * - DND end hour/min (2 bytes)
   * - Calib target (2 bytes) - N/A
   * - UI mode (1 byte) - N/A
   * - Graph max (2 bytes) - N/A
   * - Graph min (2 bytes) - N/A
   * - Illuminate screen on alarm (1 byte) - N/A
   * - Alarm on CO2 falling (1 byte) - N/A
   * - 10 advanced alarms (4 bytes each = 40 bytes) - N/A
   * - CO2 scale factor (4 bytes) - use 1.0
   * - Flight mode (1 byte)
   */
  uint8_t tx_frame[80] = {0};
  int offset = 4;

  /* Basic settings */
  tx_frame[offset++] = user_settings_get_buzzer();   /* buzzer state */
  tx_frame[offset++] = user_settings_get_vibrator(); /* vibrator state */
  tx_frame[offset++] = user_settings_get_power_mode();

  /* Yellow/Red thresholds - defaults for Slim (800/1200 ppm) */
  uint16_t yellow_start = 800;
  uint16_t red_start = 1200;
  tx_frame[offset++] = (uint8_t)(yellow_start >> 8);
  tx_frame[offset++] = (uint8_t)(yellow_start & 0xFF);
  tx_frame[offset++] = (uint8_t)(red_start >> 8);
  tx_frame[offset++] = (uint8_t)(red_start & 0xFF);

  /* Screen/Display settings - N/A for Slim */
  tx_frame[offset++] = 0; /* screen const on */
  tx_frame[offset++] = 0; /* auto calibrate - STCC4 doesn't support ASC */

  /* DND settings */
  tx_frame[offset++] = 0; /* dnd mode off */
  tx_frame[offset++] = 0; /* dnd start hour */
  tx_frame[offset++] = 0; /* dnd start minute */
  tx_frame[offset++] = 0; /* dnd end hour */
  tx_frame[offset++] = 0; /* dnd end minute */

  /* Calibration target */
  uint16_t calib_target = 400;
  tx_frame[offset++] = (uint8_t)(calib_target >> 8);
  tx_frame[offset++] = (uint8_t)(calib_target & 0xFF);

  /* UI/Graph settings - N/A for Slim */
  tx_frame[offset++] = 0; /* ui mode */
  uint16_t graph_max = 2000;
  uint16_t graph_min = 400;
  tx_frame[offset++] = (uint8_t)(graph_max >> 8);
  tx_frame[offset++] = (uint8_t)(graph_max & 0xFF);
  tx_frame[offset++] = (uint8_t)(graph_min >> 8);
  tx_frame[offset++] = (uint8_t)(graph_min & 0xFF);

  /* Alarm settings - N/A for Slim */
  tx_frame[offset++] = 0; /* illuminate screen on alarm */
  tx_frame[offset++] = 0; /* alarm on CO2 falling */

  /* 10 advanced alarms (4 bytes each: co2_level[2], repeats[1], enabled[1]) */
  for (int i = 0; i < 10; i++) {
    tx_frame[offset++] = 0; /* co2_level high */
    tx_frame[offset++] = 0; /* co2_level low */
    tx_frame[offset++] = 0; /* alarm_repeats */
    tx_frame[offset++] = 0; /* notification_enabled */
  }

  /* CO2 scale factor (float as 4 bytes) - default 1.0f */
  float scale_factor = 1.0f;
  uint32_t scale_bits;
  memcpy(&scale_bits, &scale_factor, sizeof(scale_bits));
  tx_frame[offset++] = (uint8_t)(scale_bits >> 24);
  tx_frame[offset++] = (uint8_t)(scale_bits >> 16);
  tx_frame[offset++] = (uint8_t)(scale_bits >> 8);
  tx_frame[offset++] = (uint8_t)(scale_bits);

  /* Flight mode */
  tx_frame[offset++] = user_settings_get_flight_mode();

  /* Set header */
  tx_frame[0] = CMD_FIRST_BYTE;
  tx_frame[1] = CMD_SECOND_BYTE;
  tx_frame[2] = CMD_GET_DEVICE_STATE;
  tx_frame[3] = offset - 4; /* payload length */

  set_frame_checksum(tx_frame, offset + 1);
  send_frame(tx_frame, offset + 1);

  LOG_INF("Device state sent: %d bytes payload", offset - 4);
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

  LOG_DBG("Sending current half-page: %d (0x%04X)", half_page, half_page);

  tx_frame[0] = CMD_FIRST_BYTE;
  tx_frame[1] = CMD_SECOND_BYTE;
  /* Response uses CMD_GET_HISTORY_PAGE (0x0C) like old firmware */
  tx_frame[2] = CMD_HISTORY_REQUEST;
  /* NOTE: Old firmware has tx_frame[3] = 0x01 but sends 2 bytes (bug) */
  tx_frame[3] = 0x01;
  tx_frame[4] = (uint8_t)(half_page >> 8);
  tx_frame[5] = (uint8_t)(half_page & 0xFF);

  set_frame_checksum(tx_frame, 7);
  send_frame(tx_frame, 7);
}

void protocol_send_history_data(const uint8_t *data, uint8_t record_count,
                                uint16_t half_page_number) {
  /*
   * IMPORTANT: Must match old firmware format exactly!
   * Old firmware always sends exactly 16 records (128 bytes) regardless of
   * how many are valid. frame[3] is always 128 (0x80 = 16 * 8).
   *
   * Frame format:
   *   [0xFF][0xAA][0x0C][0x80][16 records * 8 bytes][page_num(2)][checksum]
   * Total: 4 + 128 + 2 + 1 = 135 bytes
   */
  static uint8_t tx_frame[140];

  const uint8_t record_size = 8;    /* sizeof(record_t) */
  const uint8_t records_to_send = 16; /* Always send 16 records like old FW */

  LOG_DBG("Sending history: %d valid records for half-page %d", record_count,
          half_page_number);

  /* Build frame header */
  tx_frame[0] = CMD_FIRST_BYTE;
  tx_frame[1] = CMD_SECOND_BYTE;
  tx_frame[2] = CMD_HISTORY_REQUEST;
  tx_frame[3] = records_to_send * record_size; /* Always 128 bytes (0x80) */

  /* Copy all 16 records (valid or not - matches old firmware behavior) */
  uint8_t offset = 4;
  for (uint8_t i = 0; i < records_to_send; i++) {
    const record_t *rec = (const record_t *)&data[i * record_size];

    /* Timestamp (big-endian) */
    tx_frame[offset++] = (uint8_t)(rec->timestamp >> 24);
    tx_frame[offset++] = (uint8_t)(rec->timestamp >> 16);
    tx_frame[offset++] = (uint8_t)(rec->timestamp >> 8);
    tx_frame[offset++] = (uint8_t)(rec->timestamp);

    /* Value (little-endian in frame - LSB first, matches old firmware) */
    tx_frame[offset++] = (uint8_t)(rec->value & 0xFF);
    tx_frame[offset++] = (uint8_t)(rec->value >> 8);

    /* Type */
    tx_frame[offset++] = rec->type;

    /* Reserved */
    tx_frame[offset++] = 0;
  }

  /* Append half-page number (big-endian) */
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
