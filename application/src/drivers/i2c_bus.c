/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * I2C Bus Driver Implementation
 */

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/i2c_bus.h>

LOG_MODULE_REGISTER(i2c_bus, CONFIG_AIRSPOT_LOG_LEVEL);

/* I2C device - defined in device tree */
#define I2C_NODE DT_NODELABEL(i2c30)

static const struct device *i2c_dev;
static bool initialized = false;

int i2c_bus_init(void) {
  if (initialized) {
    return 0;
  }

  i2c_dev = DEVICE_DT_GET(I2C_NODE);
  if (!device_is_ready(i2c_dev)) {
    LOG_ERR("I2C device not ready");
    return -ENODEV;
  }

  /* Configure I2C speed - 400kHz (Fast mode) */
  uint32_t config = I2C_SPEED_SET(I2C_SPEED_FAST) | I2C_MODE_CONTROLLER;
  int ret = i2c_configure(i2c_dev, config);
  if (ret) {
    LOG_ERR("Failed to configure I2C: %d", ret);
    return ret;
  }

  initialized = true;
  LOG_INF("I2C bus initialized (400kHz)");
  return 0;
}

bool i2c_bus_is_ready(void) { return initialized && device_is_ready(i2c_dev); }

int i2c_bus_write(uint8_t addr, const uint8_t *data, size_t len) {
  if (!initialized) {
    return -ENODEV;
  }
  return i2c_write(i2c_dev, data, len, addr);
}

int i2c_bus_read(uint8_t addr, uint8_t *data, size_t len) {
  if (!initialized) {
    return -ENODEV;
  }
  return i2c_read(i2c_dev, data, len, addr);
}

int i2c_bus_write_read(uint8_t addr, const uint8_t *write_data,
                       size_t write_len, uint8_t *read_data, size_t read_len) {
  if (!initialized) {
    return -ENODEV;
  }
  return i2c_write_read(i2c_dev, addr, write_data, write_len, read_data,
                        read_len);
}

void i2c_bus_scan(void) {
  if (!initialized) {
    LOG_WRN("I2C not initialized, cannot scan");
    return;
  }

  LOG_INF("Scanning I2C bus...");

  int found = 0;

  /* Scan I2C addresses 0x08 to 0x77 (valid 7-bit addresses) */
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    /* Try to write 0 bytes - this is a quick probe */
    /* If device exists, it will ACK; if not, it will NACK */
    int ret = i2c_write(i2c_dev, NULL, 0, addr);
    if (ret == 0) {
      LOG_INF("  Found device at 0x%02X", addr);
      found++;
      k_sleep(K_MSEC(10));  /* Small delay between devices */
    } else if (ret != -EIO) /* EIO is expected for non-existent devices */
    {
      /* Other errors might indicate bus issues, but continue scanning */
      LOG_DBG("  Address 0x%02X: error %d", addr, ret);
    }
  }

  LOG_INF("I2C scan complete: %d devices found", found);
}
