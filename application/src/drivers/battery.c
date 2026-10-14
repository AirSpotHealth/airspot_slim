/*
 * Copyright (c) 2026 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Battery Driver Implementation
 * Uses device tree ADC configuration for reliable operation.
 * Implements multi-sample averaging and EMA filtering for stability.
 *
 * Based on working _working_minimal_uart ADC code.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/battery.h>

LOG_MODULE_REGISTER(battery, CONFIG_AIRSPOT_LOG_LEVEL);

/* ============================================================================
 * Hardware Configuration from Schematic
 * ============================================================================
 * Voltage Divider: 0.6 scaling factor
 *   Vbatt 4.2V -> Vadc 2.52V
 *   Vbatt 3.7V -> Vadc 2.22V
 *   Vbatt 3.3V -> Vadc 1.98V
 *
 * To convert ADC voltage to battery voltage: Vbatt = Vadc / 0.6
 * Or equivalently: Vbatt = Vadc * 1.667
 */
#define VOLTAGE_DIVIDER_FACTOR 0.6f
#define VOLTAGE_DIVIDER_INVERSE (1.0f / VOLTAGE_DIVIDER_FACTOR) /* ~1.667 */

/* LiPo voltage thresholds (mV) */
#define BATT_VOLTAGE_FULL 4200
#define BATT_VOLTAGE_NOMINAL 3700
#define BATT_VOLTAGE_LOW BATTERY_LOCKOUT_MV
#define BATT_VOLTAGE_CRITICAL 3000 /* ~5% */

/* ============================================================================
 * ADC Filtering Configuration
 * ============================================================================
 * Multi-sample averaging + Exponential Moving Average (EMA) for stability
 */
#define ADC_NUM_SAMPLES 4       /* Number of samples to average per reading */
#define ADC_SAMPLE_DELAY_US 200 /* Delay between samples (microseconds) */
#define ADC_STABILIZATION_MS 1  /* Voltage divider stabilization time (ms) */
#define EMA_ALPHA_NUM 2         /* EMA alpha numerator (alpha = 2/10 = 0.2) */
#define EMA_ALPHA_DEN 10        /* EMA alpha denominator */

/* ADC channel from device tree (zephyr,user io-channels) */
#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) ||                                   \
    !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No ADC io-channels specified in device tree zephyr,user node"
#endif

#define DT_SPEC_AND_COMMA(node_id, prop, idx)                                  \
  ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

/* ADC channel specs from device tree */
static const struct adc_dt_spec adc_channels[] = {
    DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, DT_SPEC_AND_COMMA)};

/* Battery ADC enable GPIO (P1.04) */
#define BATT_ADC_EN_NODE DT_NODELABEL(batt_adc_en)
#if DT_NODE_EXISTS(BATT_ADC_EN_NODE)
static const struct gpio_dt_spec adc_en_gpio =
    GPIO_DT_SPEC_GET(BATT_ADC_EN_NODE, gpios);
#define HAS_ADC_EN_GPIO 1
#else
#define HAS_ADC_EN_GPIO 0
#warning "Battery ADC enable GPIO not defined in device tree"
#endif

K_MUTEX_DEFINE(battery_mutex);
static bool initialized = false;
static bool voltage_valid;
static int64_t voltage_time;
static uint16_t unfiltered_mv;
static int32_t last_raw_adc = 0;
static int32_t last_adc_mv = 0;

/* Exponential Moving Average state */
static int32_t ema_battery_mv = 0;
static bool ema_initialized = false;

