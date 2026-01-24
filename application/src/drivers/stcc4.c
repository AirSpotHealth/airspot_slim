/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * STCC4 CO2 Sensor Driver Implementation
 *
 * Reference: Sensirion STCC4 Datasheet
 * This is a thermal conductivity based CO2 sensor.
 * Requires external SHT40 for temperature/humidity compensation.
 */

#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/i2c_bus.h>
#include <drivers/sht40.h>
#include <drivers/stcc4.h>

LOG_MODULE_REGISTER(stcc4, LOG_LEVEL_DBG);

/* STCC4 I2C Commands (from Sensirion STCC4 datasheet and working minimal uart
 * code) */
#define STCC4_CMD_GET_PRODUCT_ID_MSB 0x36
#define STCC4_CMD_GET_PRODUCT_ID_LSB                                           \
  0x5B /* GetProductID command for testing                                     \
        */
#define STCC4_CMD_START_CONTINUOUS_MSB 0x21
#define STCC4_CMD_START_CONTINUOUS_LSB 0x8B /* Correct: 0x218B */
#define STCC4_CMD_STOP_MSB 0x3F
#define STCC4_CMD_STOP_LSB 0x86
#define STCC4_CMD_READ_MEASUREMENT_MSB 0xEC
#define STCC4_CMD_READ_MEASUREMENT_LSB 0x05 /* Correct: 0xEC05 */
#define STCC4_CMD_SET_AMBIENT_MSB 0x21
#define STCC4_CMD_SET_AMBIENT_LSB 0x0D
#define STCC4_CMD_SINGLE_SHOT_MSB 0x21
#define STCC4_CMD_SINGLE_SHOT_LSB                                              \
  0x9D /* Correct: 0x219D - per working code                                   \
        */
#define STCC4_CMD_ENTER_SLEEP_MSB 0x36
#define STCC4_CMD_ENTER_SLEEP_LSB 0x50 /* Sleep mode: 0x3650 */
#define STCC4_CMD_EXIT_SLEEP 0x00      /* Wake up: single byte 0x00 */
#define STCC4_CMD_CONDITIONING_MSB 0x29
#define STCC4_CMD_CONDITIONING_LSB 0xBC /* Conditioning: 0x29BC */
#define STCC4_CMD_ENABLE_TESTING_MSB 0x3F
#define STCC4_CMD_ENABLE_TESTING_LSB 0xBC /* Enable testing mode: 0x3FBC */
#define STCC4_CMD_DISABLE_TESTING_MSB 0x3F
#define STCC4_CMD_DISABLE_TESTING_LSB 0x3D /* Disable testing mode: 0x3F3D */

/* CRC polynomial for Sensirion sensors */
#define STCC4_CRC_POLYNOMIAL 0x31
#define STCC4_CRC_INIT 0xFF

/* Timing */
#define STCC4_MEASUREMENT_INTERVAL_MS 1000
#define STCC4_SINGLE_SHOT_TIME_MS 500    /* Single shot measurement ~500ms */
#define STCC4_CONDITIONING_TIME_MS 22000 /* Conditioning cycle 22 seconds */
#define STCC4_SLEEP_ENTER_TIME_MS 1      /* Time for sleep command to execute */
#define STCC4_SLEEP_EXIT_TIME_MS 5       /* Time to wake from sleep */
#define STCC4_STOP_TIME_MS 1200          /* Stop command execution time */
#define STCC4_READ_RETRY_DELAY_MS 150    /* Retry delay when data not ready */
#define STCC4_READ_RETRY_COUNT 2         /* Additional retries after first */

static bool initialized = false;
static stcc4_data_t last_data = {.co2_ppm = 0, .valid = false};
static stcc4_mode_t current_mode = STCC4_MODE_SINGLE_SHOT;
static int64_t continuous_start_ms = 0;

