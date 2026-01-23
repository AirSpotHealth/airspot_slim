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
  LED_PATTERN_OFF,   /**< All LEDs off */
  LED_PATTERN_SOLID, /**< Solid color, full brightness */
  LED_PATTERN_FLASH, /**< On/off blinking */
  LED_PATTERN_FADE,  /**< Smooth fade in/out */
  LED_PATTERN_PULSE, /**< Quick brightness pulse */
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
  CO2_LEVEL_MODERATE, /**< 800-1500 ppm - Amber */
  CO2_LEVEL_POOR,     /**< > 1500 ppm - Red */
} co2_level_t;

/* CO2 thresholds (ppm) */
#define CO2_GOOD_MAX 800
#define CO2_MODERATE_MAX 1500

/* LED indices */
#define LED_RED 0
#define LED_GREEN 1
#define LED_BLUE 2

/* Default timing constants */
#define LED_FADE_DURATION_MS 3000
#define LED_FLASH_PERIOD_MS 500
#define LED_PAIRING_FLASH_MS 300
#define LED_WHITE_DURATION_MS 3000
#define LED_CO2_DURATION_MS 3000
#define LED_CHARGING_WHITE_MS 3000
#define LED_CHARGING_CO2_MS 5000

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
 * @brief Start continuous fading
 *
 * @param color LED color to fade
 * @param period_ms Fade cycle period
 */
void led_indicator_start_fade(led_color_t color, uint32_t period_ms);

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
 * Continuous flashing red until stopped.
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

#ifdef __cplusplus
}
#endif

#endif /* LED_INDICATOR_H */
