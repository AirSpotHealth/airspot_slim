/* Deadlines measure worker progress, not sensor validity or phone traffic. */
#include <zephyr/kernel.h>
#include <services/health_service.h>
static struct k_spinlock lock;
static int64_t deadlines[HEALTH_COUNT];
static uint32_t failed;

void health_progress(enum health_worker worker, uint32_t allowance_ms)
{
    if ((unsigned)worker >= HEALTH_COUNT) return;
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (deadlines[worker] && k_uptime_get() >= deadlines[worker]) failed |= 1U << worker;
    deadlines[worker] = allowance_ms ? k_uptime_get() + allowance_ms : 0;
    k_spin_unlock(&lock, key);
}
uint32_t health_failed(void)
{
    k_spinlock_key_t key = k_spin_lock(&lock);
    for (unsigned i = 0; i < HEALTH_COUNT; ++i) {
        if (deadlines[i] && k_uptime_get() >= deadlines[i]) failed |= 1U << i;
    }
    uint32_t result = failed; /* Latched: a late worker cannot mask the fault. */
    k_spin_unlock(&lock, key);
    return result;
}
static void workqueue_progress(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(beacon, workqueue_progress);
static void workqueue_progress(struct k_work *work)
{
    ARG_UNUSED(work);
    /* System queue includes conditioning/recovery operations of ~22 seconds. */
    health_progress(HEALTH_WORKQUEUE, 35000);
    k_work_schedule(&beacon, K_SECONDS(1));
}
void health_start(void)
{
    health_progress(HEALTH_MAIN, 5000);
    health_progress(HEALTH_WORKQUEUE, 35000);
    k_work_schedule(&beacon, K_NO_WAIT);
}
