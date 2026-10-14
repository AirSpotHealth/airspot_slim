/*
 * Copyright (c) 2026 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * LPS22HH Pressure Sensor Driver Implementation
 *
 * Reference: ST LPS22HH Datasheet
 * https://www.st.com/resource/en/datasheet/lps22hh.pdf
 */

#include <errno.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/i2c_bus.h>
#include <drivers/lps22hh.h>
#include <services/diagnostics.h>
extern void watchdog_feed(void);

LOG_MODULE_REGISTER(lps22hh, CONFIG_AIRSPOT_LOG_LEVEL);

/* LPS22HH Register addresses */
#define LPS22HH_REG_INTERRUPT_CFG 0x0B
#define LPS22HH_REG_THS_P_L 0x0C
#define LPS22HH_REG_THS_P_H 0x0D
#define LPS22HH_REG_IF_CTRL 0x0E
#define LPS22HH_REG_WHO_AM_I 0x0F
#define LPS22HH_REG_CTRL_REG1 0x10
#define LPS22HH_REG_CTRL_REG2 0x11
#define LPS22HH_REG_CTRL_REG3 0x12
#define LPS22HH_REG_FIFO_CTRL 0x13
#define LPS22HH_REG_FIFO_WTM 0x14
#define LPS22HH_REG_REF_P_L 0x15
#define LPS22HH_REG_REF_P_H 0x16
#define LPS22HH_REG_RPDS_L 0x18
#define LPS22HH_REG_RPDS_H 0x19
#define LPS22HH_REG_INT_SOURCE 0x24
#define LPS22HH_REG_FIFO_STATUS1 0x25
#define LPS22HH_REG_FIFO_STATUS2 0x26
#define LPS22HH_REG_STATUS 0x27
#define LPS22HH_REG_PRESS_OUT_XL 0x28
#define LPS22HH_REG_PRESS_OUT_L 0x29
#define LPS22HH_REG_PRESS_OUT_H 0x2A
#define LPS22HH_REG_TEMP_OUT_L 0x2B
#define LPS22HH_REG_TEMP_OUT_H 0x2C

/* WHO_AM_I value */
#define LPS22HH_WHO_AM_I_VALUE 0xB3

/* CTRL_REG1 bits */
#define LPS22HH_CTRL1_ODR_MASK 0x70
#define LPS22HH_CTRL1_BDU 0x02

/* CTRL_REG2 bits */
#define LPS22HH_CTRL2_ONE_SHOT 0x01
#define LPS22HH_CTRL2_LOW_NOISE 0x02
#define LPS22HH_CTRL2_SWRESET 0x04
#define LPS22HH_CTRL2_IF_ADD_INC 0x10
#define LPS22HH_CTRL2_BOOT 0x80
#define LPS22HH_I3C_DISABLE 0x02

static bool initialized = false;
static bool startup_recovery_attempted;
static bool has_valid_sample;
static lps22hh_data_t last_data = {.pressure = 0, .temperature = NAN};

static int lps22hh_write_reg(uint8_t reg, uint8_t value) {
  uint8_t buf[2] = {reg, value};
  return i2c_bus_write(I2C_ADDR_LPS22HH, buf, 2);
}

static int lps22hh_read_reg(uint8_t reg, uint8_t *value) {
  return i2c_bus_write_read(I2C_ADDR_LPS22HH, &reg, 1, value, 1);
}

static int lps22hh_read_regs(uint8_t reg, uint8_t *data, size_t len) {
  return i2c_bus_write_read(I2C_ADDR_LPS22HH, &reg, 1, data, len);
}

/* Diagnostics do not write registers or alter calibration. */
static void log_registers(const char *reason) {
  const uint8_t addresses[] = {0x0b, 0x0e, 0x0f, 0x10, 0x11, 0x24, 0x27};
  for (size_t i = 0; i < sizeof(addresses); i++) {
    uint8_t value = 0;
    int ret = lps22hh_read_reg(addresses[i], &value);
    LOG_INF("LPS22HH %s reg 0x%02x=0x%02x rc=%d", reason, addresses[i], value, ret);
  }
}

static int wait_clear(uint8_t mask, int attempts) {
  for (int i = 0; i < attempts; i++) {
    uint8_t value;
    k_msleep(10);
    int ret = lps22hh_read_reg(LPS22HH_REG_CTRL_REG2, &value);
    if (ret) return ret;
    if (!(value & mask)) return 0;
  }
  return -ETIMEDOUT;
}

