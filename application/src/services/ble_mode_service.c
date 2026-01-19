/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BLE Mode Service Implementation
 * Handles switching between Peripheral and Mesh modes at runtime.
 *
 * NOTE: Mesh mode support requires CONFIG_BT_MESH=y in prj.conf.
 * Currently mesh is disabled pending nRF54L-specific BT_HCI_HOST config.
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#if IS_ENABLED(CONFIG_BT_MESH)
#include <zephyr/bluetooth/mesh.h>
#include <services/mesh_sensor.h>
#endif

#include <services/ble_mode_service.h>
#include <services/ble_service.h>

LOG_MODULE_REGISTER(ble_mode, CONFIG_AIRSPOT_LOG_LEVEL);

/* Settings key for BLE mode persistence */
#define BLE_MODE_SETTINGS_KEY "airspot/ble_mode"

/* Current BLE mode */
static ble_mode_t current_mode = BLE_MODE_PERIPHERAL;
static bool initialized = false;
static ble_mode_change_callback_t mode_change_callback = NULL;

/* Forward declarations */
static int start_peripheral_mode(void);
static int stop_peripheral_mode(void);
#if IS_ENABLED(CONFIG_BT_MESH)
static int start_mesh_mode(void);
static int stop_mesh_mode(void);
#endif

/* ------------------------------------------------------------------------- */
/* Mesh Configuration (Only when CONFIG_BT_MESH is enabled) */
/* ------------------------------------------------------------------------- */

#if IS_ENABLED(CONFIG_BT_MESH)

/* Mesh provisioning UUID - unique per device (should be based on device ID) */
static uint8_t dev_uuid[16] = {0xdd, 0xdd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* Provisioning callbacks */
static void prov_complete(uint16_t net_idx, uint16_t addr)
{
  LOG_INF("Mesh provisioning complete: net_idx=0x%04x, addr=0x%04x", net_idx,
          addr);

  /* Initialize mesh sensor model after provisioning */
  mesh_sensor_init();
}

static void prov_reset(void)
{
  LOG_INF("Mesh provisioning reset");
  bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);
}

static const struct bt_mesh_prov prov = {
    .uuid = dev_uuid,
    .complete = prov_complete,
    .reset = prov_reset,
};

#endif /* CONFIG_BT_MESH */

/* ------------------------------------------------------------------------- */
/* Settings Handler for Mode Persistence */
/* ------------------------------------------------------------------------- */

static int ble_mode_settings_set(const char *name, size_t len,
                                 settings_read_cb read_cb, void *cb_arg)
{
  if (len != sizeof(current_mode))
  {
    return -EINVAL;
  }

  int rc = read_cb(cb_arg, &current_mode, sizeof(current_mode));
  if (rc < 0)
  {
    return rc;
  }

#if !IS_ENABLED(CONFIG_BT_MESH)
  /* Force peripheral mode if mesh is disabled */
  if (current_mode == BLE_MODE_MESH)
  {
    LOG_WRN(
        "Mesh mode requested but CONFIG_BT_MESH not enabled, using peripheral");
    current_mode = BLE_MODE_PERIPHERAL;
  }
#endif

  LOG_INF("Loaded BLE mode from flash: %s",
          current_mode == BLE_MODE_MESH ? "MESH" : "PERIPHERAL");
  return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(ble_mode, BLE_MODE_SETTINGS_KEY, NULL,
                               ble_mode_settings_set, NULL, NULL);

/* ------------------------------------------------------------------------- */
/* Mode Control Functions */
/* ------------------------------------------------------------------------- */

static int start_peripheral_mode(void)
{
  LOG_INF("Starting Peripheral mode");
  return ble_service_start_advertising();
}

static int stop_peripheral_mode(void)
{
  LOG_INF("Stopping Peripheral mode");
  return ble_service_stop_advertising();
}

#if IS_ENABLED(CONFIG_BT_MESH)

static int start_mesh_mode(void)
{
  int err;

  LOG_INF("Starting Mesh mode");

  /* Get the mesh composition from the sensor model */
  const struct bt_mesh_comp *comp = mesh_sensor_get_composition();
  if (!comp)
  {
    LOG_ERR("Mesh sensor composition not available");
    return -ENODEV;
  }

  err = bt_mesh_init(&prov, comp);
  if (err)
  {
    LOG_ERR("Mesh init failed: %d", err);
    return err;
  }

  /* Try to load provisioning data */
  if (IS_ENABLED(CONFIG_SETTINGS))
  {
    settings_load();
  }

  if (bt_mesh_is_provisioned())
  {
    LOG_INF("Mesh already provisioned");
    /* Initialize sensor model for already-provisioned device */
    mesh_sensor_init();
  }
  else
  {
    LOG_INF("Enabling mesh provisioning (ADV + GATT)");
    bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);
  }

  return 0;
}

static int stop_mesh_mode(void)
{
  LOG_INF("Stopping Mesh mode");
  /* Note: Full mesh reset not always needed, just disable provisioning */
  bt_mesh_prov_disable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);
  return 0;
}

