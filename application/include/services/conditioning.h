#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Persistent timestamp is valid only after a clean shutdown. */
int conditioning_begin(void);
void conditioning_time_sync(uint32_t epoch);
bool conditioning_due(bool single_shot);
void conditioning_completed(void);
void conditioning_observe(void);
int conditioning_shutdown(void);
