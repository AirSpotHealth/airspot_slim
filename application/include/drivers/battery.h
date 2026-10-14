/*
 * Copyright (c) 2026 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Battery Driver
 * nRF54L15 SAADC for battery voltage monitoring
 *
 * Hardware Configuration:
 * - ADC Input: P1.05 (AIN1) - via 0.6x voltage divider
 * - ADC Enable: P1.04 - GPIO to control voltage divider MOSFET
 *
 * Voltage Scaling (from schematic):
 * - Vbatt 4.2V -> Vadc 2.52V (scaling factor 0.6)
 * - Vbatt 3.7V -> Vadc 2.22V
 * - Vbatt 3.3V -> Vadc 1.98V
 */

#ifndef BATTERY_H
#define BATTERY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Low-battery lockout thresholds (millivolts), with hysteresis to avoid the
 * boot/advertise/brownout death-loop near empty:
 *  - Below LOCKOUT while discharging (not on charger): tear down BLE/sensors
 *    and enter the low-power charging lockout.
 *  - Stay locked out until charged back above RECOVER, then clean-reboot.
 * Recovery requires ten continuous seconds of valid readings on charge.
 */
#define BATTERY_LOCKOUT_MV 3300
#define BATTERY_RECOVER_MV 3400
#define BATTERY_RECOVER_STABLE_MS 10000

/**
 * @brief Initialize battery ADC driver
 *
 * Configures ADC channel using device tree and sets up enable GPIO.
 *
 * @return 0 on success, negative errno on failure
 */
int battery_init(void);

/**
 * @brief Read battery voltage
 *
 * Enables voltage divider, reads ADC, applies scaling, disables divider.
 *
 * @return Battery voltage in millivolts (0 on error)
 */
uint16_t battery_read_voltage_mv(void);

/* Fresh (<=5 s), serialized, fail-closed voltage for update authorization. */
int battery_get_ota_voltage(uint16_t *mv);

/**
 * @brief Get battery percentage
 *
 * Approximate usable charge, linearly scaled from the 3.3 V operating cutoff
 * (1%) to 4.2 V (100%). This is not a measured state-of-charge curve.
 *
 * @return Battery percentage (0-100)
 */
uint8_t battery_get_percentage(void);

/**
 * @brief Check if battery is low
 *
 * @return true if battery < 3.3V (operating cutoff)
 */
bool battery_is_low(void);

/**
 * @brief Check if battery is critical
 *
 * @return true if battery < 3.0V (~5%)
 */
bool battery_is_critical(void);

/**
 * @brief Get raw ADC value (for debugging)
 *
 * @return Raw 12-bit ADC value
 */
int32_t battery_get_raw_adc(void);

/**
 * @brief Get ADC millivolt reading (before scaling)
 *
 * @return ADC voltage in millivolts (before divider scaling)
 */
int32_t battery_get_adc_mv(void);

/**
 * @brief Get the filtered (EMA) battery voltage
 *
 * Returns the exponential moving average filtered battery voltage
 * for stable readings. This is the same value returned by
 * battery_read_voltage_mv() but without triggering a new ADC read.
 *
 * @return Battery voltage in mV after EMA filtering
 */
int32_t battery_get_filtered_mv(void);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_H */
