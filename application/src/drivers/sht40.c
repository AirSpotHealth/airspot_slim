/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SHT40 Temperature & Humidity Sensor Driver Implementation
 * Reference: Sensirion SHT4x Datasheet v6.4
 */

#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/i2c_bus.h>
#include <drivers/sht40.h>

LOG_MODULE_REGISTER(sht40, CONFIG_AIRSPOT_LOG_LEVEL);

/* I2C Commands (from datasheet) */
#define SHT40_CMD_MEASURE_HIGH 0xFD   /* High precision, no heater */
#define SHT40_CMD_MEASURE_MEDIUM 0xF6 /* Medium precision, no heater */
#define SHT40_CMD_MEASURE_LOW 0xE0    /* Low precision, no heater */
#define SHT40_CMD_READ_SERIAL 0x89    /* Read serial number */
#define SHT40_CMD_SOFT_RESET 0x94     /* Soft reset */

/* Heater commands */
#define SHT40_CMD_HEATER_200MW_1S 0x39
#define SHT40_CMD_HEATER_200MW_100MS 0x32
#define SHT40_CMD_HEATER_110MW_1S 0x2F
#define SHT40_CMD_HEATER_110MW_100MS 0x24
#define SHT40_CMD_HEATER_20MW_1S 0x1E
#define SHT40_CMD_HEATER_20MW_100MS 0x15

/* Timing (from datasheet) */
#define SHT40_MEASURE_TIME_HIGH_MS 9
#define SHT40_MEASURE_TIME_MEDIUM_MS 5
#define SHT40_MEASURE_TIME_LOW_MS 2
#define SHT40_RESET_TIME_MS 1

/* CRC polynomial: x^8 + x^5 + x^4 + 1 = 0x31 */
#define SHT40_CRC_POLYNOMIAL 0x31
#define SHT40_CRC_INIT 0xFF

static bool initialized = false;
static sht40_data_t last_data = {.temperature = NAN, .humidity = NAN};

