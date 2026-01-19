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

#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/i2c_bus.h>
#include <drivers/lps22hh.h>

LOG_MODULE_REGISTER(lps22hh, LOG_LEVEL_DBG);

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

static bool initialized = false;
static lps22hh_data_t last_data = {.pressure = 0, .temperature = NAN};

static int lps22hh_write_reg(uint8_t reg, uint8_t value)
{
  uint8_t buf[2] = {reg, value};
  return i2c_bus_write(I2C_ADDR_LPS22HH, buf, 2);
}

static int lps22hh_read_reg(uint8_t reg, uint8_t *value)
{
  return i2c_bus_write_read(I2C_ADDR_LPS22HH, &reg, 1, value, 1);
}

static int lps22hh_read_regs(uint8_t reg, uint8_t *data, size_t len)
{
  return i2c_bus_write_read(I2C_ADDR_LPS22HH, &reg, 1, data, len);
}

int lps22hh_init(void)
{
  if (initialized)
  {
    return 0;
  }

  if (!i2c_bus_is_ready())
  {
    LOG_ERR("I2C bus not ready");
    return -ENODEV;
  }

  /* Read WHO_AM_I */
  uint8_t id;
  int ret = lps22hh_read_id(&id);
  if (ret)
  {
    LOG_ERR("Failed to read LPS22HH ID: %d", ret);
    return ret;
  }

  if (id != LPS22HH_WHO_AM_I_VALUE)
  {
    LOG_ERR("Invalid LPS22HH ID: 0x%02X (expected 0x%02X)", id,
            LPS22HH_WHO_AM_I_VALUE);
    return -ENODEV;
  }

  /* Trigger BOOT to reload factory calibration data */
  /* This is essential for proper sensor operation */
  ret = lps22hh_write_reg(LPS22HH_REG_CTRL_REG2, LPS22HH_CTRL2_BOOT);
  if (ret)
  {
    LOG_WRN("Failed to trigger BOOT: %d", ret);
  }
  else
  {
    /* Wait for BOOT to complete (bit should self-clear) */
    uint8_t ctrl2;
    for (int i = 0; i < 50; i++)
    {
      k_msleep(10);
      ret = lps22hh_read_reg(LPS22HH_REG_CTRL_REG2, &ctrl2);
      if (ret == 0 && !(ctrl2 & LPS22HH_CTRL2_BOOT))
      {
        LOG_DBG("BOOT completed after %d ms", (i + 1) * 10);
        break;
      }
    }
  }

  k_msleep(50); /* Extra delay after BOOT */

  /* Configure: auto-increment + low-noise in CTRL_REG2 */
  ret = lps22hh_write_reg(LPS22HH_REG_CTRL_REG2,
                          LPS22HH_CTRL2_IF_ADD_INC | LPS22HH_CTRL2_LOW_NOISE);
  if (ret)
  {
    LOG_ERR("Failed to write CTRL_REG2: %d", ret);
    return ret;
  }

  /* Configure: BDU enabled in CTRL_REG1 (ODR=0 for one-shot mode) */
  ret = lps22hh_write_reg(LPS22HH_REG_CTRL_REG1, LPS22HH_CTRL1_BDU);
  if (ret)
  {
    LOG_WRN("Failed to write CTRL_REG1: %d", ret);
  }

  initialized = true;
  LOG_INF("LPS22HH initialized (ID: 0x%02X)", id);
  return 0;
}

int lps22hh_read_id(uint8_t *id)
{
  if (!id)
  {
    return -EINVAL;
  }
  return lps22hh_read_reg(LPS22HH_REG_WHO_AM_I, id);
}

int lps22hh_soft_reset(void)
{
  int ret = lps22hh_write_reg(LPS22HH_REG_CTRL_REG2, LPS22HH_CTRL2_SWRESET);
  if (ret)
  {
    return ret;
  }

  /* Wait for reset to complete */
  k_msleep(10);

  /* Verify reset is complete */
  uint8_t val;
  for (int i = 0; i < 10; i++)
  {
    ret = lps22hh_read_reg(LPS22HH_REG_CTRL_REG2, &val);
    if (ret == 0 && !(val & LPS22HH_CTRL2_SWRESET))
    {
      return 0;
    }
    k_msleep(1);
  }

  return -ETIMEDOUT;
}

int lps22hh_set_odr(lps22hh_odr_t odr)
{
  if (!initialized)
  {
    return -ENODEV;
  }

  uint8_t odr_bits;
  switch (odr)
  {
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

int lps22hh_trigger_one_shot(void)
{
  if (!initialized)
  {
    return -ENODEV;
  }

  uint8_t val;
  int ret = lps22hh_read_reg(LPS22HH_REG_CTRL_REG2, &val);
  if (ret)
  {
    return ret;
  }

  return lps22hh_write_reg(LPS22HH_REG_CTRL_REG2, val | LPS22HH_CTRL2_ONE_SHOT);
}

int lps22hh_read(lps22hh_data_t *data)
{
  if (!initialized)
  {
    return -ENODEV;
  }
  if (!data)
  {
    return -EINVAL;
  }

  /* Trigger one-shot measurement */
  int ret = lps22hh_trigger_one_shot();
  if (ret)
  {
    LOG_WRN("Failed to trigger one-shot: %d", ret);
  }
  else
  {
    /* Wait for measurement to complete */
    uint8_t status;
    for (int i = 0; i < 50; i++) /* Max 500ms wait */
    {
      k_msleep(10);
      ret = lps22hh_read_reg(LPS22HH_REG_STATUS, &status);
      if (ret == 0 && (status & 0x03) == 0x03) /* Both P_DA and T_DA set */
      {
        break;
      }
    }
  }

  /* Read pressure + temperature (5 bytes from PRESS_OUT_XL) */
  uint8_t raw[5];
  ret = lps22hh_read_regs(LPS22HH_REG_PRESS_OUT_XL, raw, 5);
  if (ret)
  {
    return ret;
  }

  /* Pressure: 24-bit signed, LSB = 1/4096 hPa */
  int32_t press_raw = (int32_t)((raw[2] << 16) | (raw[1] << 8) | raw[0]);
  if (press_raw & 0x800000)
  { /* Sign extend */
    press_raw |= 0xFF000000;
  }
  data->pressure = (float)press_raw / 4096.0f;

  /* Temperature: 16-bit signed, LSB = 0.01 °C */
  int16_t temp_raw = (int16_t)((raw[4] << 8) | raw[3]);
  data->temperature = (float)temp_raw / 100.0f;

  last_data = *data;

  LOG_DBG("LPS22HH: %.2f hPa, %.2f°C", (double)data->pressure, (double)data->temperature);
  return 0;
}

float lps22hh_get_pressure(void) { return last_data.pressure; }

float lps22hh_get_temperature(void) { return last_data.temperature; }
