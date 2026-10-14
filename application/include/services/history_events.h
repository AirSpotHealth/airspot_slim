/* Screen-compatible event history. Thread context only, except retain_fault. */
#ifndef AIRSPOT_HISTORY_EVENTS_H
#define AIRSPOT_HISTORY_EVENTS_H
#include <services/data_log.h>

struct history_event { uint16_t value; record_type_t type; };
void history_events_boot(uint32_t reset_cause);
int history_event_record(uint16_t value, record_type_t type);
int history_event_batch(const struct history_event *events, size_t count);
void history_time_sync(uint32_t time);
/* One record per normal main-loop iteration; failed writes remain queued. */
int history_events_process(void);
void history_events_flush(void);
int history_events_erase_all(void);
/* No locks or flash access; recovered only after a validated warm reset. */
void history_retain_fault(uint16_t reason);
#endif
