/* Shared soft-off request for the protocol and main-thread power state machine. */
#include <zephyr/sys/atomic.h>
#include <services/power_manager.h>

static atomic_t off_requested;

int power_manager_init(void)
{
  atomic_clear(&off_requested);
  return 0;
}

void power_manager_enter_off_state(void)
{
  atomic_set(&off_requested, 1);
}

bool power_manager_take_off_request(void)
{
  return atomic_cas(&off_requested, 1, 0);
}
