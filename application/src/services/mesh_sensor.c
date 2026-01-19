/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mesh Sensor Model Implementation
 *
 * Publishes CO2 sensor data over Bluetooth Mesh using the standard Sensor Model.
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if IS_ENABLED(CONFIG_BT_MESH_SENSOR_SRV)
#include <bluetooth/mesh/sensor.h>
#include <bluetooth/mesh/sensor_srv.h>
#include <bluetooth/mesh/sensor_types.h>
#endif

#include <services/mesh_sensor.h>

LOG_MODULE_REGISTER(mesh_sensor, CONFIG_AIRSPOT_LOG_LEVEL);

#if IS_ENABLED(CONFIG_BT_MESH_SENSOR_SRV)

/* Current sensor data */
static mesh_sensor_data_t current_data = {0};
static bool initialized = false;

/* Mutex for thread safety */
static K_MUTEX_DEFINE(sensor_mutex);

/*
 * =============================================================================
 * Sensor Type Definitions
 * =============================================================================
 */

/* CO2 Concentration Sensor */
static int co2_get(struct bt_mesh_sensor_srv *srv,
                   struct bt_mesh_sensor *sensor,
                   struct bt_mesh_msg_ctx *ctx,
                   struct sensor_value *rsp)
{
  k_mutex_lock(&sensor_mutex, K_FOREVER);

  if (current_data.valid)
  {
    /* CO2 is reported directly in ppm */
    rsp->val1 = current_data.co2_ppm;
    rsp->val2 = 0;
  }
  else
  {
    rsp->val1 = 0;
    rsp->val2 = 0;
  }

  k_mutex_unlock(&sensor_mutex);

  LOG_DBG("CO2 get: %d ppm", rsp->val1);
  return 0;
}

static struct bt_mesh_sensor co2_sensor = {
    .type = &bt_mesh_sensor_co2_concentration,
    .get = co2_get,
};

/* Temperature Sensor */
static int temp_get(struct bt_mesh_sensor_srv *srv,
                    struct bt_mesh_sensor *sensor,
                    struct bt_mesh_msg_ctx *ctx,
                    struct sensor_value *rsp)
{
  k_mutex_lock(&sensor_mutex, K_FOREVER);

  if (current_data.valid)
  {
    /* Temperature in 0.01°C units, convert to sensor_value */
    rsp->val1 = current_data.temperature_c100 / 100;
    rsp->val2 = (current_data.temperature_c100 % 100) * 10000;
  }
  else
  {
    rsp->val1 = 0;
    rsp->val2 = 0;
  }

  k_mutex_unlock(&sensor_mutex);

  LOG_DBG("Temperature get: %d.%02d C", rsp->val1, rsp->val2 / 10000);
  return 0;
}

static struct bt_mesh_sensor temp_sensor = {
    .type = &bt_mesh_sensor_present_amb_temp,
    .get = temp_get,
};

/* Humidity Sensor */
static int humidity_get(struct bt_mesh_sensor_srv *srv,
                        struct bt_mesh_sensor *sensor,
                        struct bt_mesh_msg_ctx *ctx,
                        struct sensor_value *rsp)
{
  k_mutex_lock(&sensor_mutex, K_FOREVER);

  if (current_data.valid)
  {
    /* Humidity in 0.01% units, convert to sensor_value */
    rsp->val1 = current_data.humidity_c100 / 100;
    rsp->val2 = (current_data.humidity_c100 % 100) * 10000;
  }
  else
  {
    rsp->val1 = 0;
    rsp->val2 = 0;
  }

  k_mutex_unlock(&sensor_mutex);

  LOG_DBG("Humidity get: %d.%02d %%", rsp->val1, rsp->val2 / 10000);
  return 0;
}

static struct bt_mesh_sensor humidity_sensor = {
    .type = &bt_mesh_sensor_present_amb_rel_humidity,
    .get = humidity_get,
};

/* Sensor array */
static struct bt_mesh_sensor *const sensors[] = {
    &co2_sensor,
    &temp_sensor,
    &humidity_sensor,
};

