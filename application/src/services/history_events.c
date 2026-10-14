#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>
#include <services/history_events.h>
#include <services/user_settings.h>
#include <services/diagnostics.h>

LOG_MODULE_REGISTER(history_events, CONFIG_AIRSPOT_LOG_LEVEL);
#define HISTORY_QUEUE_SIZE 64
#define EVENT_RESERVE 8
#define FAULT_MAGIC 0x48534654U
struct pending_record {
  uint32_t timestamp;
  uint16_t value;
  record_type_t type;
  bool retained_fault;
};
static struct pending_record pending[HISTORY_QUEUE_SIZE];
static unsigned head, count;
K_MUTEX_DEFINE(history_mutex);
K_MUTEX_DEFINE(history_writer_mutex);
static volatile struct { uint32_t magic, reason, inverse; } fault __noinit;

static uint32_t timestamp_now(void)
{
  uint32_t time = user_settings_is_time_set() ? user_settings_get_time_now()
                                            : (uint32_t)(k_uptime_get() / 1000);
  /* Zero is an empty-record sentinel in the legacy history scanner. */
  return time ? time : 1;
}

/* Caller holds history_mutex. A group is accepted entirely or rejected. */
static int enqueue(const struct history_event *events, size_t length, uint32_t ts)
{
  if (!events || !length || length > HISTORY_QUEUE_SIZE) return -EINVAL;
  unsigned limit = HISTORY_QUEUE_SIZE;
  for (size_t i = 0; i < length; i++) {
    if (events[i].type < RECORD_TYPE_CO2 || events[i].type > RECORD_TYPE_FAULT_RESET)
      return -EINVAL;
    if (events[i].type == RECORD_TYPE_CO2) limit = HISTORY_QUEUE_SIZE - EVENT_RESERVE;
  }
  if (count + length > limit) return -ENOSPC;
  for (size_t i = 0; i < length; i++) {
    pending[(head + count++) % HISTORY_QUEUE_SIZE] = (struct pending_record) {
      .timestamp = ts, .value = events[i].value, .type = events[i].type,
    };
  }
  return 0;
}

int history_event_batch(const struct history_event *events, size_t length)
{
  k_mutex_lock(&history_mutex, K_FOREVER);
  int rc = enqueue(events, length, timestamp_now());
  k_mutex_unlock(&history_mutex);
  if (rc) {
    LOG_ERR("History enqueue failed: %d", rc);
    diagnostics_record(DIAG_STORAGE_FAILED, (uint32_t)-rc);
  }
  return rc;
}

int history_event_record(uint16_t value, record_type_t type)
{
  const struct history_event event = {value, type};
  return history_event_batch(&event, 1);
}

void history_time_sync(uint32_t time)
{
  k_mutex_lock(&history_mutex, K_FOREVER);
  bool first = !user_settings_is_time_set();
  uint32_t old_time = timestamp_now();
  user_settings_set_timebase(time);
  /* Screen uses the new timestamp and the old unsynced clock's low 16 bits. */
  struct history_event event = {(uint16_t)old_time, RECORD_TYPE_TIME_SET};
  int rc = first ? enqueue(&event, 1, time) : 0;
  k_mutex_unlock(&history_mutex);
  if (rc) diagnostics_record(DIAG_STORAGE_FAILED, (uint32_t)-rc);
}

int history_events_process(void)
{
  k_mutex_lock(&history_writer_mutex, K_FOREVER);
  k_mutex_lock(&history_mutex, K_FOREVER);
  if (!count) {
    k_mutex_unlock(&history_mutex);
    k_mutex_unlock(&history_writer_mutex);
    return 0;
  }
  struct pending_record item = pending[head];
  k_mutex_unlock(&history_mutex);
  int rc = data_log_store_record(item.timestamp, item.value, item.type);
  if (!rc) {
    k_mutex_lock(&history_mutex, K_FOREVER);
    head = (head + 1) % HISTORY_QUEUE_SIZE;
    count--;
    if (item.retained_fault) fault.magic = 0;
    k_mutex_unlock(&history_mutex);
  } else {
    diagnostics_record(DIAG_STORAGE_FAILED, (uint32_t)-rc);
  }
  k_mutex_unlock(&history_writer_mutex);
  return rc ? rc : 1;
}

void history_events_flush(void)
{
  /* Bounded even if a producer continues to enqueue during shutdown. */
  for (unsigned i = 0; i < HISTORY_QUEUE_SIZE; i++)
    if (history_events_process() <= 0) break;
}

int history_events_erase_all(void)
{
  k_mutex_lock(&history_writer_mutex, K_FOREVER);
  k_mutex_lock(&history_mutex, K_FOREVER);
  unsigned old_count = count;
  k_mutex_unlock(&history_mutex);
  int rc = data_log_erase_all();
  if (!rc) {
    k_mutex_lock(&history_mutex, K_FOREVER);
    head = (head + old_count) % HISTORY_QUEUE_SIZE;
    count -= old_count;
    fault.magic = 0;
    k_mutex_unlock(&history_mutex);
  }
  k_mutex_unlock(&history_writer_mutex);
  return rc;
}

void history_retain_fault(uint16_t reason)
{
  fault.magic = 0;
  fault.reason = reason;
  fault.inverse = ~(uint32_t)reason;
  fault.magic = FAULT_MAGIC;
}

void history_events_boot(uint32_t cause)
{
  /* Existing mobile/Screen reset reason numbers, not Zephyr bit flags. */
  uint16_t reason = (cause & RESET_WATCHDOG) ? 2 :
                    (cause & RESET_CPU_LOCKUP) ? 4 :
                    (cause & RESET_SOFTWARE) ? 3 :
                    (cause & RESET_LOW_POWER_WAKE) ? 5 :
                    (cause & RESET_PIN) ? 1 : 0;
  (void)history_event_record(reason, RECORD_TYPE_RESET_REASON);
  if ((cause & (RESET_SOFTWARE | RESET_WATCHDOG | RESET_CPU_LOCKUP)) &&
      fault.magic == FAULT_MAGIC && fault.reason <= UINT16_MAX &&
      fault.reason == ~fault.inverse) {
    k_mutex_lock(&history_mutex, K_FOREVER);
    struct history_event event = {(uint16_t)fault.reason, RECORD_TYPE_FAULT_RESET};
    if (!enqueue(&event, 1, timestamp_now()))
      pending[(head + count - 1) % HISTORY_QUEUE_SIZE].retained_fault = true;
    k_mutex_unlock(&history_mutex);
  } else {
    fault.magic = 0;
  }
}
