/*
 * Copyright (c) 2026 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * LED Indicator Service
 *
 * High-level LED pattern control for device status indication.
 * Provides fade, flash, and solid patterns with timed durations.
 *
 * LED Color Mapping (RGB LED):
 * - Red:   LED 0
 * - Green: LED 1
 * - Blue:  LED 2
 * - White: All LEDs
 * - Amber: Red + Green
 */

#ifndef LED_INDICATOR_H
#define LED_INDICATOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED pattern types
 */
typedef enum {
  LED_PATTERN_OFF,        /**< All LEDs off */
  LED_PATTERN_SOLID,      /**< Solid color, full brightness */
  LED_PATTERN_FLASH,      /**< On/off blinking */
  LED_PATTERN_FADE,       /**< Smooth continuous fade in/out (breathing) */
  LED_PATTERN_PULSE,      /**< Quick brightness pulse */
  LED_PATTERN_FADE_PULSE, /**< Smooth fade in/out, then rest, repeating */
} led_pattern_t;

/**
 * @brief LED colors (mapped to RGB LED indices)
 */
typedef enum {
  LED_COLOR_OFF,
  LED_COLOR_RED,
  LED_COLOR_GREEN,
  LED_COLOR_BLUE,
  LED_COLOR_WHITE, /**< All LEDs on */
  LED_COLOR_AMBER, /**< Red + Green */
} led_color_t;

/**
 * @brief CO2 level classifications
 */
typedef enum {
  CO2_LEVEL_GOOD,     /**< < 800 ppm - Green */
  CO2_LEVEL_MODERATE, /**< amber zone (yellow_start .. red_start) */
  CO2_LEVEL_POOR,     /**< > 1500 ppm - Red */
} co2_level_t;

/* CO2 thresholds (ppm) */
#define CO2_GOOD_MAX 800
#define CO2_MODERATE_MAX 1000

/* LED driver channel indices per physical color. Verified on hardware via the
 * boot color test: channel 0 (P2.09) = blue, 1 (P2.08) = red, 2 (P2.06) = green.
 * (The board's RGB order does not match the channel order, so these are not
 * 0/1/2.) */
#define LED_RED 1
#define LED_GREEN 2
#define LED_BLUE 0

/* Default timing constants */
#define LED_FADE_DURATION_MS 3000
#define LED_FLASH_PERIOD_MS 500
#define LED_PAIRING_FLASH_MS 300
#define LED_WHITE_DURATION_MS 3000
#define LED_CO2_DURATION_MS 3000
#define LED_CHARGING_WHITE_MS 3000
#define LED_CHARGING_CO2_MS 5000
#define LED_STATUS_DURATION_MS 5000
#define LED_CONNECTED_FLASH_MS 1500
#define LED_FIND_DEVICE_PERIOD_MS 300
#define LED_FIND_DEVICE_DURATION_MS 5000
#define LED_BATTERY_CRITICAL_PERIOD_MS 250
#define LED_BATTERY_CRITICAL_DURATION_MS 3000

/**
 * @brief Initialize LED indicator service
 *
 * Sets up work queue and timers for non-blocking LED patterns.
 *
 * @return 0 on success, negative errno on failure
 */
int led_indicator_init(void);

/**
 * @brief Stop any active LED pattern
 *
 * Turns off all LEDs and cancels any active pattern.
 */
void led_indicator_stop(void);

/**
 * @brief Check if a pattern is currently active
 *
 * @return true if an LED pattern is running
 */
bool led_indicator_is_active(void);

/* ========================================
 * Blocking Software-PWM Ramp (one-shot transitions)
 * ======================================== */

/**
 * @brief Continue/abort callback for led_indicator_ramp().
 *
 * @return true to keep ramping, false to abort (LED is turned off).
 */
typedef bool (*led_ramp_continue_cb)(void);

/**
 * @brief Blocking software-PWM brightness ramp between two levels.
 *
 * The RGB LED is wired as plain gpio-leds with no hardware PWM, so
 * set_brightness is on/off only and a brightness ramp cannot dim. This
 * bit-bangs the duty cycle at a flicker-free carrier and steps it from
 * @p from_pct to @p to_pct, gamma-corrected so the change looks smooth to the
 * eye. Intended for one-shot power on/off light cues — NOT the workqueue-driven
 * patterns above.
 *
 * BLOCKS the calling thread for ~@p duration_ms (busy-wait carrier), and first
 * stops any running pattern. Settles the LED at @p to_pct on completion.
 *
 * @param color           Color to ramp.
 * @param from_pct        Start brightness 0..100.
 * @param to_pct          End brightness 0..100.
 * @param duration_ms     Ramp duration.
 * @param should_continue Polled once per frame (~20 ms); return false to abort.
 *                        Pass NULL to always run to completion.
 * @return true if the ramp completed, false if @p should_continue aborted it.
 */
bool led_indicator_ramp(led_color_t color, uint8_t from_pct, uint8_t to_pct,
                        uint32_t duration_ms,
                        led_ramp_continue_cb should_continue);

/* ========================================
 * Timed Patterns (auto-stop after duration)
 * ======================================== */

/**
 * @brief Show solid color for specified duration
 *
 * @param color LED color to display
 * @param duration_ms Duration in milliseconds (0 = indefinite)
 */