/* Sensor Server */
static struct bt_mesh_sensor_srv sensor_srv =
    BT_MESH_SENSOR_SRV_INIT(sensors, ARRAY_SIZE(sensors));

/*
 * =============================================================================
 * Model Definitions
 * =============================================================================
 */

/* Health Server (required) */
static struct bt_mesh_health_srv health_srv = {};
BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);

/* Sensor Server Model */
BT_MESH_MODEL_PUB_DEFINE(sensor_pub, NULL, 64);

/* Models for the sensor element */
static struct bt_mesh_model sensor_models[] = {
    BT_MESH_MODEL_CFG_SRV,
    BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
    BT_MESH_MODEL_SENSOR_SRV(&sensor_srv),
};

/* Elements */
static struct bt_mesh_elem sensor_elements[] = {
    BT_MESH_ELEM(0, sensor_models, BT_MESH_MODEL_NONE),
};

/* Composition */
static const struct bt_mesh_comp sensor_comp = {
    .cid = 0xFFFF, /* TODO: Replace with AirSpot company ID */
    .elem = sensor_elements,
    .elem_count = ARRAY_SIZE(sensor_elements),
};

/*
 * =============================================================================
 * Public API
 * =============================================================================
 */

int mesh_sensor_init(void)
{
  if (initialized)
  {
    return 0;
  }

  LOG_INF("Mesh sensor model initialized");
  LOG_INF("Sensors: CO2, Temperature, Humidity");

  initialized = true;
  return 0;
}

int mesh_sensor_update(const mesh_sensor_data_t *data)
{
  if (!data)
  {
    return -EINVAL;
  }

  k_mutex_lock(&sensor_mutex, K_FOREVER);
  current_data = *data;
  k_mutex_unlock(&sensor_mutex);

  LOG_DBG("Sensor data updated: CO2=%d ppm, T=%d.%02d C, H=%d.%02d %%",
          data->co2_ppm, data->temperature_c100 / 100,
          data->temperature_c100 % 100, data->humidity_c100 / 100,
          data->humidity_c100 % 100);

  return 0;
}

int mesh_sensor_publish_now(void)
{
  if (!initialized)
  {
    return -ENODEV;
  }

  if (!bt_mesh_is_provisioned())
  {
    LOG_WRN("Cannot publish - not provisioned");
    return -ENOTCONN;
  }

  /* Publish CO2 sensor data */
  int err = bt_mesh_sensor_srv_pub(&sensor_srv, NULL, &co2_sensor, NULL);
  if (err && err != -EADDRNOTAVAIL)
  {
    LOG_WRN("CO2 publish failed: %d", err);
  }

  /* Publish temperature */
  err = bt_mesh_sensor_srv_pub(&sensor_srv, NULL, &temp_sensor, NULL);
  if (err && err != -EADDRNOTAVAIL)
  {
    LOG_WRN("Temperature publish failed: %d", err);
  }

  /* Publish humidity */
  err = bt_mesh_sensor_srv_pub(&sensor_srv, NULL, &humidity_sensor, NULL);
  if (err && err != -EADDRNOTAVAIL)
  {
    LOG_WRN("Humidity publish failed: %d", err);
  }

  return 0;
}

const struct bt_mesh_comp *mesh_sensor_get_composition(void)
{
  return &sensor_comp;
}

bool mesh_sensor_is_ready(void) { return initialized && bt_mesh_is_provisioned(); }

#else /* CONFIG_BT_MESH_SENSOR_SRV not enabled */

/* Stub implementations when mesh sensor is disabled */

int mesh_sensor_init(void)
{
  LOG_WRN("Mesh sensor not enabled (CONFIG_BT_MESH_SENSOR_SRV=n)");
  return -ENOTSUP;
}

int mesh_sensor_update(const mesh_sensor_data_t *data)
{
  ARG_UNUSED(data);
  return -ENOTSUP;
}

int mesh_sensor_publish_now(void) { return -ENOTSUP; }

const struct bt_mesh_comp *mesh_sensor_get_composition(void) { return NULL; }

bool mesh_sensor_is_ready(void) { return false; }

#endif /* CONFIG_BT_MESH_SENSOR_SRV */
