#ifndef AIRSPOT_OTA_SERVICE_H
#define AIRSPOT_OTA_SERVICE_H
#include <stdbool.h>
#include <services/sensor_manager.h>
void ota_service_init(void);
void ota_service_startup_complete(bool healthy);
void ota_service_observe_sample(const sensor_data_t *data);
void ota_service_poll(void);
#endif
