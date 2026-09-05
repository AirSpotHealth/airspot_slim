/*
 * Copyright (c) 2025 AirSpot
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AIRSPOT_VERSION_H
#define AIRSPOT_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/app_version.h>

#define AIRSPOT_FIRMWARE_VERSION_MAJOR APP_VERSION_MAJOR
#define AIRSPOT_FIRMWARE_VERSION_MINOR APP_VERSION_MINOR
#define AIRSPOT_FIRMWARE_VERSION_PATCH APP_PATCHLEVEL
#define AIRSPOT_FIRMWARE_VERSION_STRING APP_VERSION_STRING

#ifdef __cplusplus
}
#endif

#endif /* AIRSPOT_VERSION_H */
