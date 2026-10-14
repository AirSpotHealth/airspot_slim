/*
 * Copyright (c) 2026 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Qi Charger Detection Driver
 * GPIO-based Qi receiver status detection. This indicates pad presence, not
 * the nPM battery-charge/complete/fault state.
 *
 * Hardware Configuration:
 * - Pin: P0.00 (NRF_QI_nCHG)
 * - Active LOW: 0 = Qi pad detected, 1 = not detected
 */

#ifndef QI_CHARGER_H
#define QI_CHARGER_H

#include <stdbool.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Callback for charging status changes
     *
     * @param is_charging true if the Qi receiver reports a pad
     */
    typedef void (*qi_charger_callback_t)(bool is_charging);

    /**
     * @brief Initialize Qi charger detection
     *
     * Configures P0.00 as input with interrupt for charging status.
     *
     * @return 0 on success, negative errno on failure
     */
    int qi_charger_init(void);

    /** Wait for the initial debounced status; zero on success, negative on timeout/error. */
    int qi_charger_wait_ready(k_timeout_t timeout);

    /**
     * @brief Check if device is currently charging
     *
     * @return true if charging (Qi pad detected)
     */
    bool qi_charger_is_charging(void);

    /**
     * @brief Register callback for charging status changes
     *
     * @param callback Function to call when charging status changes
     */
    void qi_charger_register_callback(qi_charger_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif /* QI_CHARGER_H */
