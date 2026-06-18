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
#include <drivers/stcc4.h>

#include <airspot/version.h>
#include <services/led_indicator.h>
#include <services/airspot_nus.h>
#include <services/ble_mode_service.h>
#include <services/ble_service.h>
#include <services/calibration_service.h>
#include <services/data_log.h>
#include <services/factory_test.h>
#include <services/power_manager.h>
#include <services/protocol.h>
#include <services/sensor_manager.h>
#include <services/user_settings.h>

LOG_MODULE_REGISTER(protocol, CONFIG_AIRSPOT_LOG_LEVEL);

/* Frame buffer for receiving partial frames */
#define RX_FRAME_MAX_LEN 64
static uint8_t rx_frame[RX_FRAME_MAX_LEN];
static uint8_t rx_frame_idx = 0;

/*
 * Command dispatch runs on a dedicated thread, NOT in the NUS RX callback.
 * The RX callback executes in the Bluetooth RX thread; running long/blocking
 * handlers there (sensor measurement ~550ms, factory test, etc.) stalls all
 * GATT/ATT processing and delays the ATT write-response back to the app.
 * Instead we copy each complete frame into a queue and process it here.
 */
typedef struct {
  uint8_t data[RX_FRAME_MAX_LEN];
  uint16_t len;
} protocol_frame_t;

#define PROTOCOL_QUEUE_DEPTH 8
K_MSGQ_DEFINE(protocol_msgq, sizeof(protocol_frame_t), PROTOCOL_QUEUE_DEPTH, 4);

/* Roomy enough for the heaviest handler (factory test: sensor reads +
 * float-formatted logging). */
#define PROTOCOL_THREAD_STACK_SIZE 3072
#define PROTOCOL_THREAD_PRIORITY 6
static K_THREAD_STACK_DEFINE(protocol_thread_stack, PROTOCOL_THREAD_STACK_SIZE);
static struct k_thread protocol_thread_data;

/* Hardware/Software version strings */
#define HARDWARE_VERSION "SLIM_1.0"
#define SOFTWARE_VERSION AIRSPOT_FIRMWARE_VERSION_STRING

static void protocol_calib_on_start(void);
static void protocol_calib_on_countdown(uint16_t seconds_remaining);
static void protocol_calib_on_done(int16_t correction);

/* Forward declarations */
static void frame_received_handler(const uint8_t *frame, uint16_t len);
static void set_frame_checksum(uint8_t *frame, uint16_t len);
static uint8_t verify_frame_checksum(const uint8_t *frame, uint16_t len);

/* NUS RX callback - accumulates data into frames */
static void nus_rx_callback(const uint8_t *data, uint16_t len)
{
  for (uint16_t i = 0; i < len; i++)
  {
    if (rx_frame_idx >= RX_FRAME_MAX_LEN)
    {
      LOG_WRN("RX frame overflow, resetting");
      rx_frame_idx = 0;
    }

    rx_frame[rx_frame_idx++] = data[i];

    /* Check for valid frame header */
    if (rx_frame_idx == 1 && rx_frame[0] != CMD_FIRST_BYTE)
    {
      rx_frame_idx = 0;
      continue;
    }
    if (rx_frame_idx == 2 && rx_frame[1] != CMD_SECOND_BYTE)
    {
      rx_frame_idx = 0;
      continue;
    }

    /* Check if frame is complete: 4 header + payload + 1 checksum */
    if (rx_frame_idx >= 4)
    {
      uint16_t expected_len = 4 + rx_frame[3] + 1;
      if (rx_frame_idx == expected_len)
      {
        /* Hand the complete frame to the worker thread; never dispatch
         * (and never block) inside the BLE RX callback. */
        protocol_frame_t frame;
        frame.len = rx_frame_idx;
        memcpy(frame.data, rx_frame, rx_frame_idx);
        if (k_msgq_put(&protocol_msgq, &frame, K_NO_WAIT) != 0)
        {
          LOG_WRN("Protocol command queue full, dropping frame");
        }
        rx_frame_idx = 0;
      }
    }
  }
}