#endif /* CONFIG_BT_MESH */

/* ------------------------------------------------------------------------- */
/* Public API */
/* ------------------------------------------------------------------------- */

int ble_mode_init(void)
{
  int err;

  if (initialized)
  {
    return 0;
  }

  LOG_INF("Initializing BLE Mode Service");

  /* Load settings to get stored mode */
  if (IS_ENABLED(CONFIG_SETTINGS))
  {
    err = settings_subsys_init();
    if (err)
    {
      LOG_ERR("Settings init failed: %d", err);
      /* Continue with default mode */
    }
    settings_load();
  }

#if !IS_ENABLED(CONFIG_BT_MESH)
  /* Force peripheral mode if mesh is disabled */
  if (current_mode == BLE_MODE_MESH)
  {
    LOG_WRN("Mesh mode not available (CONFIG_BT_MESH=n), using peripheral");
    current_mode = BLE_MODE_PERIPHERAL;
  }
#endif

  LOG_INF("Starting in %s mode",
          current_mode == BLE_MODE_MESH ? "MESH" : "PERIPHERAL");

  /* Start in the stored mode */
#if IS_ENABLED(CONFIG_BT_MESH)
  if (current_mode == BLE_MODE_MESH)
  {
    err = start_mesh_mode();
  }
  else
  {
    err = start_peripheral_mode();
  }
#else
  err = start_peripheral_mode();
#endif

  if (err)
  {
    LOG_ERR("Failed to start BLE mode: %d", err);
    return err;
  }

  initialized = true;
  return 0;
}

int ble_mode_switch(ble_mode_t mode)
{
  int err;

  if (!initialized)
  {
    return -ENODEV;
  }

  if (mode == current_mode)
  {
    LOG_INF("Already in %s mode",
            mode == BLE_MODE_MESH ? "MESH" : "PERIPHERAL");
    return 0;
  }

#if !IS_ENABLED(CONFIG_BT_MESH)
  if (mode == BLE_MODE_MESH)
  {
    LOG_ERR("Cannot switch to mesh - CONFIG_BT_MESH not enabled");
    return -ENOTSUP;
  }
#endif

  LOG_INF("Switching from %s to %s mode",
          current_mode == BLE_MODE_MESH ? "MESH" : "PERIPHERAL",
          mode == BLE_MODE_MESH ? "MESH" : "PERIPHERAL");

  /* Stop current mode */
#if IS_ENABLED(CONFIG_BT_MESH)
  if (current_mode == BLE_MODE_MESH)
  {
    err = stop_mesh_mode();
  }
  else
  {
    err = stop_peripheral_mode();
  }
#else
  err = stop_peripheral_mode();
#endif

  if (err)
  {
    LOG_ERR("Failed to stop current mode: %d", err);
    return err;
  }

  /* Update and save mode */
  current_mode = mode;

  if (IS_ENABLED(CONFIG_SETTINGS))
  {
    err = settings_save_one(BLE_MODE_SETTINGS_KEY, &current_mode,
                            sizeof(current_mode));
    if (err)
    {
      LOG_WRN("Failed to save mode to flash: %d", err);
    }
  }

  /* Start new mode */
#if IS_ENABLED(CONFIG_BT_MESH)
  if (mode == BLE_MODE_MESH)
  {
    err = start_mesh_mode();
  }
  else
  {
    err = start_peripheral_mode();
  }
#else
  err = start_peripheral_mode();
#endif

  if (err)
  {
    LOG_ERR("Failed to start new mode: %d", err);
    return err;
  }

  /* Notify callback */
  if (mode_change_callback)
  {
    mode_change_callback(mode);
  }

  return 0;
}

ble_mode_t ble_mode_get_current(void) { return current_mode; }

bool ble_mode_is_provisioned(void)
{
#if IS_ENABLED(CONFIG_BT_MESH)
  if (current_mode != BLE_MODE_MESH)
  {
    return false;
  }
  return bt_mesh_is_provisioned();
#else
  return false;
#endif
}

void ble_mode_register_callback(ble_mode_change_callback_t callback)
{
  mode_change_callback = callback;
}

int ble_mode_factory_reset(void)
{
  int err;

  LOG_WRN("Factory reset - clearing mesh provisioning data");

#if IS_ENABLED(CONFIG_BT_MESH)
  /* Reset mesh if active */
  if (current_mode == BLE_MODE_MESH && bt_mesh_is_provisioned())
  {
    bt_mesh_reset();
  }
#endif

  /* Switch back to peripheral mode */
  current_mode = BLE_MODE_PERIPHERAL;

  if (IS_ENABLED(CONFIG_SETTINGS))
  {
    err = settings_save_one(BLE_MODE_SETTINGS_KEY, &current_mode,
                            sizeof(current_mode));
    if (err)
    {
      LOG_WRN("Failed to save mode to flash: %d", err);
    }
  }

  LOG_INF("Factory reset complete - device is now in Peripheral mode");
  return 0;
}
