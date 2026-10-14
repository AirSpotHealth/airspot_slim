#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H
#include <stdbool.h>

/* Called from the normal main loop, never from the soft-off/lockout loops.
 * Samples independently of CO2 cadence. True requests low-battery lockout. */
bool battery_monitor_poll(void);
#endif
