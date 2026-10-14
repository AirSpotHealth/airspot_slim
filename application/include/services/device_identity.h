#ifndef DEVICE_IDENTITY_H
#define DEVICE_IDENTITY_H
#include <stdint.h>

/* Factory-programmed 48-bit identity, big endian. Stable across reset/OTA/NVS
 * erasure; independent of the phone's platform-specific BLE identifier. */
int device_identity_get(uint8_t serial[6]);
#endif
