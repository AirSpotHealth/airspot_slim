/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <services/airspot_nus.h>
#include <services/ble_service.h>

LOG_MODULE_REGISTER(ble_service, CONFIG_AIRSPOT_LOG_LEVEL);

static struct bt_conn *current_conn = NULL;
static bool initialized = false;
static bool advertising = false;

/* BLE connection callbacks */
static void connected(struct bt_conn *conn, uint8_t err)
{
  if (err)
  {
    LOG_ERR("Connection failed (err %u)", err);
    return;
  }

  LOG_INF("BLE Connected");
  current_conn = bt_conn_ref(conn);

  /* Notify NUS of connection for TX operations */
  airspot_nus_set_conn(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
  LOG_INF("BLE Disconnected (reason %u)", reason);

  if (current_conn)
  {
    bt_conn_unref(current_conn);
    current_conn = NULL;
  }

  /* Notify NUS of disconnection */
  airspot_nus_set_conn(NULL);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

/* Battery Service (BAS) - enabled via CONFIG_BT_BAS in prj.conf */

/* Advertising data - stored in flash (const) */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

int ble_service_init(void)
{
  int err;

  /* Initialize Bluetooth */
  err = bt_enable(NULL);
  if (err)
  {
    LOG_ERR("Bluetooth init failed (err %d)", err);
    return err;
  }

  initialized = true;
  LOG_INF("BLE service initialized");
  return 0;
}

bool ble_service_is_connected(void) { return (current_conn != NULL); }

void ble_service_disconnect(void)
{
  if (current_conn)
  {
    bt_conn_disconnect(current_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
  }
}

int ble_service_start_advertising(void)
{
  int err;

  if (!initialized)
  {
    return -ENODEV;
  }

  if (advertising)
  {
    return 0; /* Already advertising */
  }

  err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd,
                        ARRAY_SIZE(sd));
  if (err)
  {
    LOG_ERR("Advertising failed to start (err %d)", err);
    return err;
  }

  advertising = true;
  LOG_INF("BLE advertising started");
  return 0;
}

int ble_service_stop_advertising(void)
{
  int err;

  if (!advertising)
  {
    return 0; /* Not advertising */
  }

  err = bt_le_adv_stop();
  if (err)
  {
    LOG_ERR("Advertising stop failed (err %d)", err);
    return err;
  }

  advertising = false;
  LOG_INF("BLE advertising stopped");
  return 0;
}