void led_indicator_show_color(led_color_t color, uint32_t duration_ms);

/**
 * @brief Flash color for specified duration
 *
 * @param color LED color to flash
 * @param period_ms Flash period (on + off time)
 * @param duration_ms Total duration (0 = indefinite)
 */
void led_indicator_flash(led_color_t color, uint32_t period_ms,
                         uint32_t duration_ms);

/**
 * @brief Fade color in and out for specified duration
 *
 * Smooth brightness ramp up and down.
 *
 * @param color LED color to fade
 * @param duration_ms Total fade duration
 */
void led_indicator_fade(led_color_t color, uint32_t duration_ms);

/* ========================================
 * Continuous Patterns (until stopped)
 * ======================================== */

/**
 * @brief Start continuous flashing
 *
 * Continues until led_indicator_stop() is called.
 *
 * @param color LED color to flash
 * @param period_ms Flash period
 */
void led_indicator_start_flash(led_color_t color, uint32_t period_ms);

/**
 * @brief Start continuous flashing with custom on-time
 *
 * @param color LED color to flash
 * @param period_ms Total flash period
 * @param pulse_ms On-time within the period
 */
void led_indicator_start_flash_pulse(led_color_t color, uint32_t period_ms,
                                     uint32_t pulse_ms);

/**
 * @brief Start continuous fading
 *
 * @param color LED color to fade
 * @param period_ms Fade cycle period
 */
void led_indicator_start_fade(led_color_t color, uint32_t period_ms);

/**
 * @brief Start a repeating gentle fade pulse: smooth fade in/out over
 *        fade_ms, then off for the rest of the period. Battery-friendly
 *        "breathing blip" — gradual, not a hard blink.
 *
 * @param color LED color
 * @param period_ms Time between pulses (e.g. 10000 = one breath every 10s)
 * @param fade_ms Duration of the fade in+out window (e.g. 1600)
 */
void led_indicator_start_fade_pulse(led_color_t color, uint32_t period_ms,
                                    uint32_t fade_ms);

/* ========================================
 * High-Level Device State Indicators
 * ======================================== */

/**
 * @brief Get CO2 level classification from PPM reading
 *
 * @param co2_ppm CO2 reading in parts per million
 * @return CO2 level classification
 */
co2_level_t led_indicator_get_co2_level(uint16_t co2_ppm);

/**
 * @brief Get LED color for CO2 level
 *
 * @param co2_ppm CO2 reading in parts per million
 * @return LED color for the CO2 level
 */
led_color_t led_indicator_get_co2_color(uint16_t co2_ppm);

/**
 * @brief Show CO2 level indicator
 *
 * Displays green (good), amber (moderate), or red (poor).
 *
 * @param co2_ppm CO2 reading in PPM
 * @param duration_ms How long to display
 */
void led_indicator_show_co2_level(uint16_t co2_ppm, uint32_t duration_ms);

/**
 * @brief Show low battery indicator
 *
 * Fading red for 3 seconds. Non-blocking.
 */
void led_indicator_show_low_battery(void);

/**
 * @brief Show charging with low battery
 *
 * Slow red flash for a few seconds, then off (light never stays on).
 */
void led_indicator_show_charging_low_battery(void);

/**
 * @brief Show charging with normal battery
 *
 * White for 3 seconds, then:
 * - If device off: white for 5 seconds
 * - If device on: CO2 color for 5 seconds
 *
 * @param device_on true if device is in active mode
 * @param co2_ppm Current CO2 reading (used if device on)
 */
void led_indicator_show_charging_normal(bool device_on, uint16_t co2_ppm);

/**
 * @brief Show pairing mode indicator
 *
 * Continuous flashing blue until connected.
 */
void led_indicator_show_pairing(void);

/**
 * @brief Show connected state + CO2 level
 *
 * White for 3 seconds, then CO2 color for 3 seconds.
 *
 * @param co2_ppm Current CO2 reading
 */
void led_indicator_show_connected(uint16_t co2_ppm);

/**
 * @brief Show calibration indicator
 *
 * Short pulse every few seconds while calibration is active.
 *
 * @param active true to start indicator, false to stop
 */
void led_indicator_show_calibration(bool active);

/**
 * @brief Show overall device-status indicator (single source of truth).
 *
 * Single press / placement on the Qi charger both show this. Colors per the
 * device UX spec, sustained for LED_STATUS_DURATION_MS then off:
 *   Red   = low battery (critical battery uses a fast flash instead)
 *   Blue  = currently connected to a phone
 *   White = not connected (open the app to connect)
 * Non-blocking; auto-stops after its duration.
 *
 * @param battery_low      true if battery is low
 * @param battery_critical true if battery is critically low
 * @param connected        true if a phone is connected right now
 */
void led_indicator_show_status(bool battery_low, bool battery_critical,
                               bool connected);

/**
 * @brief Show critical-battery indicator (fast red flash, ~3s). Non-blocking.
 */
void led_indicator_show_battery_critical(void);

/**
 * @brief Show "find my device" indicator (rapid white flash, ~5s).
 *
 * Non-blocking replacement for the old blocking led_driver sequence so it can
 * be triggered from a BLE command without stalling the connection.
 */
void led_indicator_show_find_device(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_INDICATOR_H */
