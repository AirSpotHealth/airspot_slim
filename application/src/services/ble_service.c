/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <services/airspot_nus.h>
#include <services/ble_service.h>
#include <services/led_indicator.h>

LOG_MODULE_REGISTER(ble_service, CONFIG_AIRSPOT_LOG_LEVEL);

/* Device name buffer - matches old firmware format: "AirSpot-XXXXXX" */
#define DEVICE_NAME_BASE "AirSpot"
#define DEVICE_NAME_MAX_LEN 20
static char device_name[DEVICE_NAME_MAX_LEN] = DEVICE_NAME_BASE;
static uint8_t device_name_len = sizeof(DEVICE_NAME_BASE) - 1;

static struct bt_conn *current_conn = NULL;
static bool initialized = false;
static bool advertising = false;

/* Work item for deferred advertising restart */
static void restart_advertising_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(restart_adv_work, restart_advertising_work_handler);

static void restart_advertising_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  ble_service_start_advertising();
}

/* Dynamic advertising data */
static struct bt_data ad[2];

/* SMP UUID in scan response only (MCUmgr disabled) — lets the mobile app
 * detect AirSpot Slim without exposing an OTA/DFU GATT service. */
#define SMP_SERVICE_UUID \
    BT_UUID_128_ENCODE(0x8D53DC1D, 0x1DB7, 0x4CD3, 0x868B, 0x8A527460AA84)

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, SMP_SERVICE_UUID),
};

/* BLE connection callbacks */
static void connected(struct bt_conn *conn, uint8_t err) {
  if (err) {
    LOG_ERR("Connection failed (err %u)", err);
    return;
  }

  LOG_INF("BLE Connected");
  current_conn = bt_conn_ref(conn);

  /* Notify NUS of connection for TX operations */
  airspot_nus_set_conn(conn);

  /* Stop pairing indicator */
  led_indicator_stop();
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
  LOG_INF("BLE Disconnected (reason %u)", reason);

  if (current_conn) {
    bt_conn_unref(current_conn);
    current_conn = NULL;
  }

  /* Notify NUS of disconnection */
  airspot_nus_set_conn(NULL);

  /* Restart advertising after disconnect with a small delay
   * to allow the BLE stack to fully release connection resources.
   * Direct call from callback can fail with -ENOMEM (-12). */
  advertising = false;
  k_work_schedule(&restart_adv_work, K_MSEC(100));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

/**
 * @brief Generate device name from BLE identity address
 *
 * Creates name in format "AirSpot-XXXXXX" where XXXXXX is derived
 * from the BLE MAC address (matching old firmware format).
 */
static void generate_device_name(void) {
  bt_addr_le_t addr;
  size_t count = 1;

  /* Get the BLE identity address */
  bt_id_get(&addr, &count);

  if (count > 0) {
    /* Format: AirSpot-XXYYZZ (last 3 bytes of MAC, reversed order like old FW)
     */
    snprintf(device_name, sizeof(device_name), "%s-%02X%02X%02X",
             DEVICE_NAME_BASE, addr.a.val[3], addr.a.val[4], addr.a.val[5]);
    device_name_len = strlen(device_name);
    LOG_INF("Device name: %s", device_name);
  } else {
    LOG_WRN("No BLE identity, using default name");
    strncpy(device_name, DEVICE_NAME_BASE, sizeof(device_name));
    device_name_len = sizeof(DEVICE_NAME_BASE) - 1;
  }
}

/**
 * @brief Set the GAP device name
 */
static int set_gap_device_name(void) {
  int err = bt_set_name(device_name);
  if (err) {
    LOG_ERR("Failed to set device name (err %d)", err);
    return err;
  }
  return 0;
}

int ble_service_init(void) {
  int err;

  /* Initialize Bluetooth */
  err = bt_enable(NULL);
  if (err) {
    LOG_ERR("Bluetooth init failed (err %d)", err);
    return err;
  }

  /* Generate BLE device name from MAC address (always "AirSpot-XXXXXX")
   * Note: The user-settable "device name" (alias) is separate and stored
   * in user_settings. The BLE advertising name is always MAC-based. */
  generate_device_name();

  /* Set GAP device name */
  err = set_gap_device_name();
  if (err) {
    LOG_WRN("Could not set GAP name (err %d)", err);
  }

  /* Build advertising data with dynamic name */
  ad[0] = (struct bt_data)BT_DATA_BYTES(BT_DATA_FLAGS,
                                        (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR));
  ad[1] = (struct bt_data)BT_DATA(BT_DATA_NAME_COMPLETE, device_name,
                                  device_name_len);

  initialized = true;
  LOG_INF("BLE service initialized");
  return 0;
}

const char *ble_service_get_device_name(void) { return device_name; }

bool ble_service_is_connected(void) { return (current_conn != NULL); }

void ble_service_disconnect(void) {
  if (current_conn) {
    bt_conn_disconnect(current_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
  }
}

/* Power-efficient advertising parameters:
 * Interval: 1000-2500ms (vs default 100-150ms)
 * This significantly reduces power consumption when not connected
 */
#define ADV_SLOW_INT_MIN 1600 /* 1000ms in 0.625ms units */
#define ADV_SLOW_INT_MAX 4000 /* 2500ms in 0.625ms units */

static const struct bt_le_adv_param adv_param_slow = {
    .id = BT_ID_DEFAULT,
    .sid = 0,
    .secondary_max_skip = 0,
    .options = BT_LE_ADV_OPT_CONN, /* Connectable + Scannable */
    .interval_min = ADV_SLOW_INT_MIN,
    .interval_max = ADV_SLOW_INT_MAX,
    .peer = NULL,
};

int ble_service_start_advertising(void) {
  int err;

  if (!initialized) {
    return -ENODEV;
  }

  if (advertising) {
    return 0; /* Already advertising */
  }

  /* Use slower advertising interval for power efficiency */
  err =
      bt_le_adv_start(&adv_param_slow, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
  if (err) {
    LOG_ERR("Advertising failed to start (err %d)", err);
    return err;
  }

  advertising = true;
  LOG_INF("BLE advertising started as: %s", device_name);
  return 0;
}

int ble_service_stop_advertising(void) {
  int err;

  if (!advertising) {
    return 0; /* Not advertising */
  }

  err = bt_le_adv_stop();
  if (err) {
    LOG_ERR("Advertising stop failed (err %d)", err);
    return err;
  }

  advertising = false;
  LOG_INF("BLE advertising stopped");
  return 0;
}

void ble_service_update_battery_level(uint8_t level) {
  /* Clamp to valid range 0-100 */
  if (level > 100) {
    level = 100;
  }

  /* Update BAS battery level characteristic */
  int err = bt_bas_set_battery_level(level);
  if (err) {
    LOG_WRN("Failed to update battery level: %d", err);
  }
}
