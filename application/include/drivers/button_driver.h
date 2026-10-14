/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BUTTON_DRIVER_H
#define BUTTON_DRIVER_H

#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

	/**
	 * @file button_driver.h
	 * @brief Button driver interface
	 *
	 * This module provides button handling with debouncing and
	 * event callbacks for press, release, and long-press events.
	 */

	/* Button event types */
	typedef enum
	{
		BUTTON_EVENT_PRESSED,
		BUTTON_EVENT_RELEASED,
		BUTTON_EVENT_LONG_PRESS,
		BUTTON_EVENT_RECOVERY_WARNING,
		BUTTON_EVENT_RECOVERY_READY,
		BUTTON_EVENT_RECOVERY_CANCELLED,
		BUTTON_EVENT_RECOVERY_CONFIRMED,
	} button_event_t;

	/* Button callback function type */
	typedef void (*button_callback_t)(button_event_t event);

	/**
	 * @brief Initialize button driver
	 *
	 * @return 0 on success, negative error code on failure
	 */
	int button_driver_init(void);

	/**
	 * @brief Register button event callback
	 *
	 * @param callback Function to call on button events (NULL to unregister)
	 */
	void button_driver_register_callback(button_callback_t callback);

	/**
	 * @brief Get current button state
	 *
	 * @param pressed Pointer to store state (true = pressed, false = released)
	 * @return 0 on success, negative error code on failure
	 */
	int button_driver_get_state(bool *pressed);

	/** Read the GPIO level directly, bypassing the debounced cached state. */
	int button_driver_read_now(bool *pressed);

	/** Sleep until an interrupt edge, or timeout (main-thread power transition). */
	int button_driver_wait_for_change(k_timeout_t timeout);

	/** Disable event delivery and drain work before arming System OFF wake. */
	int button_driver_prepare_wakeup(void);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_DRIVER_H */
