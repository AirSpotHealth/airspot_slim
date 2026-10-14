#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <services/history_events.h>

/* SPI/NVS and kernel locks are unsafe here. Leave minimal evidence for boot. */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
  ARG_UNUSED(esf);
  history_retain_fault((uint16_t)reason);
  sys_reboot(SYS_REBOOT_COLD);
  CODE_UNREACHABLE;
}
