/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include <zephyr/types.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @file led_driver.h
     * @brief LED driver interface
     *
     * This module provides LED control functionality optimized for
     * RGB LED control pins on AirSpot Slim board.
     */

    /**
     * @brief Initialize LED driver
     *
     * @return 0 on success, negative error code on failure
     */
    int led_driver_init(void);

    /**
     * @brief Set LED state
     *
     * @param led_id LED index (0 to AIRSPOT_LED_COUNT-1)
     * @param state true to turn on, false to turn off
     * @return 0 on success, negative error code on failure
     */
    int led_driver_set(uint8_t led_id, bool state);

    /**
     * @brief Toggle LED state
     *
     * @param led_id LED index (0 to AIRSPOT_LED_COUNT-1)
     * @return 0 on success, negative error code on failure
     */
    int led_driver_toggle(uint8_t led_id);

    /**
     * @brief Set LED brightness (0-100)
     *
     * @param led_id LED index
     * @param brightness Brightness level (0-100)
     * @return 0 on success, negative error code on failure
     */
    int led_driver_set_brightness(uint8_t led_id, uint8_t brightness);

    /**
     * @brief Get LED state
     *
     * @param led_id LED index
     * @param state Pointer to store state (true = on, false = off)
     * @return 0 on success, negative error code on failure
     */
    int led_driver_get_state(uint8_t led_id, bool *state);

    /**
     * @brief Play startup LED sequence
     */
    void led_driver_startup_sequence(void);

    /**
     * @brief Turn off all LEDs
     */
    void led_driver_all_off(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_DRIVER_H */
