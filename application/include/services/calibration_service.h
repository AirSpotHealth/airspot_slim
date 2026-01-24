/*
 * Copyright (c) 2026 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Calibration Service - Legacy math-based calibration
 *
 * Handles calibration timing and correction calculation.
 */

#ifndef CALIBRATION_SERVICE_H
#define CALIBRATION_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void (*on_start)(void);
  void (*on_countdown)(uint16_t seconds_remaining);
  void (*on_done)(int16_t correction);
} calibration_callbacks_t;

/**
 * @brief Initialize calibration service
 *
 * @param callbacks Optional callbacks for protocol notifications
 */
void calibration_service_init(const calibration_callbacks_t *callbacks);

/**
 * @brief Start calibration sequence
 *
 * @param target_ppm Calibration target (ppm)
 * @return 0 on success, negative errno on failure
 */
int calibration_service_start(uint16_t target_ppm);

/**
 * @brief Feed calibration service with a raw CO2 measurement
 *
 * @param co2_ppm Raw CO2 measurement (ppm)
 */
void calibration_service_process_measurement(uint16_t co2_ppm);

/**
 * @brief Check if calibration is active
 */
bool calibration_service_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* CALIBRATION_SERVICE_H */