/* CRC-8 calculation */
static uint8_t sht40_crc8(const uint8_t *data, size_t len) {
  uint8_t crc = SHT40_CRC_INIT;

  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ SHT40_CRC_POLYNOMIAL;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

/* Convert raw values to temperature and humidity */
static void sht40_convert_raw(const uint8_t *raw, sht40_data_t *data) {
  uint16_t t_raw = (raw[0] << 8) | raw[1];
  uint16_t h_raw = (raw[3] << 8) | raw[4];

  /* Temperature: -45 + 175 * (t_raw / 65535) */
  data->temperature = -45.0f + 175.0f * ((float)t_raw / 65535.0f);

  /* Humidity: -6 + 125 * (h_raw / 65535), clamped to 0-100 */
  data->humidity = -6.0f + 125.0f * ((float)h_raw / 65535.0f);
  if (data->humidity < 0.0f)
    data->humidity = 0.0f;
  if (data->humidity > 100.0f)
    data->humidity = 100.0f;
}

int sht40_init(void) {
  if (initialized) {
    return 0;
  }

  if (!i2c_bus_is_ready()) {
    LOG_ERR("I2C bus not ready");
    return -ENODEV;
  }

  /* Soft reset to ensure known state */
  int ret = sht40_soft_reset();
  if (ret) {
    LOG_ERR("SHT40 soft reset failed: %d", ret);
    return ret;
  }

  /* Verify communication by reading serial */
  uint32_t serial;
  ret = sht40_read_serial(&serial);
  if (ret) {
    LOG_ERR("SHT40 not responding at 0x%02X", I2C_ADDR_SHT40);
    return ret;
  }

  initialized = true;
  LOG_INF("SHT40 initialized (serial: 0x%08X)", serial);
  return 0;
}

int sht40_soft_reset(void) {
  uint8_t cmd = SHT40_CMD_SOFT_RESET;
  int ret = i2c_bus_write(I2C_ADDR_SHT40, &cmd, 1);
  if (ret) {
    return ret;
  }
  k_msleep(SHT40_RESET_TIME_MS);
  return 0;
}

int sht40_read_serial(uint32_t *serial) {
  if (!serial) {
    return -EINVAL;
  }

  uint8_t cmd = SHT40_CMD_READ_SERIAL;
  uint8_t data[6];

  int ret = i2c_bus_write(I2C_ADDR_SHT40, &cmd, 1);
  if (ret) {
    return ret;
  }

  k_msleep(1);

  ret = i2c_bus_read(I2C_ADDR_SHT40, data, 6);
  if (ret) {
    return ret;
  }

  /* Verify CRC for both words */
  if (sht40_crc8(&data[0], 2) != data[2] ||
      sht40_crc8(&data[3], 2) != data[5]) {
    LOG_WRN("SHT40 serial CRC mismatch");
    return -EIO;
  }

  *serial = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
            ((uint32_t)data[3] << 8) | data[4];
  return 0;
}

int sht40_measure(sht40_data_t *data, sht40_precision_t precision) {
  if (!initialized) {
    return -ENODEV;
  }
  if (!data) {
    return -EINVAL;
  }

  uint8_t cmd;
  uint32_t wait_ms;

  switch (precision) {
  case SHT40_PRECISION_HIGH:
    cmd = SHT40_CMD_MEASURE_HIGH;
    wait_ms = SHT40_MEASURE_TIME_HIGH_MS;
    break;
  case SHT40_PRECISION_MEDIUM:
    cmd = SHT40_CMD_MEASURE_MEDIUM;
    wait_ms = SHT40_MEASURE_TIME_MEDIUM_MS;
    break;
  case SHT40_PRECISION_LOW:
  default:
    cmd = SHT40_CMD_MEASURE_LOW;
    wait_ms = SHT40_MEASURE_TIME_LOW_MS;
    break;
  }

  int ret = i2c_bus_write(I2C_ADDR_SHT40, &cmd, 1);
  if (ret) {
    return ret;
  }

  k_msleep(wait_ms);

  uint8_t raw[6];
  ret = i2c_bus_read(I2C_ADDR_SHT40, raw, 6);
  if (ret) {
    return ret;
  }

  /* Verify CRC */
  if (sht40_crc8(&raw[0], 2) != raw[2] || sht40_crc8(&raw[3], 2) != raw[5]) {
    LOG_WRN("SHT40 measurement CRC mismatch");
    return -EIO;
  }

  sht40_convert_raw(raw, data);
  last_data = *data;

  LOG_DBG("SHT40: %.2f°C, %.1f%%RH", data->temperature, data->humidity);
  return 0;
}

int sht40_measure_with_heater(sht40_data_t *data, sht40_heater_t heater) {
  if (!initialized) {
    return -ENODEV;
  }
  if (!data) {
    return -EINVAL;
  }

  uint8_t cmd;
  uint32_t wait_ms;

  switch (heater) {
  case SHT40_HEATER_200MW_1S:
    cmd = SHT40_CMD_HEATER_200MW_1S;
    wait_ms = 1100;
    break;
  case SHT40_HEATER_200MW_100MS:
    cmd = SHT40_CMD_HEATER_200MW_100MS;
    wait_ms = 110;
    break;
  case SHT40_HEATER_110MW_1S:
    cmd = SHT40_CMD_HEATER_110MW_1S;
    wait_ms = 1100;
    break;
  case SHT40_HEATER_110MW_100MS:
    cmd = SHT40_CMD_HEATER_110MW_100MS;
    wait_ms = 110;
    break;
  case SHT40_HEATER_20MW_1S:
    cmd = SHT40_CMD_HEATER_20MW_1S;
    wait_ms = 1100;
    break;
  case SHT40_HEATER_20MW_100MS:
    cmd = SHT40_CMD_HEATER_20MW_100MS;
    wait_ms = 110;
    break;
  default:
    return -EINVAL;
  }

  int ret = i2c_bus_write(I2C_ADDR_SHT40, &cmd, 1);
  if (ret) {
    return ret;
  }

  k_msleep(wait_ms);

  uint8_t raw[6];
  ret = i2c_bus_read(I2C_ADDR_SHT40, raw, 6);
  if (ret) {
    return ret;
  }

  /* Verify CRC */
  if (sht40_crc8(&raw[0], 2) != raw[2] || sht40_crc8(&raw[3], 2) != raw[5]) {
    return -EIO;
  }

  sht40_convert_raw(raw, data);
  last_data = *data;

  return 0;
}

float sht40_get_temperature(void) { return last_data.temperature; }

float sht40_get_humidity(void) { return last_data.humidity; }
