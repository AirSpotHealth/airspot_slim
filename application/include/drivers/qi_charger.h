/*
 * Copyright (c) 2026 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Qi Charger Detection Driver
 * GPIO-based charging status detection
 *
 * Hardware Configuration:
 * - Pin: P0.00 (NRF_QI_nCHG)
 * - Active LOW: 0 = charging, 1 = not charging
 */

#ifndef QI_CHARGER_H
#define QI_CHARGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Callback for charging status changes
     *
     * @param is_charging true if device is charging
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
