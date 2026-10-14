#ifndef AIRSPOT_DIAGNOSTICS_H
#define AIRSPOT_DIAGNOSTICS_H
#include <stdint.h>
#include <stddef.h>
enum diagnostic_event {
    DIAG_BOOT = 1, DIAG_WATCHDOG, DIAG_STARTUP_FAILED, DIAG_TRIAL,
    DIAG_CONFIRMED, DIAG_ROLLBACK_REQUESTED, DIAG_SENSOR_FAILED,
    DIAG_OTA_REJECTED, DIAG_STORAGE_FAILED,
    DIAG_I2C_FAILED, DIAG_SAMPLE_FAILED, DIAG_CO2_CONTEXT,
    DIAG_RECOVERY_RESULT, DIAG_FACTORY_RESET_FAILED, DIAG_SENSOR_POWER
};
void diagnostics_init(uint32_t reset_cause);
void diagnostics_record(enum diagnostic_event event, uint32_t detail);
void diagnostics_watchdog(uint32_t workers);
void diagnostics_reboot_event(enum diagnostic_event event, uint32_t detail);
/* Payload: schema,status,index,count,head-sequence BE, optional 28-byte record. */
size_t diagnostics_read(uint8_t index, uint32_t expected_head, uint8_t *out);
#endif