/* Worker thread: drains the command queue and dispatches frames. Blocking
 * here is safe — it does not hold up the Bluetooth RX thread. */
static void protocol_thread_fn(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  protocol_frame_t frame;
  while (1)
  {
    if (k_msgq_get(&protocol_msgq, &frame, K_FOREVER) == 0)
    {
      frame_received_handler(frame.data, frame.len);
    }
  }
}

/**
 * @brief Initialize the protocol layer and bind to NUS RX.
 *
 * @return 0 on success, negative errno on failure.
 */
int protocol_init(void)
{
  int ret = airspot_nus_init(nus_rx_callback);
  if (ret)
  {
    LOG_ERR("Failed to init NUS: %d", ret);
    return ret;
  }

  calibration_callbacks_t calib_cb = {
      .on_start = protocol_calib_on_start,
      .on_countdown = protocol_calib_on_countdown,
      .on_done = protocol_calib_on_done,
  };
  calibration_service_init(&calib_cb);

  /* Start the command-dispatch worker thread */
  k_tid_t tid = k_thread_create(
      &protocol_thread_data, protocol_thread_stack,
      K_THREAD_STACK_SIZEOF(protocol_thread_stack), protocol_thread_fn, NULL,
      NULL, NULL, PROTOCOL_THREAD_PRIORITY, 0, K_NO_WAIT);
  k_thread_name_set(tid, "protocol");

  LOG_INF("Protocol handler initialized");
  return 0;
}

/**
 * @brief Calculate and set checksum for a frame
 */
static void set_frame_checksum(uint8_t *frame, uint16_t len)
{
  uint32_t sum = 0;
  for (uint16_t i = 0; i < len - 1; i++)
  {
    sum += frame[i];
  }
  frame[len - 1] = (uint8_t)(sum & 0xFF);
}

/**
 * @brief Verify frame checksum
 */
static uint8_t verify_frame_checksum(const uint8_t *frame, uint16_t len)
{
  uint32_t sum = 0;
  for (uint16_t i = 0; i < len - 1; i++)
  {
    sum += frame[i];
  }
  return ((uint8_t)(sum & 0xFF)) == frame[len - 1];
}

/**
 * @brief Send a frame via NUS
 */
static void send_frame(const uint8_t *frame, uint16_t len)
{
  int ret = airspot_nus_send(frame, len);
  if (ret)
  {
    LOG_WRN("Failed to send frame: %d", ret);
  }
}

static uint16_t apply_co2_correction(uint16_t raw_ppm)
{
  return user_settings_apply_co2_correction(raw_ppm);
}

static void protocol_send_recali_start(void)
{
  uint8_t tx_frame[8] = {0};
  tx_frame[0] = CMD_FIRST_BYTE;
  tx_frame[1] = CMD_SECOND_BYTE;
  tx_frame[2] = CMD_CALIB_START;
  tx_frame[3] = 0x01;
  tx_frame[4] = 0x02; /* Calibration started */
  set_frame_checksum(tx_frame, 6);
  send_frame(tx_frame, 6);
}

static void protocol_send_recali_done(int16_t cc)
{
  uint8_t tx_frame[8] = {0};
  uint16_t cc_abs = (uint16_t)(cc < 0 ? -cc : cc);

  tx_frame[0] = CMD_FIRST_BYTE;
  tx_frame[1] = CMD_SECOND_BYTE;
  tx_frame[2] = CMD_CALIB_START;
  tx_frame[3] = 0x01;
  tx_frame[4] = (uint8_t)(cc_abs >> 8);
  tx_frame[5] = (uint8_t)(cc_abs & 0xFF);
  tx_frame[6] = (cc < 0) ? 0x01 : 0x00;

  set_frame_checksum(tx_frame, 8);
  send_frame(tx_frame, 8);
}