int battery_init(void) {
  int ret;

  if (initialized) {
    return 0;
  }

  LOG_INF("Initializing battery driver...");
  LOG_INF("ADC channels configured: %d", (int)ARRAY_SIZE(adc_channels));
  LOG_INF("Using %d-sample averaging with EMA filter (alpha=%d/%d)",
          ADC_NUM_SAMPLES, EMA_ALPHA_NUM, EMA_ALPHA_DEN);

  /* Setup ADC enable GPIO if available */
#if HAS_ADC_EN_GPIO
  if (!gpio_is_ready_dt(&adc_en_gpio)) {
    LOG_ERR("ADC enable GPIO device not ready");
    return -ENODEV;
  }

  ret = gpio_pin_configure_dt(&adc_en_gpio, GPIO_OUTPUT_INACTIVE);
  if (ret < 0) {
    LOG_ERR("Failed to configure ADC enable GPIO: %d", ret);
    return ret;
  }
  LOG_INF("ADC enable GPIO (P1.04) configured");
#endif

  /* Setup ADC channels from device tree */
  for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
    LOG_INF("Setting up ADC channel %d on %s...", adc_channels[i].channel_id,
            adc_channels[i].dev->name);

    if (!adc_is_ready_dt(&adc_channels[i])) {
      LOG_ERR("ADC controller device %s not ready", adc_channels[i].dev->name);
      return -ENODEV;
    }

    ret = adc_channel_setup_dt(&adc_channels[i]);
    if (ret < 0) {
      LOG_ERR("Could not setup ADC channel %d: %d", i, ret);
      return ret;
    }
  }

  initialized = true;
  LOG_INF("Battery driver initialized successfully");

  /* Take initial readings to prime the EMA filter */
  for (int i = 0; i < 3; i++) {
    battery_read_voltage_mv();
    k_msleep(10);
  }

  LOG_INF("Initial battery reading: %u mV (%u%%)", (uint16_t)ema_battery_mv,
          battery_get_percentage());

  return 0;
}

/**
 * @brief Take a single raw ADC sample
 * @return Raw ADC value, or -1 on error
 */
static int32_t adc_read_single_sample(void) {
  int ret;
  int16_t buf;

  struct adc_sequence sequence = {
      .buffer = &buf,
      .buffer_size = sizeof(buf),
  };

  /* Initialize sequence from device tree config */
  ret = adc_sequence_init_dt(&adc_channels[0], &sequence);
  if (ret < 0) {
    LOG_ERR("ADC sequence setup failed: %d", ret);
    return -1;
  }

  /* Read ADC */
  ret = adc_read_dt(&adc_channels[0], &sequence);
  if (ret < 0) {
    LOG_ERR("ADC read failed: %d", ret);
    return -1;
  }

  /* SAADC samples are signed even in single-ended mode. Negative readings
   * must not wrap into a large positive battery voltage.
   */
  return buf;
}

/**
 * @brief Convert raw ADC value to millivolts
 * @param raw_adc Raw ADC value
 * @return Voltage in millivolts
 */
static int32_t adc_raw_to_mv(int32_t raw_adc) {
  int ret;
  int32_t val_mv = raw_adc;

  ret = adc_raw_to_millivolts_dt(&adc_channels[0], &val_mv);
  if (ret < 0) {
    /* nRF54L has a 0.9V reference. Never fabricate a voltage using the
     * historical nRF52 0.6V fallback if the device configuration is invalid.
     */
    LOG_ERR("ADC voltage conversion failed: %d", ret);
    return ret;
  }

  return val_mv;
}

