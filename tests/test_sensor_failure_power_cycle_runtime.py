"""Execute real shutdown/wake/startup blocks with persistent sensor faults.

Host simulation: GPIO, scheduler, BLE and System OFF are boundaries, not hardware.
"""
import unittest
from native_support import ROOT, run_c


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


class SensorFailurePowerCycleTests(unittest.TestCase):
    def test_repeated_off_on_with_failed_sensor_and_charger_gate(self):
        main = (ROOT / 'application/src/main.c').read_text()
        sensors = (ROOT / 'application/src/services/sensor_manager.c').read_text()
        start = main.index('  ret = sensor_manager_init();')
        end = main.index('  /* No persistent LED at boot', start)
        startup = main[start:end]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <drivers/button_hold.h>
#define LED_COLOR_AMBER 1
#include <setjmp.h>
#define LOG_INF(...)
#define LOG_ERR(...)
#define LOG_WRN(...)
#define K_FOREVER 0
#define K_SECONDS(x) ((x)*1000)
#define HEALTH_SENSOR 0
#define LED_COLOR_WHITE 0
#define SOFT_OFF_FADE_MS 1000
#define POWER_ON_FADE_MS 3000
#define POWER_ON_FADE_DELAY_MS 2000
#define POWER_ON_HOLD_MS 5000
#define CONFIG_AIRSPOT_POWER_OFF_ENABLED 1
#define IS_ENABLED(x) (x)
#define SYS_REBOOT_COLD 0
struct k_work_sync {int unused;};
static int meas_mutex, sensor_work, recovery_work, time_timer;
static bool recovery_enabled;
static bool running, soft_off_active, soft_on_requested, soft_on_holding;
static uint32_t soft_on_press_uptime, now;
static bool held, charging, connected, suppressed, advertising, calibrated;
static uint32_t release_at=6000;
static int sleep_calls, init_calls, off_count, cancels, ready_rc;
static jmp_buf off;
static void k_mutex_lock(int *m,int t) {}
static void k_mutex_unlock(int *m) {}
static void settling_observe(bool eligible) {}
static void health_progress(int w,int t) {}
static void k_work_cancel_delayable_sync(int *w,struct k_work_sync *s) {cancels++;}
static int stcc4_enter_sleep_mode(void) {sleep_calls++;return -EIO;}
static void watchdog_feed(void) {}
static void k_msleep(int ms) {now+=ms;}
static uint32_t k_uptime_get_32(void) {return now;}
static bool qi_charger_is_charging(void) {return charging;}
static int qi_charger_wait_ready(int timeout) {now+=310;return ready_rc;}
static int button_driver_read_now(bool *p) {*p=held && now<release_at;return 0;}
static bool led_indicator_ramp(int c,int lo,int hi,uint32_t ms,bool (*check)(void)) {
 now+=ms;return !check || check();
}
static void led_driver_all_off(void) {}
static void led_indicator_stop(void) {}
static void led_indicator_flash(int c,int period,int ms) {}
static void led_indicator_show_color(int c,int ms) {}
static void history_events_flush(void) {}
static void k_timer_stop(int *t) {}
static void ble_service_set_advertising_suppressed(bool b) {suppressed=b;advertising=false;}
static bool ble_service_is_connected(void) {return connected;}
static void ble_service_disconnect(void) {connected=false;}
static void ota_service_poll(void) {}
static void sys_reboot(int mode) {assert(!"unexpected fallback reboot");}
static void enter_system_off(bool started) {off_count++;longjmp(off,1);}
static bool startup_check(int stage,int rc) {return rc==0;}
static int sensor_manager_init(void) {init_calls++;return -EIO;}
static void sensor_data_callback(void) {}
static void sensor_manager_register_callback(void (*cb)(void)) {}
static void qi_charger_callback(bool b) {}
static void qi_charger_register_callback(void (*cb)(bool)) {}
static int data_log_init(void) {return 0;}
static int ble_mode_init(void) {advertising=!suppressed;return 0;}
static void ble_service_start_advertising(void) {advertising=!suppressed;}
''' + function(sensors, 'void sensor_manager_stop(void)') + '\n' +
            function(main, 'static void validate_system_off_wake(uint32_t hold_start)') + '\n' +
            function(main, 'static void enter_soft_off(void)') + r'''
static void finish_boot(void) {
 int ret;bool startup_healthy=true;bool sensor_startup_healthy=true;
''' + startup + r'''
 assert(startup_healthy && !sensor_startup_healthy && advertising);
}
int main(void) {
 /* Each boot still encounters a broken sensor; two off/on cycles remain usable. */
 charging=true;
 for(int cycle=0;cycle<2;cycle++) {
  running=true;connected=true;
  if(setjmp(off)==0) enter_soft_off();
  assert(!running && !connected && suppressed);
  assert(sleep_calls==cycle+1 && cancels==2*(cycle+1));
  /* Reset MCU globals at the simulated hardware wake boundary. */
  suppressed=false;held=true;now=0;ready_rc=0;
  validate_system_off_wake(0);
  assert(!soft_off_active && now>=6000);
  finish_boot();
 }
 assert(init_calls==2 && off_count==2);
 /* A working button cannot override a false charger indication, however long held. */
 charging=false;held=true;now=0;
 if(setjmp(off)==0) {validate_system_off_wake(0);assert(false);}
 assert(off_count==3 && now>=6000 && soft_off_active);
 /* An unavailable initial charger read has the same outcome. */
 charging=true;ready_rc=-EIO;now=0;
 if(setjmp(off)==0) {validate_system_off_wake(0);assert(false);}
 assert(off_count==4 && soft_off_active);
}
''')