/* CRC-8 calculation (same as SHT40) */
static uint8_t stcc4_crc8(const uint8_t *data, size_t len) {
  uint8_t crc = STCC4_CRC_INIT;

  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ STCC4_CRC_POLYNOMIAL;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

static int stcc4_write_cmd(uint8_t msb, uint8_t lsb) {
  uint8_t cmd[2] = {msb, lsb};
  return i2c_bus_write(I2C_ADDR_STCC4, cmd, 2);
}

int stcc4_init(void) {
  if (initialized) {
    return 0;
  }

  if (!i2c_bus_is_ready()) {
    LOG_ERR("I2C bus not ready");
    return -ENODEV;
  }

  /* Note: SHT40 is initialized before STCC4 in sensor_manager.
   * The first measurement happens after init, so we don't check here.
   * Compensation data will be provided when sensor_manager reads sensors. */

  /* STCC4 initialization: No special initialization needed */
  /* The sensor works directly after power-up - no ExitSleepMode or reset
   * required */
  /* Per working minimal uart code: sensor can be used directly */

  initialized = true;
  LOG_INF("STCC4 CO2 sensor initialized");
  return 0;
}

int stcc4_get_product_id(uint8_t *product_id, size_t len) {
  /* GetProductID command (0x365B) - used to verify sensor communication */
  /* Per working minimal uart code: cmd_stcc4_test uses this */
  if (!product_id || len < 9) {
    return -EINVAL;
  }

  int ret = stcc4_write_cmd(STCC4_CMD_GET_PRODUCT_ID_MSB,
                            STCC4_CMD_GET_PRODUCT_ID_LSB);
  if (ret) {
    return ret;
  }

  k_msleep(1);
  return i2c_bus_read(I2C_ADDR_STCC4, product_id, len);
}

int stcc4_start_continuous(void) {
  if (!initialized) {
    return -ENODEV;
  }

  int ret = stcc4_write_cmd(STCC4_CMD_START_CONTINUOUS_MSB,
                            STCC4_CMD_START_CONTINUOUS_LSB);
  if (ret == 0) {
    current_mode = STCC4_MODE_CONTINUOUS;
    continuous_start_ms = k_uptime_get();
    LOG_INF("STCC4 continuous mode started");
  }
  return ret;
}

int stcc4_stop(void) {
  if (!initialized) {
    return -ENODEV;
  }

  int ret = stcc4_write_cmd(STCC4_CMD_STOP_MSB, STCC4_CMD_STOP_LSB);
  if (ret == 0) {
    current_mode = STCC4_MODE_SINGLE_SHOT;
    continuous_start_ms = 0;
    k_msleep(STCC4_STOP_TIME_MS);
    LOG_INF("STCC4 measurement stopped");
  }
  return ret;
}

int stcc4_trigger_single_shot(void) {
  if (!initialized) {
    return -ENODEV;
  }

  /* Update ambient conditions from SHT40 first */
  float temp = sht40_get_temperature();
  float hum = sht40_get_humidity();

  if (!isnan(temp) && !isnan(hum)) {
    stcc4_set_ambient(temp, hum);
  }

  return stcc4_write_cmd(STCC4_CMD_SINGLE_SHOT_MSB, STCC4_CMD_SINGLE_SHOT_LSB);
}

int stcc4_read(stcc4_data_t *data) {
  if (!initialized) {
    return -ENODEV;
  }
  if (!data) {
    return -EINVAL;
  }

  /* Ensure continuous mode has had time to produce data */
  if (current_mode == STCC4_MODE_CONTINUOUS && continuous_start_ms > 0) {
    int64_t elapsed = k_uptime_get() - continuous_start_ms;
    if (elapsed < STCC4_MEASUREMENT_INTERVAL_MS) {
      k_msleep(STCC4_MEASUREMENT_INTERVAL_MS - (int32_t)elapsed);
    }
  }

  /* Send read measurement command */
  int ret = stcc4_write_cmd(STCC4_CMD_READ_MEASUREMENT_MSB,
                            STCC4_CMD_READ_MEASUREMENT_LSB);
  if (ret) {
    return ret;
  }

  k_msleep(1);

  /* Read 12 bytes: CO2 (2 + CRC) + Temp (2 + CRC) + RH (2 + CRC) + Status (2 + CRC) */
  uint8_t raw[12];
  for (int attempt = 0; attempt <= STCC4_READ_RETRY_COUNT; attempt++) {
    ret = i2c_bus_read(I2C_ADDR_STCC4, raw, sizeof(raw));
    if (ret == 0) {
      /* Verify CRC for each word */
      if (stcc4_crc8(&raw[0], 2) != raw[2]) {
        LOG_WRN("STCC4 CO2 CRC mismatch");
        ret = -EIO;
      } else if (stcc4_crc8(&raw[3], 2) != raw[5]) {
        LOG_WRN("STCC4 temperature CRC mismatch");
        ret = -EIO;
      } else if (stcc4_crc8(&raw[6], 2) != raw[8]) {
        LOG_WRN("STCC4 humidity CRC mismatch");
        ret = -EIO;
      } else if (stcc4_crc8(&raw[9], 2) != raw[11]) {
        LOG_WRN("STCC4 status CRC mismatch");
        ret = -EIO;
      }

      if (ret == 0) {
        break;
      }
    }

    if (attempt < STCC4_READ_RETRY_COUNT) {
      k_msleep(STCC4_READ_RETRY_DELAY_MS);
    }
  }

  if (ret) {
    return ret;
  }

  /* Extract CO2 value */
  uint16_t co2_raw = (raw[0] << 8) | raw[1];
  uint16_t temp_raw = (raw[3] << 8) | raw[4];
  uint16_t rh_raw = (raw[6] << 8) | raw[7];
  uint16_t status_raw = (raw[9] << 8) | raw[10];

  data->co2_ppm = co2_raw;
  data->temperature = -45.0f + 175.0f * ((float)temp_raw / 65535.0f);
  data->humidity = -6.0f + 125.0f * ((float)rh_raw / 65535.0f);
  data->status = status_raw;
  data->valid = true;

  last_data = *data;

  LOG_DBG("STCC4: %d ppm CO2", data->co2_ppm);
  return 0;
}

int stcc4_set_ambient(float temperature, float humidity) {
  if (!initialized) {
    return -ENODEV;
  }

  /* STCC4 Datasheet Table 11 - Signal Input Conversion Formulas:
   * Temperature: Input = (T[°C] + 45) * (2^16 - 1) / 175
   * Humidity:    Input = (RH[%RH] + 6) * (2^16 - 1) / 125
   *
   * Command: set_rht_compensation (0xE000), executable during measurement
   */

  /* Clamp values to valid ranges */
  if (temperature < -45.0f)
    temperature = -45.0f;
  if (temperature > 130.0f)
    temperature = 130.0f;
  if (humidity < -6.0f)
    humidity = -6.0f;
  if (humidity > 119.0f)
    humidity = 119.0f;

  /* Convert using datasheet formulas */
  uint16_t temp_raw = (uint16_t)((temperature + 45.0f) * 65535.0f / 175.0f);
  uint16_t hum_raw = (uint16_t)((humidity + 6.0f) * 65535.0f / 125.0f);

  /* Build command: [cmd_msb, cmd_lsb, temp_msb, temp_lsb, crc, rh_msb, rh_lsb,
   * crc] */
  uint8_t cmd[8];
  cmd[0] = 0xE0; /* Command MSB: set_rht_compensation */
  cmd[1] = 0x00; /* Command LSB */
  cmd[2] = (temp_raw >> 8) & 0xFF;
  cmd[3] = temp_raw & 0xFF;
  cmd[4] = stcc4_crc8(&cmd[2], 2);
  cmd[5] = (hum_raw >> 8) & 0xFF;
  cmd[6] = hum_raw & 0xFF;
  cmd[7] = stcc4_crc8(&cmd[5], 2);

  int ret = i2c_bus_write(I2C_ADDR_STCC4, cmd, 8);
  if (ret != 0) {
    LOG_DBG("STCC4 set_rht_compensation failed: %d", ret);
    return ret;
  }

  LOG_DBG("STCC4 RH/T compensation set: T=%.1f°C (%u), RH=%.1f%% (%u)",
          (double)temperature, temp_raw, (double)humidity, hum_raw);
  return 0;
}

int stcc4_set_pressure(uint16_t pressure_hpa) {
  if (!initialized) {
    return -ENODEV;
  }

  /* STCC4 Datasheet Table 11 - Pressure Input Conversion:
   * Input = P[Pa] / 2
   * Example: 101300 Pa → Input = 50650
   *
   * Command: set_pressure_compensation (0xE016), executable during measurement
   * Valid range: 40000 to 110000 Pa (clipped by sensor)
   * Default: 101300 Pa, reset on power cycle
   */

  /* Convert hPa to Pascals */
  uint32_t pressure_pa = (uint32_t)pressure_hpa * 100;

  /* Clamp to valid range per datasheet (40000 - 110000 Pa) */
  if (pressure_pa < 40000) {
    pressure_pa = 40000;
  } else if (pressure_pa > 110000) {
    pressure_pa = 110000;
  }

  /* Apply datasheet formula: Input = P[Pa] / 2 */
  uint16_t pressure_input = (uint16_t)(pressure_pa / 2);

  uint8_t cmd[5];
  cmd[0] = 0xE0; /* Command MSB: set_pressure_compensation */
  cmd[1] = 0x16; /* Command LSB per datasheet */
  cmd[2] = (pressure_input >> 8) & 0xFF; /* Pressure MSB */
  cmd[3] = pressure_input & 0xFF;        /* Pressure LSB */
  cmd[4] = stcc4_crc8(&cmd[2], 2);       /* CRC of pressure bytes */

  int ret = i2c_bus_write(I2C_ADDR_STCC4, cmd, 5);
  if (ret != 0) {
    LOG_DBG("STCC4 set_pressure_compensation failed: %d", ret);
    return ret;
  }

  LOG_DBG("STCC4 pressure compensation: %u hPa (%u Pa, input=%u)", pressure_hpa,
          (uint16_t)pressure_pa, pressure_input);
  return 0;
}

uint16_t stcc4_get_co2(void) { return last_data.valid ? last_data.co2_ppm : 0; }

bool stcc4_data_ready(void) {
  /* In continuous mode, data is ready after measurement interval */
  /* In single-shot, need to poll or wait */
  return last_data.valid;
}

stcc4_mode_t stcc4_get_mode(void) { return current_mode; }

int stcc4_enter_sleep_mode(void) {
  if (!initialized) {
    return -ENODEV;
  }

  /* Stop any ongoing measurement first */
  stcc4_stop();

  /* Send enter sleep command (0x3650) */
  int ret =
      stcc4_write_cmd(STCC4_CMD_ENTER_SLEEP_MSB, STCC4_CMD_ENTER_SLEEP_LSB);
  if (ret == 0) {
    k_msleep(STCC4_SLEEP_ENTER_TIME_MS);
    LOG_INF("STCC4 entered sleep mode");
  }
  return ret;
}

int stcc4_exit_sleep_mode(void) {
  if (!initialized) {
    return -ENODEV;
  }

  /* Send single-byte 0x00 to wake sensor */
  uint8_t wake_cmd = STCC4_CMD_EXIT_SLEEP;
  int ret = i2c_bus_write(I2C_ADDR_STCC4, &wake_cmd, 1);
  ARG_UNUSED(ret); /* NACK is expected, ignore result */

  /* Note: Sensor may NACK this command, which is expected behavior */
  /* Wait for sensor to wake up regardless of return value */
  k_msleep(STCC4_SLEEP_EXIT_TIME_MS);

  LOG_INF("STCC4 exited sleep mode");
  return 0; /* Always return success as NACK is expected */
}

int stcc4_perform_conditioning(void) {
  if (!initialized) {
    return -ENODEV;
  }

  LOG_INF("STCC4 performing conditioning (22 sec)...");

  /* Send conditioning command (0x29BC) */
  int ret =
      stcc4_write_cmd(STCC4_CMD_CONDITIONING_MSB, STCC4_CMD_CONDITIONING_LSB);
  if (ret != 0) {
    LOG_ERR("STCC4 conditioning command failed: %d", ret);
    return ret;
  }

  /* Wait for conditioning to complete - 22 seconds per datasheet */
  k_msleep(STCC4_CONDITIONING_TIME_MS);

  LOG_INF("STCC4 conditioning complete");
  return 0;
}

int stcc4_enable_testing_mode(void) {
  if (!initialized) {
    return -ENODEV;
  }

  int ret = stcc4_write_cmd(STCC4_CMD_ENABLE_TESTING_MSB,
                            STCC4_CMD_ENABLE_TESTING_LSB);
  if (ret == 0) {
    LOG_INF("STCC4 testing mode enabled (ASC paused)");
  }
  return ret;
}

int stcc4_disable_testing_mode(void) {
  if (!initialized) {
    return -ENODEV;
  }

  int ret = stcc4_write_cmd(STCC4_CMD_DISABLE_TESTING_MSB,
                            STCC4_CMD_DISABLE_TESTING_LSB);
  if (ret == 0) {
    LOG_INF("STCC4 testing mode disabled (ASC active)");
  }
  return ret;
}