static uint8_t init_stage;
static int lps22hh_init_once(void) {
  if (initialized) {
    return 0;
  }

  init_stage = 1;
  if (!i2c_bus_is_ready()) {
    LOG_ERR("I2C bus not ready");
    return -ENODEV;
  }

  init_stage = 2;
  /* Read WHO_AM_I */
  uint8_t id;
  int ret = lps22hh_read_id(&id);
  /* Allow a briefly unavailable sensor to finish power-up, without unbounded
   * retries or attempting register writes to an unreadable device. */
  for (unsigned attempt = 1; ret && attempt < 3; ++attempt) {
    k_msleep(50);
    ret = lps22hh_read_id(&id);
  }
  if (ret) {
    LOG_ERR("Failed to read LPS22HH ID: %d", ret);
    return ret;
  }

  init_stage = 3;
  if (id != LPS22HH_WHO_AM_I_VALUE) {
    LOG_ERR("Invalid LPS22HH ID: 0x%02X (expected 0x%02X)", id,
            LPS22HH_WHO_AM_I_VALUE);
    return -ENODEV;
  }

  log_registers("before init");
  /* Enter power-down before changing LOW_NOISE_EN (datasheet section 9.7). */
  init_stage = 4;
  ret = lps22hh_write_reg(LPS22HH_REG_CTRL_REG1, LPS22HH_CTRL1_BDU);
  if (ret) return ret;
  /* Reload factory trims; this does not modify the factory memory. */
  init_stage = 5;
  ret = lps22hh_write_reg(LPS22HH_REG_CTRL_REG2, LPS22HH_CTRL2_BOOT);
  if (ret) return ret;
  init_stage = 6;
  ret = wait_clear(LPS22HH_CTRL2_BOOT, 50);
  if (ret) return ret;
  k_msleep(50);
  init_stage = 7;
  ret = lps22hh_write_reg(LPS22HH_REG_CTRL_REG2,
                          LPS22HH_CTRL2_IF_ADD_INC | LPS22HH_CTRL2_LOW_NOISE);
  if (ret) return ret;
  /* ST recommends disabling I3C when using I2C (datasheet sections 7.5/9.4).
   * Apply after BOOT and every volatile reset, and verify the interface write.
   * This cannot recover a sensor already strapped into I3C-only at power-up. */
  init_stage = 8;
  ret = lps22hh_write_reg(LPS22HH_REG_IF_CTRL, LPS22HH_I3C_DISABLE);
  if (ret) return ret;
  uint8_t interface;
  init_stage = 9;
  ret = lps22hh_read_reg(LPS22HH_REG_IF_CTRL, &interface);
  if (ret) return ret;
  init_stage = 10;
  if (interface != LPS22HH_I3C_DISABLE) return -EIO;
  log_registers("configured");

  initialized = true;
  LOG_INF("LPS22HH initialized (ID: 0x%02X)", id);
  return 0;
}

/* One bounded retry, only resetting a sensor whose identity is readable.
 * Record the original failing stage before recovery; the NVS journal survives
 * trial rollback and is readable using existing Bluetooth command 0x37.
 * Detail: 0x50 marker, stage byte, absolute errno (16 bits).
 */
int lps22hh_init(void) {
  int ret = lps22hh_init_once();
  if (!ret) return 0;
  diagnostics_record(DIAG_SENSOR_FAILED,
      0x50000000U | ((uint32_t)init_stage << 16) | ((uint32_t)-ret & 0xffff));
  if (init_stage == 1 || init_stage == 3) return ret;
  watchdog_feed();
  k_msleep(500);
  uint8_t id = 0;
  int probe = lps22hh_read_id(&id);
  watchdog_feed();
  if (probe || id != LPS22HH_WHO_AM_I_VALUE) return ret;
  int reset = lps22hh_soft_reset();
  watchdog_feed();
  if (reset) return reset;
  ret = lps22hh_init_once();
  watchdog_feed();
  return ret;
}

int lps22hh_read_id(uint8_t *id) {
  if (!id) {
    return -EINVAL;
  }
  return lps22hh_read_reg(LPS22HH_REG_WHO_AM_I, id);
}

int lps22hh_soft_reset(void) {
  int ret = lps22hh_write_reg(LPS22HH_REG_CTRL_REG2, LPS22HH_CTRL2_SWRESET);
  if (ret) {
    return ret;
  }

  initialized = false;
  return wait_clear(LPS22HH_CTRL2_SWRESET, 50);
}

