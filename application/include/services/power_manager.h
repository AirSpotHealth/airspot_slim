/*
 * Copyright (c) 2026 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdbool.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file power_manager.h
 * @brief Power Manager for AirSpot Slim
 *
 * Manages device power states including System OFF (shipping mode).
 *
 * Power ON/OFF gesture: Tap → Tap-and-hold 3 seconds
 * - Prevents accidental power toggles from simple long-press
 * - Same gesture works for both power on and power off
 *
 * System OFF state:
 * - Ultra-low power (~1-3 µA)
 * - All peripherals disabled
 * - Only wakes on button GPIO
 * - Charging continues but doesn't wake device
 */

/* Gesture timing constants */
#define POWER_GESTURE_TAP_MAX_MS 300 /* Max duration for initial tap */
#define POWER_GESTURE_WINDOW_MS                                                \
  1000 /* Window to start second press (increased for polling) */
#define POWER_GESTURE_HOLD_DURATION_MS                                         \
  3000                                 /* Hold duration for power toggle       \
                                        */
#define POWER_GESTURE_LED_BLINK_MS 500 /* LED blink interval during hold */

/**
 * @brief Initialize power manager
 *
 * Sets up GPIO for wake detection and resets gesture state.
 *
 * @return 0 on success, negative error code on failure
 */
int power_manager_init(void);

/**
 * @brief Check if device woke from System OFF
 *
 * Should be called early in main() to determine boot reason.
 *
 * @return true if woke from System OFF, false otherwise
 */
bool power_manager_is_wakeup_from_off(void);

/**
 * @brief Validate wake-up gesture (tap + tap-hold 3s)
 *
 * Called after detecting wake from System OFF. Waits for user to complete
 * the power-on gesture. Shows LED feedback during the hold period.
 *
 * @return true if valid power-on gesture, false if invalid (should go back to
 * off)
 */
bool power_manager_validate_wakeup_gesture(void);

/**
 * @brief Enter System OFF (shipping mode)
 *
 * Powers down all peripherals, configures button as wake source,
 * and enters ultra-low power System OFF state.
 *
 * This function does NOT return (device resets on wake).
 */
void power_manager_enter_off_state(void);

/**
 * @brief Handle power-off gesture detection (for when device is ON)
 *
 * Called when a tap is detected while device is running.
 * Starts monitoring for the tap-hold-3s gesture to power off.
 *
 * Should be called from button driver callback on TAP event.
 */
void power_manager_on_tap_detected(void);

/**
 * @brief Called when button is pressed during gesture detection
 */
void power_manager_on_button_pressed(void);

/**
 * @brief Called when button is released during gesture detection
 */
void power_manager_on_button_released(void);

/**
 * @brief Check if power-off gesture is currently being monitored
 *
 * @return true if waiting for hold after tap, false otherwise
 */
bool power_manager_is_gesture_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* POWER_MANAGER_H */
