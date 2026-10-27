#ifndef AIRSPOT_INITIAL_SETTLING_H
#define AIRSPOT_INITIAL_SETTLING_H
#include <stdbool.h>
#include <stdint.h>
void initial_settling_observe(bool valid, bool single_shot, bool paired, bool low_battery);
int initial_settling_reset(void);
/* Caller holds the sensor mutex, shared with observe/reset. */
uint32_t initial_settling_remaining_seconds(void);
#endif