static uint16_t read_voltage_locked(void) {
  voltage_valid = false;
  int32_t raw_sum = 0;
  int32_t raw_avg;
  int32_t adc_mv;
  int32_t battery_mv;
  int valid_samples = 0;

  if (!initialized) {
    LOG_WRN("Battery driver not initialized");
    return 0;
  }

#if HAS_ADC_EN_GPIO
  /* Enable voltage divider with longer stabilization time */
  int ret = gpio_pin_set_dt(&adc_en_gpio, 1);
  if (ret < 0) {
    LOG_ERR("Battery divider enable failed: %d", ret);
    return (uint16_t)ema_battery_mv;
  }
  k_msleep(ADC_STABILIZATION_MS);
#endif

  /* Discard first sample to eliminate switching transients */
  (void)adc_read_single_sample();
  k_usleep(ADC_SAMPLE_DELAY_US);

  /* Take multiple samples and average */
  for (int i = 0; i < ADC_NUM_SAMPLES; i++) {
    int32_t sample = adc_read_single_sample();
    if (sample >= 0) {
      raw_sum += sample;
      valid_samples++;
    }
    if (i < ADC_NUM_SAMPLES - 1) {
      k_usleep(ADC_SAMPLE_DELAY_US);
    }
  }

#if HAS_ADC_EN_GPIO
  /* Disable voltage divider (power saving) */
  ret = gpio_pin_set_dt(&adc_en_gpio, 0);
  if (ret < 0) {
    LOG_ERR("Battery divider disable failed: %d", ret);
    return (uint16_t)ema_battery_mv;
  }
#endif

  if (valid_samples == 0) {
    LOG_ERR("No valid ADC samples");
    return (uint16_t)ema_battery_mv; /* Return last known value */
  }

  /* Calculate average raw value */
  raw_avg = raw_sum / valid_samples;
  last_raw_adc = raw_avg;

  /* Convert to millivolts */
  adc_mv = adc_raw_to_mv(raw_avg);
  if (adc_mv < 0) {
    return (uint16_t)ema_battery_mv;
  }
  last_adc_mv = adc_mv;

  /* Apply voltage divider inverse to get actual battery voltage */
  battery_mv = (int32_t)((float)adc_mv * VOLTAGE_DIVIDER_INVERSE);

  /* Apply Exponential Moving Average filter */
  /* EMA = alpha * new_value + (1 - alpha) * old_EMA */
  /* Using integer math: EMA = (alpha_num * new + (alpha_den - alpha_num) * old)
   * / alpha_den */
  if (!ema_initialized) {
    ema_battery_mv = battery_mv;
    ema_initialized = true;
  } else {
    ema_battery_mv = (EMA_ALPHA_NUM * battery_mv +
                      (EMA_ALPHA_DEN - EMA_ALPHA_NUM) * ema_battery_mv) /
                     EMA_ALPHA_DEN;
  }

  unfiltered_mv = (uint16_t)battery_mv;
  voltage_time = k_uptime_get();
  voltage_valid = true;

  LOG_DBG("ADC: raw_avg=%d, adc_mv=%d, batt_mv=%d, ema_mv=%d (samples=%d)",
          raw_avg, adc_mv, battery_mv, ema_battery_mv, valid_samples);

  return (uint16_t)ema_battery_mv;
}

uint16_t battery_read_voltage_mv(void) {
  k_mutex_lock(&battery_mutex, K_FOREVER);
  uint16_t mv = read_voltage_locked();
  k_mutex_unlock(&battery_mutex);
  return mv;
}

int battery_get_ota_voltage(uint16_t *mv) {
  if (!mv) return -EINVAL;
  *mv = 0;
  /* MCUmgr's worker may wait briefly for a sensor read, never indefinitely. */
  if (k_mutex_lock(&battery_mutex, K_MSEC(100)) != 0) return -EBUSY;
  if (!voltage_valid || k_uptime_get() - voltage_time >= 5000) {
    (void)read_voltage_locked();
  }
  int rc = voltage_valid ? 0 : -EIO;
  if (!rc) {
    /* A falling supply must not be hidden by the display's EMA filter. */
    *mv = MIN(unfiltered_mv, (uint16_t)ema_battery_mv);
  }
  k_mutex_unlock(&battery_mutex);
  return rc;
}

uint8_t battery_get_percentage(void) {
  /* Use cached EMA value instead of re-reading */
  uint16_t mv =
      ema_initialized ? (uint16_t)ema_battery_mv : battery_read_voltage_mv();

  if (mv >= BATT_VOLTAGE_FULL) {
    return 100;
  }
  if (mv <= BATTERY_LOCKOUT_MV) {
    return 1;
  }

  /* Display usable operating range, not charge below the shutdown cutoff. */
  uint32_t range = BATT_VOLTAGE_FULL - BATTERY_LOCKOUT_MV;
  uint32_t level = mv - BATTERY_LOCKOUT_MV;
  return (uint8_t)(1 + (level * 99) / range);
}

bool battery_is_low(void) {
  return battery_read_voltage_mv() < BATT_VOLTAGE_LOW;
}

bool battery_is_critical(void) {
  return battery_read_voltage_mv() < BATT_VOLTAGE_CRITICAL;
}

int32_t battery_get_raw_adc(void) { return last_raw_adc; }

int32_t battery_get_adc_mv(void) { return last_adc_mv; }

/**
 * @brief Get the filtered (EMA) battery voltage
 * @return Battery voltage in mV after EMA filtering
 */
int32_t battery_get_filtered_mv(void) { return ema_battery_mv; }
