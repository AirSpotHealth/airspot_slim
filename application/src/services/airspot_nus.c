/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AirSpot NUS Service Implementation
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <services/airspot_nus.h>

LOG_MODULE_REGISTER(airspot_nus, CONFIG_AIRSPOT_LOG_LEVEL);

/* NUS Service UUIDs - Standard Nordic UART Service */
#define BT_UUID_NUS_SERVICE_VAL \
  BT_UUID_128_ENCODE(0x6e400001, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)
#define BT_UUID_NUS_RX_VAL \
  BT_UUID_128_ENCODE(0x6e400002, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)
#define BT_UUID_NUS_TX_VAL \
  BT_UUID_128_ENCODE(0x6e400003, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)

#define BT_UUID_NUS_SERVICE BT_UUID_DECLARE_128(BT_UUID_NUS_SERVICE_VAL)
#define BT_UUID_NUS_RX BT_UUID_DECLARE_128(BT_UUID_NUS_RX_VAL)
#define BT_UUID_NUS_TX BT_UUID_DECLARE_128(BT_UUID_NUS_TX_VAL)

static struct bt_conn *current_conn = NULL;
static airspot_nus_rx_callback_t rx_callback = NULL;
static bool tx_notifications_enabled = false;

/* TX characteristic CCC changed callback */
static void tx_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
  tx_notifications_enabled = (value == BT_GATT_CCC_NOTIFY);
  LOG_INF("NUS TX notifications %s",
          tx_notifications_enabled ? "enabled" : "disabled");
}

/* RX characteristic write callback */
static ssize_t rx_write_cb(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr, const void *buf,
                           uint16_t len, uint16_t offset, uint8_t flags)
{
  ARG_UNUSED(conn);
  ARG_UNUSED(attr);
  ARG_UNUSED(offset);
  ARG_UNUSED(flags);

  LOG_DBG("NUS RX received %d bytes", len);

  if (rx_callback)
  {
    rx_callback(buf, len);
  }

  return len;
}

/* NUS Service Definition */
BT_GATT_SERVICE_DEFINE(
    nus_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_NUS_SERVICE),
    /* RX Characteristic - Write from phone to device */
    BT_GATT_CHARACTERISTIC(BT_UUID_NUS_RX,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE, NULL, rx_write_cb, NULL),
    /* TX Characteristic - Notify from device to phone */
    BT_GATT_CHARACTERISTIC(BT_UUID_NUS_TX, BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE, NULL, NULL, NULL),
    BT_GATT_CCC(tx_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

int airspot_nus_init(airspot_nus_rx_callback_t callback)
{
  rx_callback = callback;
  LOG_INF("NUS service initialized");
  return 0;
}

int airspot_nus_send(const uint8_t *data, uint16_t len)
{
  if (!current_conn)
  {
    LOG_WRN("NUS send failed: not connected");
    return -ENOTCONN;
  }

  if (!tx_notifications_enabled)
  {
    LOG_WRN("NUS send failed: notifications not enabled");
    return -ENOENT;
  }

  /* Get TX characteristic attribute (index 4 in service definition) */
  const struct bt_gatt_attr *tx_attr = &nus_svc.attrs[4];

  int err = bt_gatt_notify(current_conn, tx_attr, data, len);
  if (err)
  {
    LOG_ERR("NUS notify failed: %d", err);
    return err;
  }

  LOG_DBG("NUS sent %d bytes", len);
  return 0;
}

bool airspot_nus_is_ready(void)
{
  return (current_conn != NULL) && tx_notifications_enabled;
}

void airspot_nus_set_conn(struct bt_conn *conn)
{
  if (conn)
  {
    current_conn = bt_conn_ref(conn);
    LOG_INF("NUS connection set");
  }
  else
  {
    if (current_conn)
    {
      bt_conn_unref(current_conn);
    }
    current_conn = NULL;
    tx_notifications_enabled = false;
    LOG_INF("NUS connection cleared");
  }
}
