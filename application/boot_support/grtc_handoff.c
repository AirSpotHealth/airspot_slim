/* NCS 3.2.1: preserve GRTC and its LF clock across chainload. MCUboot's
 * nrf_cleanup_peripheral() already disables GRTC interrupts/compare channels
 * without resetting the counter. Calling sys_clock_disable() first instead
 * stops the counter and releases LFCLK, which stalled this board's subsequent
 * application GRTC startup. Let the Nordic cleanup perform the handoff.
 * Scope: MCUboot only; the application's timer/low-power behavior is unchanged.
 */
#include <zephyr/sys/util.h>
BUILD_ASSERT(IS_ENABLED(CONFIG_NRF_GRTC_TIMER));
BUILD_ASSERT(IS_ENABLED(CONFIG_MCUBOOT_NRF_CLEANUP_PERIPHERAL));
void __wrap_sys_clock_disable(void)
{
    /* nrf_cleanup_peripheral() runs immediately after this hook. */
}
