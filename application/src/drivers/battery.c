/*
 * Copyright (c) 2026 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Battery Driver Implementation
 * Uses device tree ADC configuration for reliable operation.
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

LOG_MODULE_REGISTER(battery, LOG_LEVEL_INF);

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
#define BATT_VOLTAGE_LOW 3300      /* ~15% */
#define BATT_VOLTAGE_CRITICAL 3000 /* ~5% */
#define BATT_VOLTAGE_EMPTY 2800

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

static bool initialized = false;
static int32_t last_raw_adc = 0;
static int32_t last_adc_mv = 0;

int battery_init(void) {
  int ret;

  if (initialized) {
    return 0;
  }

  LOG_INF("Initializing battery driver...");
  LOG_INF("ADC channels configured: %d", (int)ARRAY_SIZE(adc_channels));

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

  /* Take an initial reading to verify */
  uint16_t voltage = battery_read_voltage_mv();
  LOG_INF("Initial battery reading: %u mV (%u%%)", voltage,
          battery_get_percentage());

  return 0;
}

uint16_t battery_read_voltage_mv(void) {
  int ret;
  int32_t val_mv;
  uint16_t buf;

  if (!initialized) {
    LOG_WRN("Battery driver not initialized");
    return 0;
  }

  struct adc_sequence sequence = {
      .buffer = &buf,
      .buffer_size = sizeof(buf),
  };

#if HAS_ADC_EN_GPIO
  /* Enable voltage divider */
  gpio_pin_set_dt(&adc_en_gpio, 1);
  k_usleep(100); /* Wait for stabilization */
#endif

  /* Initialize sequence from device tree config */
  (void)adc_sequence_init_dt(&adc_channels[0], &sequence);

  /* Read ADC */
  ret = adc_read_dt(&adc_channels[0], &sequence);

#if HAS_ADC_EN_GPIO
  /* Disable voltage divider (power saving) */
  gpio_pin_set_dt(&adc_en_gpio, 0);
#endif

  if (ret < 0) {
    LOG_ERR("ADC read failed: %d", ret);
    return 0;
  }

  /* Store raw value for debugging */
  if (adc_channels[0].channel_cfg.differential) {
    last_raw_adc = (int32_t)((int16_t)buf);
  } else {
    last_raw_adc = (int32_t)buf;
  }

  /* Convert raw to millivolts using device tree config */
  val_mv = last_raw_adc;
  ret = adc_raw_to_millivolts_dt(&adc_channels[0], &val_mv);
  if (ret < 0) {
    LOG_WRN("ADC mV conversion not available, using raw value");
    /* Fallback: assume 0.6V reference, 1/4 gain, 12-bit */
    /* ADC_mV = raw * (600mV * 4) / 4095 = raw * 2400 / 4095 */
    val_mv = (last_raw_adc * 2400) / 4095;
  }

  last_adc_mv = val_mv;

  /* Apply voltage divider inverse to get actual battery voltage */
  /* Vbatt = Vadc / 0.6 */
  float battery_mv = (float)val_mv * VOLTAGE_DIVIDER_INVERSE;

  LOG_DBG("ADC: raw=%d, adc_mv=%d, batt_mv=%.0f", last_raw_adc, last_adc_mv,
          battery_mv);

  return (uint16_t)battery_mv;
}

uint8_t battery_get_percentage(void) {
  uint16_t mv = battery_read_voltage_mv();

  if (mv >= BATT_VOLTAGE_FULL) {
    return 100;
  }
  if (mv <= BATT_VOLTAGE_EMPTY) {
    return 0;
  }

  /* Linear approximation between empty and full */
  uint32_t range = BATT_VOLTAGE_FULL - BATT_VOLTAGE_EMPTY;
  uint32_t level = mv - BATT_VOLTAGE_EMPTY;

  return (uint8_t)((level * 100) / range);
}

bool battery_is_low(void) {
  return battery_read_voltage_mv() < BATT_VOLTAGE_LOW;
}

bool battery_is_critical(void) {
  return battery_read_voltage_mv() < BATT_VOLTAGE_CRITICAL;
}

int32_t battery_get_raw_adc(void) { return last_raw_adc; }

int32_t battery_get_adc_mv(void) { return last_adc_mv; }
