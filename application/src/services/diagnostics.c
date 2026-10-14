/* Bounded fault journal. No sensor history addresses or settings keys reused. */
#include <services/history_events.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/hwinfo.h>
#include <airspot/version.h>
#include <services/diagnostics.h>

#define CAPACITY 16
#define DIAG_RECORD_SIZE 28
static uint8_t records[CAPACITY][DIAG_RECORD_SIZE];
static uint32_t head;
static uint32_t seen;
static bool ready;
static struct k_spinlock lock;
struct event { uint32_t code, detail, uptime; };
K_MSGQ_DEFINE(events, sizeof(struct event), 16, 4);
K_SEM_DEFINE(start, 0, 1);
/* Best effort warm-reset evidence. Never trust retained RAM without validation. */
static struct { uint32_t magic, event, detail, inverse; } retained __noinit;
static uint32_t reset_reason;

static bool valid(const uint8_t *record)
{
    return record[0] == 1 && record[1] >= DIAG_BOOT &&
        record[1] <= DIAG_SENSOR_POWER && sys_get_be32(record + 8) != 0 &&
        crc32_ieee(record, 24) == sys_get_be32(record + 24);
}
static int load(const char *name, size_t len, settings_read_cb read_cb, void *arg)
{
    unsigned index;
    if (strlen(name) != 2 || name[0] < '0' || name[0] > '1' ||
        name[1] < '0' || name[1] > '9') return 0;
    index = (name[0] - '0') * 10 + name[1] - '0';
    uint8_t record[DIAG_RECORD_SIZE];
    /* Diagnostic corruption must never fail application settings startup. */
    if (index >= CAPACITY || len != sizeof(record) ||
        read_cb(arg, record, sizeof(record)) != sizeof(record) || !valid(record)) return 0;
    uint32_t seq = sys_get_be32(record + 8);
    if ((seq - 1) % CAPACITY != index) return 0;
    k_spinlock_key_t key = k_spin_lock(&lock);
    memcpy(records[index], record, sizeof(record));
    if (seq > head) head = seq;
    k_spin_unlock(&lock, key);
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(diagnostics, "diag", NULL, load, NULL, NULL);

void diagnostics_record(enum diagnostic_event event, uint32_t detail)
{
    if (event < DIAG_BOOT || event > DIAG_SENSOR_POWER) return;
    struct event item = {event, detail, k_uptime_get_32() / 1000};
    /* At most one of each event per boot; no periodic telemetry flash writes. */
    k_spinlock_key_t key = k_spin_lock(&lock);
    uint32_t bit = 1U << event;
    bool accepted = !(seen & bit) && k_msgq_put(&events, &item, K_NO_WAIT) == 0;
    if (accepted) seen |= bit;
    k_spin_unlock(&lock, key);
    if (accepted) {
        if (event == DIAG_STARTUP_FAILED)
            (void)history_event_record((uint16_t)detail, RECORD_TYPE_SDK_ERROR);
        else if (event == DIAG_ROLLBACK_REQUESTED || event == DIAG_OTA_REJECTED)
            (void)history_event_record(0, RECORD_TYPE_DFU_FAIL);
        else if (event == DIAG_WATCHDOG)
            (void)history_event_record((uint16_t)detail, RECORD_TYPE_FAULT_RESET);
    }
}
void diagnostics_reboot_event(enum diagnostic_event event, uint32_t detail)
{
    retained.detail = detail;
    retained.event = event;
    retained.inverse = ~(detail ^ (uint32_t)event);
    retained.magic = 0x41535744;
}
void diagnostics_watchdog(uint32_t workers)
{
    diagnostics_reboot_event(DIAG_WATCHDOG, workers);
    diagnostics_record(DIAG_WATCHDOG, workers);
}
void diagnostics_init(uint32_t reset_cause)
{
    reset_reason = reset_cause;
    k_sem_give(&start);
}
static void journal_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    k_sem_take(&start, K_FOREVER);
    bool storage_ok = settings_subsys_init() == 0;
    if (storage_ok) storage_ok = settings_load_subtree("diag") == 0;
    k_spinlock_key_t key = k_spin_lock(&lock);
    ready = true;
    k_spin_unlock(&lock, key);
    diagnostics_record(DIAG_BOOT, reset_reason);
    if ((reset_reason & (RESET_WATCHDOG | RESET_SOFTWARE)) &&
        retained.magic == 0x41535744 &&
        (retained.detail ^ retained.event) == ~retained.inverse &&
        (retained.event == DIAG_WATCHDOG || retained.event == DIAG_ROLLBACK_REQUESTED)) {
        diagnostics_record(retained.event, retained.detail);
    }
    retained.magic = 0;
    struct event item;
    while (true) {
        k_msgq_get(&events, &item, K_FOREVER);
        uint8_t record[DIAG_RECORD_SIZE] = {1, item.code,
            AIRSPOT_FIRMWARE_VERSION_MAJOR, AIRSPOT_FIRMWARE_VERSION_MINOR};
        sys_put_be16(AIRSPOT_FIRMWARE_VERSION_PATCH, record + 4);
        key = k_spin_lock(&lock);
        /* Saturation avoids ambiguous sequence wrap; impossible in normal life. */
        if (head == UINT32_MAX) { k_spin_unlock(&lock, key); continue; }
        uint32_t seq = ++head;
        unsigned index = (seq - 1) % CAPACITY;
        sys_put_be32(seq, record + 8);
        sys_put_be32(item.uptime, record + 12);
        sys_put_be32(item.detail, record + 16);
        sys_put_be32(reset_reason, record + 20);
        sys_put_be32(crc32_ieee(record, 24), record + 24);
        memcpy(records[index], record, sizeof(record));
        k_spin_unlock(&lock, key);
        if (storage_ok) {
            char name[8];
            snprintf(name, sizeof(name), "diag/%02u", index);
            /* One atomic NVS value per event. Stop retries after storage failure. */
            if (settings_save_one(name, record, sizeof(record))) storage_ok = false;
        }
    }
}
K_THREAD_DEFINE(diagnostic_thread, 2048, journal_thread, NULL, NULL, NULL, 10, 0, 0);

size_t diagnostics_read(uint8_t index, uint32_t expected_head, uint8_t *out)
{
    k_spinlock_key_t key = k_spin_lock(&lock);
    out[0] = 1; out[1] = 0; out[2] = index;
    out[3] = head < CAPACITY ? head : CAPACITY;
    sys_put_be32(head, out + 4);
    size_t size = 8;
    if (!ready) out[1] = 2;
    else if (expected_head && expected_head != head) out[1] = 3;
    else if (index >= out[3]) out[1] = 1;
    else {
        uint8_t *record = records[(head - index - 1) % CAPACITY];
        if (!valid(record) || sys_get_be32(record + 8) != head - index) out[1] = 4;
        else { memcpy(out + 8, record, DIAG_RECORD_SIZE); size += DIAG_RECORD_SIZE; }
    }
    k_spin_unlock(&lock, key);
    return size;
}
