#ifndef AIRSPOT_HEALTH_SERVICE_H
#define AIRSPOT_HEALTH_SERVICE_H
#include <stdbool.h>
#include <stdint.h>
enum health_worker { HEALTH_MAIN, HEALTH_WORKQUEUE, HEALTH_PROTOCOL, HEALTH_OTA, HEALTH_SENSOR, HEALTH_READOUT, HEALTH_COUNT };
/* Progress renews a deadline after latching expiry. Zero disarms. */
void health_progress(enum health_worker worker, uint32_t allowance_ms);
uint32_t health_failed(void);
void health_start(void);
#endif