int lps22hh_set_odr(lps22hh_odr_t odr) {
  if (!initialized) {
    return -ENODEV;
  }

  uint8_t odr_bits;
  switch (odr) {
  case LPS22HH_ODR_ONE_SHOT:
    odr_bits = 0;
    break;
  case LPS22HH_ODR_1HZ:
    odr_bits = 1;
    break;
  case LPS22HH_ODR_10HZ:
    odr_bits = 2;
    break;
  case LPS22HH_ODR_25HZ:
    odr_bits = 3;
    break;
  case LPS22HH_ODR_50HZ:
    odr_bits = 4;
    break;
  case LPS22HH_ODR_75HZ:
    odr_bits = 5;
    break;
  case LPS22HH_ODR_100HZ:
    odr_bits = 6;
    break;
  case LPS22HH_ODR_200HZ:
    odr_bits = 7;
    break;
  default:
    return -EINVAL;
  }

  return lps22hh_write_reg(LPS22HH_REG_CTRL_REG1,
                           (odr_bits << 4) | LPS22HH_CTRL1_BDU);
}

int lps22hh_trigger_one_shot(void) {
  if (!initialized) {
    return -ENODEV;
  }

  uint8_t val;
  int ret = lps22hh_read_reg(LPS22HH_REG_CTRL_REG2, &val);
  if (ret) {
    return ret;
  }

  return lps22hh_write_reg(LPS22HH_REG_CTRL_REG2, val | LPS22HH_CTRL2_ONE_SHOT);
}

static int read_once(lps22hh_data_t *data) {
  if (!initialized) {
    return -ENODEV;
  }
  if (!data) {
    return -EINVAL;
  }

  int ret = lps22hh_trigger_one_shot();
  if (ret) return ret;
  bool ready = false;
  for (int i = 0; i < 50; i++) {
    uint8_t status;
    k_msleep(10);
    ret = lps22hh_read_reg(LPS22HH_REG_STATUS, &status);
    if (ret) return ret;
    if ((status & 0x03) == 0x03) { ready = true; break; }
  }
  if (!ready) return -ETIMEDOUT;

  /* With BDU, PRESS_OUT_H must be the last address read (section 9.6).
   * Read temperature first, then all three pressure bytes. */
  uint8_t raw[5];
  ret = lps22hh_read_regs(LPS22HH_REG_TEMP_OUT_L, raw + 3, 2);
  if (!ret) ret = lps22hh_read_regs(LPS22HH_REG_PRESS_OUT_XL, raw, 3);
  if (ret) return ret;

  /* Pressure: 24-bit signed, LSB = 1/4096 hPa */
  int32_t press_raw = (int32_t)((raw[2] << 16) | (raw[1] << 8) | raw[0]);
  if (press_raw & 0x800000) { /* Sign extend */
    press_raw |= 0xFF000000;
  }
  float pressure = (float)press_raw / 4096.0f;
  if (pressure < 260.0f || pressure > 1260.0f) return -ERANGE;
  data->pressure = pressure;

  /* Temperature: 16-bit signed, LSB = 0.01 °C */
  int16_t temp_raw = (int16_t)((raw[4] << 8) | raw[3]);
  data->temperature = (float)temp_raw / 100.0f;

  last_data = *data;

  LOG_DBG("LPS22HH: %.2f hPa, %.2f°C", (double)data->pressure,
          (double)data->temperature);
  return 0;
}

int lps22hh_read(lps22hh_data_t *data) {
  if (!data) return -EINVAL;
  if (!initialized) return -ENODEV;
  int ret = read_once(data);
  if (ret && !has_valid_sample && !startup_recovery_attempted) {
    startup_recovery_attempted = true;
    LOG_WRN("LPS22HH first measurement failed: %d; retrying volatile configuration", ret);
    log_registers("measurement failed");
    /* Only the pressure sensor's volatile configuration is reset. No STCC4
     * command, settings write, or stored log operation is involved. */
    ret = lps22hh_soft_reset();
    if (!ret) ret = lps22hh_init();
    if (!ret) ret = read_once(data);
    LOG_INF("LPS22HH startup recovery result: %d", ret);
  }
  if (!ret) has_valid_sample = true;
  return ret;
}

float lps22hh_get_pressure(void) { return last_data.pressure; }

float lps22hh_get_temperature(void) { return last_data.temperature; }
