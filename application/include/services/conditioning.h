#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Last sample is checkpointed every minute and on clean shutdown.
 * Phone time resolves elapsed off-time; abrupt loss can overestimate it by
 * up to one checkpoint interval while sampling and storage are healthy. */
int conditioning_begin(void);
void conditioning_time_sync(uint32_t epoch);
bool conditioning_due(bool single_shot);
void conditioning_completed(void);
void conditioning_observe(void);
int conditioning_shutdown(void);
