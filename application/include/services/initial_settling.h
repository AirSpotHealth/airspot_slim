#ifndef AIRSPOT_INITIAL_SETTLING_H
#define AIRSPOT_INITIAL_SETTLING_H
#include <stdbool.h>
void initial_settling_observe(bool valid, bool single_shot, bool paired, bool low_battery);
int initial_settling_reset(void);
#endif