static void protocol_send_recali_countdown(uint16_t countdown)
{
  uint8_t tx_frame[8] = {0};
  tx_frame[0] = CMD_FIRST_BYTE;
  tx_frame[1] = CMD_SECOND_BYTE;
  tx_frame[2] = CMD_RECALI_COUNTDOWN;
  tx_frame[3] = 0x01;
  tx_frame[4] = (uint8_t)(countdown >> 8);
  tx_frame[5] = (uint8_t)(countdown & 0xFF);
  set_frame_checksum(tx_frame, 7);
  send_frame(tx_frame, 7);
}

static void protocol_calib_on_start(void)
{
  led_indicator_show_calibration(true);
  protocol_send_recali_start();
}

static void protocol_calib_on_countdown(uint16_t seconds_remaining)
{
  protocol_send_recali_countdown(seconds_remaining);
}

static void protocol_calib_on_done(int16_t correction)
{
  protocol_send_recali_done(correction);
  led_indicator_show_calibration(false);
}

/**
 * @brief Handle a complete received frame
 */
static void frame_received_handler(const uint8_t *frame, uint16_t len)
{
  if (!verify_frame_checksum(frame, len))
  {
    LOG_WRN("Frame checksum failed");
    return;
  }

  uint8_t cmd = frame[2];
  LOG_DBG("Received cmd: 0x%02X, len: %d", cmd, len);

  switch (cmd)
  {
  case CMD_GET_CO2_VALUE:
    protocol_send_realtime_co2();
    break;

  case CMD_REFRESH_CO2_VALUE:
    /* Trigger immediate measurement then send */
    if (calibration_service_is_active())
    {
      LOG_WRN("Calibration active, skipping immediate measurement");
      break;
    }
    sensor_manager_measure_now();
    protocol_send_realtime_co2();
    break;

  case CMD_GET_DEVICE_STATE:
    protocol_send_device_state();
    break;

  case CMD_GET_DEVICE_NAME:
    /* Return device alias (user-settable name, separate from BLE name)
     * This is what the app displays, not the BLE advertising name */
    {
      const char *alias = user_settings_get_device_name();
      uint8_t alias_len = (alias && alias[0]) ? strlen(alias) : 0;
      uint8_t tx_frame[32] = {0};

      tx_frame[0] = CMD_FIRST_BYTE;
      tx_frame[1] = CMD_SECOND_BYTE;
      tx_frame[2] = CMD_GET_DEVICE_NAME;
      tx_frame[3] = alias_len;
      if (alias_len > 0)
      {
        memcpy(&tx_frame[4], alias, alias_len);
      }

      set_frame_checksum(tx_frame, 5 + alias_len);
      send_frame(tx_frame, 5 + alias_len);
      LOG_INF("Sent device alias: %s", alias_len > 0 ? alias : "(empty)");
    }
    break;

  case CMD_SET_DEVICE_NAME:
    /* Set device alias (user-friendly name for app display)
     * Frame format: [FF][AA][0A][len][name...][checksum]
     * Note: This does NOT change the BLE advertising name (AirSpot-XXXXXX) */
    if (len >= 5)
    {
      uint8_t name_len = frame[3];
      if (name_len > 0 && name_len <= 20 && len >= (4 + name_len + 1))
      {
        char new_alias[21] = {0};
        memcpy(new_alias, &frame[4], name_len);
        new_alias[name_len] = '\0';

        /* Save alias to NVS */
        user_settings_set_device_name(new_alias);
        LOG_INF("Device alias set: %s", new_alias);
      }
    }
    /* Send ACK */
    {
      uint8_t tx_frame[8] = {0};
      tx_frame[0] = CMD_FIRST_BYTE;
      tx_frame[1] = CMD_SECOND_BYTE;
      tx_frame[2] = CMD_SET_DEVICE_NAME;
      tx_frame[3] = 0x01;
      tx_frame[4] = 0x01; /* Success */
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

      /* ASC Enabled (1 byte) */
      tx_frame[offset++] = user_settings_get_auto_calibrate();

      /* ASC Target (uint16_t, 2 bytes) */
      uint16_t calib_target = user_settings_get_calib_target();
      tx_frame[offset++] = (uint8_t)(calib_target >> 8);
      tx_frame[offset++] = (uint8_t)(calib_target & 0xFF);

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
      if (user_settings_is_time_set())
      {
        time = user_settings_get_time_now();
      }
      else
      {
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
    if (len >= 9)
    {
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
    if (len >= 6)
    {
      user_settings_set_buzzer(frame[4]);
      LOG_INF("Buzzer set: %d (stub)", frame[4]);
    }
    protocol_send_device_state();
    break;

  case CMD_SET_VIBRATOR_STATE:
    if (len >= 6)
    {
      user_settings_set_vibrator(frame[4]);
      LOG_INF("Vibrator set: %d (stub)", frame[4]);
    }
    protocol_send_device_state();
    break;

  case CMD_SET_POWER_MODE:
    if (len >= 6)
    {
      user_settings_set_power_mode(frame[4]);
      sensor_manager_set_power_mode(frame[4]);
      LOG_INF("Power mode set: %d", frame[4]);
    }
    protocol_send_device_state();
    break;

  case CMD_SET_FLIGHT_MODE:
    if (len >= 6)
    {
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
    if (len >= 7)
    {
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
    {
      int ret = sensor_manager_reset_co2_sensor();
      if (ret)
      {
        LOG_WRN("CO2 sensor reset failed: %d", ret);
      }
      else
      {
        /* Trigger a measurement to verify sensor is responding */
        ret = sensor_manager_measure_now();
        if (ret)
        {
          LOG_WRN("Post-reset measurement failed: %d", ret);
        }
      }
    }
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
    /* Blink LEDs rapidly to help locate the device */
    LOG_INF("Find My AirSpot command received");
    /* Send ACK first */
    {
      uint8_t tx_frame[8] = {0};
      tx_frame[0] = CMD_FIRST_BYTE;
      tx_frame[1] = CMD_SECOND_BYTE;
      tx_frame[2] = CMD_FIND_MY_AIRSPOT;
      tx_frame[3] = 0x01;
      tx_frame[4] = 0x01; /* ACK */
      set_frame_checksum(tx_frame, 6);
      send_frame(tx_frame, 6);
    }
    /* Rapid white flash for ~5s (non-blocking LED indicator) */
    led_indicator_show_find_device();
    break;

  case CMD_SET_GRAPH_YELLOW_AND_RED_START:
    if (frame[3] >= 4 && len >= 9)
    {
      uint16_t yellow_start =
          (uint16_t)((frame[4] << 8) | frame[5]);
      uint16_t red_start = (uint16_t)((frame[6] << 8) | frame[7]);

      if (user_settings_set_co2_thresholds(yellow_start, red_start) == 0)
      {
        LOG_INF("CO2 LED thresholds set: yellow=%u red=%u", yellow_start,
                red_start);
      }
      else
      {
        LOG_WRN("Invalid CO2 thresholds: yellow=%u red=%u", yellow_start,
                red_start);
      }
      protocol_send_device_state();
    }
    break;

  case CMD_SET_BLE_DISCONNECT:
    /* Force BLE disconnect */
    LOG_INF("Force disconnect command received");
    ble_service_disconnect();
    break;

  case CMD_SET_DND:
    /* Set DND mode - just store settings for now */
    /* payload: [enabled(1), startH(1), startM(1), endH(1), endM(1)] */
    if (len >= 9)
    {
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
      if (factory_test_get_report(&report) == 0)
      {
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

  case CMD_FACTORY_RESET:
    /* Factory reset - clear all settings and history, then reboot */
    LOG_WRN("Factory reset command received");
    /* Send ACK first */
    {
      uint8_t tx_frame[8] = {0};
      tx_frame[0] = CMD_FIRST_BYTE;
      tx_frame[1] = CMD_SECOND_BYTE;
      tx_frame[2] = CMD_FACTORY_RESET;
      tx_frame[3] = 0x01;
      tx_frame[4] = 0x01; /* ACK */
      set_frame_checksum(tx_frame, 6);
      send_frame(tx_frame, 6);
    }
    k_msleep(100);
    /* Clear user settings */
    user_settings_factory_reset();
    /* Erase history data */
    data_log_request_erase_all();
    k_msleep(500); /* Allow erase to start */
    /* Reboot */
    LOG_WRN("Factory reset complete - rebooting");
    sys_reboot(SYS_REBOOT_COLD);
    break;

  case CMD_CALIB_START:
    /* 0x0D len 0: start calibration; len 2: set target only (mobile app) */
    {
      uint16_t target = user_settings_get_calib_target();
      uint8_t payload_len = frame[3];

      if (payload_len >= 2 && len >= (uint16_t)(4 + payload_len + 1))
      {
        target = (uint16_t)((frame[4] << 8) | frame[5]);
        user_settings_set_calib_target(target);
        if (payload_len == 2)
        {
          LOG_INF("Calibration target set to %u ppm", target);
          break;
        }
      }

      if (payload_len == 0)
      {
        LOG_INF("Calibration start - target=%u ppm", target);
        (void)calibration_service_start(target);
      }
    }
    break;

  case CMD_SET_SELF_CALIB:
    /* Toggle ASC (mapped to STCC4 testing mode) */
    if (len >= 5)
    {
      uint8_t enabled = frame[4] ? 1 : 0;
      if (enabled)
      {
        (void)stcc4_disable_testing_mode();
      }
      else
      {
        (void)stcc4_enable_testing_mode();
      }
      user_settings_set_auto_calibrate(enabled);

      uint8_t tx_frame[8] = {0};
      tx_frame[0] = CMD_FIRST_BYTE;
      tx_frame[1] = CMD_SECOND_BYTE;
      tx_frame[2] = CMD_SET_SELF_CALIB;
      tx_frame[3] = 0x01;
      tx_frame[4] = 0x02; /* success */
      set_frame_checksum(tx_frame, 6);
      send_frame(tx_frame, 6);
    }
    break;

  case CMD_GET_ASC_COUNT:
    /* Return last correction value (legacy behavior) */
    {
      int16_t correction = user_settings_get_co2_correction();
      uint16_t correction_abs = (uint16_t)(correction < 0 ? -correction : correction);

      uint8_t tx_frame[8] = {0};
      tx_frame[0] = CMD_FIRST_BYTE;
      tx_frame[1] = CMD_SECOND_BYTE;
      tx_frame[2] = CMD_GET_ASC_COUNT;
      tx_frame[3] = 0x01; /* legacy length */
      tx_frame[4] = (uint8_t)(correction_abs >> 8);
      tx_frame[5] = (uint8_t)(correction_abs & 0xFF);
      tx_frame[6] = (correction < 0) ? 0x01 : 0x00;
      set_frame_checksum(tx_frame, 8);
      send_frame(tx_frame, 8);
    }
    break;

  case CMD_SET_ASC_CHECK_DURATION:
    /* Not implemented for STCC4; ignore for compatibility */
    LOG_INF("Set ASC check duration (ignored)");
    break;

  case CMD_GET_ASC_DAY_COUNT:
  {
    uint8_t tx_frame[8] = {0};
    tx_frame[0] = CMD_FIRST_BYTE;
    tx_frame[1] = CMD_SECOND_BYTE;
    tx_frame[2] = CMD_GET_ASC_DAY_COUNT;
    tx_frame[3] = 0x01;
    tx_frame[4] = user_settings_get_asc_day_count();
    set_frame_checksum(tx_frame, 6);
    send_frame(tx_frame, 6);
  }
  break;

  case CMD_GET_SENSOR_VARIANT:
    /* Return sensor variant - 0x02 for STCC4 (thermal conductivity) */
    LOG_INF("Get sensor variant");
    {
      uint8_t tx_frame[8] = {0};
      tx_frame[0] = CMD_FIRST_BYTE;
      tx_frame[1] = CMD_SECOND_BYTE;
      tx_frame[2] = CMD_GET_SENSOR_VARIANT;
      tx_frame[3] = 0x01;
      tx_frame[4] = 0x02; /* STCC4 = variant 2 (vs SCD4x = variant 1) */
      set_frame_checksum(tx_frame, 6);
      send_frame(tx_frame, 6);
    }
    break;

  case CMD_SET_CO2_SCALE_FACTOR:
    /* CO2 scale factor - STCC4 is factory calibrated, just ACK */
    LOG_INF("Set CO2 scale factor (ignored - STCC4 pre-calibrated)");
    {
      uint8_t tx_frame[8] = {0};
      tx_frame[0] = CMD_FIRST_BYTE;
      tx_frame[1] = CMD_SECOND_BYTE;
      tx_frame[2] = CMD_SET_CO2_SCALE_FACTOR;
      tx_frame[3] = 0x01;
      tx_frame[4] = 0x01; /* ACK */
      set_frame_checksum(tx_frame, 6);
      send_frame(tx_frame, 6);
    }
    break;

  default:
    LOG_WRN("Unknown command: 0x%02X", cmd);
    break;
  }
}

/**
 * @brief Send realtime CO2 notification frame.
 */
void protocol_send_realtime_co2(void)
{
  uint8_t tx_frame[20] = {0};
  sensor_data_t data;

  tx_frame[0] = CMD_FIRST_BYTE;
  tx_frame[1] = CMD_SECOND_BYTE;
  tx_frame[2] = CMD_GET_CO2_VALUE;
  /* NOTE: Old firmware had tx_frame[3] = 0x02 (bug).
   * New format: Time(4) + CO2(2) + Temp(2) + Hum(2) = 10 bytes (0x0A)
   */
  tx_frame[3] = 0x0A;

  if (sensor_manager_get_data(&data) == 0 && data.valid)
  {
    /* Timestamp (32-bit, big-endian) - use synced time if available */
    uint32_t time;
    if (user_settings_is_time_set())
    {
      time = user_settings_get_time_now();
    }
    else
    {
      time = k_uptime_get_32() / 1000;
    }
    tx_frame[4] = (uint8_t)(time >> 24);
    tx_frame[5] = (uint8_t)(time >> 16);
    tx_frame[6] = (uint8_t)(time >> 8);
    tx_frame[7] = (uint8_t)(time);

    /* CO2 value (16-bit, big-endian) - apply calibration correction */
    uint16_t corrected = apply_co2_correction(data.co2_ppm);
    tx_frame[8] = (uint8_t)(corrected >> 8);
    tx_frame[9] = (uint8_t)(corrected & 0xFF);

    /* Temperature (16-bit, big-endian, x100) */
    int16_t temp = (int16_t)(data.temperature_c * 100.0f);
    tx_frame[10] = (uint8_t)(temp >> 8);
    tx_frame[11] = (uint8_t)(temp & 0xFF);

    /* Humidity (16-bit, big-endian, x100) */
    uint16_t hum = (uint16_t)(data.humidity_rh * 100.0f);
    tx_frame[12] = (uint8_t)(hum >> 8);
    tx_frame[13] = (uint8_t)(hum & 0xFF);
  }
  else
  {
    /* Error state: 0xFFFF for CO2, error info in time bytes */
    tx_frame[4] = 0x00; /* error code placeholder */
    tx_frame[5] = 0x00; /* recovery attempts placeholder */
    tx_frame[6] = 0x00;
    tx_frame[7] = 0x00;
    tx_frame[8] = 0xFF;
    tx_frame[9] = 0xFF;

    /* Invalid Temp/Hum */
    tx_frame[10] = 0x00;
    tx_frame[11] = 0x00;
    tx_frame[12] = 0x00;
    tx_frame[13] = 0x00;
  }

  set_frame_checksum(tx_frame, 15);
  send_frame(tx_frame, 15);
}

/**
 * @brief Send battery level + charging state.
 */
void protocol_send_battery_level(void)
{
  uint8_t tx_frame[16] = {0};
  sensor_data_t data;

  tx_frame[0] = CMD_FIRST_BYTE;
  tx_frame[1] = CMD_SECOND_BYTE;
  tx_frame[2] = CMD_GET_BATTERY_LEVEL;
  /* NOTE: Old firmware has tx_frame[3] = 0x01 but sends 2 bytes (bug) */
  tx_frame[3] = 0x01;

  if (sensor_manager_get_data(&data) == 0)
  {
    tx_frame[4] = data.battery_percent;
    tx_frame[5] = data.is_charging ? 1 : 0;
  }
  else
  {
    tx_frame[4] = 0;
    tx_frame[5] = 0;
  }

  set_frame_checksum(tx_frame, 7);
  send_frame(tx_frame, 7);
}

void protocol_send_low_battery_lockout(void)
{
  uint8_t tx_frame[16] = {0};
  sensor_data_t data;

  tx_frame[0] = CMD_FIRST_BYTE;
  tx_frame[1] = CMD_SECOND_BYTE;
  tx_frame[2] = CMD_LOW_BATTERY_LOCKOUT;
  tx_frame[3] = 0x02; /* payload length: battery %, charging flag */

  if (sensor_manager_get_data(&data) == 0)
  {
    tx_frame[4] = data.battery_percent;
    tx_frame[5] = data.is_charging ? 1 : 0;
  }

  set_frame_checksum(tx_frame, 7);
  send_frame(tx_frame, 7);
}

/**
 * @brief Send full device state payload for app synchronization.
 */
void protocol_send_device_state(void)
{
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

  uint16_t yellow_start = user_settings_get_co2_yellow_start();
  uint16_t red_start = user_settings_get_co2_red_start();
  tx_frame[offset++] = (uint8_t)(yellow_start >> 8);
  tx_frame[offset++] = (uint8_t)(yellow_start & 0xFF);
  tx_frame[offset++] = (uint8_t)(red_start >> 8);
  tx_frame[offset++] = (uint8_t)(red_start & 0xFF);

  /* Screen/Display settings - N/A for Slim */
  tx_frame[offset++] = 0; /* screen const on */
  tx_frame[offset++] = user_settings_get_auto_calibrate();

  /* DND settings - report the values actually stored in NVS */
  const user_settings_t *us = user_settings_get();
  tx_frame[offset++] = us->dnd_enabled;
  tx_frame[offset++] = us->dnd_start_hour;
  tx_frame[offset++] = us->dnd_start_minute;
  tx_frame[offset++] = us->dnd_end_hour;
  tx_frame[offset++] = us->dnd_end_minute;

  /* Calibration target */
  uint16_t calib_target = user_settings_get_calib_target();
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
  for (int i = 0; i < 10; i++)
  {
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

void protocol_send_hardware_version(void)
{
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

void protocol_send_software_version(void)
{
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

void protocol_send_current_half_page(void)
{
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
                                uint16_t half_page_number)
{
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

  const uint8_t record_size = 8;      /* sizeof(record_t) */
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
  for (uint8_t i = 0; i < records_to_send; i++)
  {
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

void protocol_send_erase_done(void)
{
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

/**
 * @brief Process pending history requests and erase operations.
 */
void protocol_process_history(void)
{
  /* Skip history processing in mesh mode - NUS is not available */
  if (ble_mode_get_current() == BLE_MODE_MESH)
  {
    return;
  }

  /* Handle erase all request */
  if (data_log_is_erase_pending())
  {
    LOG_INF("Processing history erase...");
    int ret = data_log_erase_all();
    if (ret == 0)
    {
      protocol_send_erase_done();
    }
    return;
  }

  /* Handle history data request */
  if (data_log_has_pending_request())
  {
    record_page_t page;
    uint8_t count;
    uint16_t half_page;

    int ret = data_log_process_request(&page, &count, &half_page);
    if (ret == 0)
    {
      protocol_send_history_data(page.buf, count, half_page);
    }
    else
    {
      LOG_WRN("History request failed: %d", ret);
    }
  }
}
