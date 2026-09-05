#ifndef SUPPLY_DIAGNOSTIC_H
#define SUPPLY_DIAGNOSTIC_H
#include <stdint.h>

/* Diagnostic only: 0 restores the schematic default, 1 requests PWM for
 * five minutes, 2 queries. Never persisted. The timeout also covers BLE loss. */
typedef struct {
  uint8_t pwm_active;
  uint16_t seconds_remaining;
} supply_diagnostic_t;
int supply_diagnostic_control(uint8_t action, supply_diagnostic_t *state);
#endif
