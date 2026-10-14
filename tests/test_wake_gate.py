"""Run actual wake qualification with charger faults and physical release timing."""
import unittest
from native_support import ROOT, run_c

class WakeGateTests(unittest.TestCase):
    def test_normal_emergency_cancel_and_charger_failure(self):
        source = (ROOT/'application/src/main.c').read_text()
        start = source.index('static void validate_system_off_wake(uint32_t hold_start)')
        function = source[start:source.index('\n}', start)+2]
        run_c(r'''
#include <assert.h>
#include <stddef.h>
#include <setjmp.h>
#include <drivers/button_hold.h>
#define LOG_INF(...)
#define LOG_WRN(...)
#define K_SECONDS(s) ((s)*1000)
#define POWER_ON_HOLD_MS BUTTON_NORMAL_HOLD_MS
#define LED_COLOR_WHITE 0
#define LED_COLOR_AMBER 1
static bool soft_off_active, charging;
static int ready_rc, gpio_rc, slept, amber, white, normal_cue;
static uint32_t now, release_at;
static jmp_buf off;
static void watchdog_feed(void) {}
static int qi_charger_wait_ready(int timeout) {now+=310;return ready_rc;}
static bool qi_charger_is_charging(void) {return charging;}
static uint32_t k_uptime_get_32(void) {return now;}
static void k_msleep(int ms) {now+=ms;assert(now<20000);}
static int button_driver_read_now(bool *p) {*p=now<release_at;return gpio_rc;}
static void led_indicator_stop(void) {}
static void led_indicator_show_color(int c,int ms) {normal_cue++;}
static void led_indicator_flash(int c,int period,int ms) {if(c==LED_COLOR_AMBER)amber++;else white++;}
static bool led_indicator_ramp(int c,int lo,int hi,int ms,void *check) {return true;}
static void enter_system_off(bool started) {assert(!started);slept++;longjmp(off,1);}
''' + function + r'''
static void run(uint32_t release, bool charger,int rc) {
 now=0;release_at=release;charging=charger;ready_rc=rc;
 slept=amber=white=normal_cue=0;soft_off_active=false;
 if(setjmp(off)==0) validate_system_off_wake(0);
}
int main(void) {
 run(6000,true,0);assert(!slept && !soft_off_active && normal_cue==1);
 run(4000,true,0);assert(slept);
 run(6000,false,0);assert(slept);
 run(16000,false,0);assert(!slept && amber==1 && white==1);
 run(16000,true,0);assert(!slept && amber==1 && white==1);
 run(16000,true,-1);assert(!slept && !normal_cue);
 run(14000,false,0);assert(slept && amber==1 && !white);
 run(18000,true,0);assert(slept);
 run(60000,true,0);assert(slept && now>=18000 && now<18100);
 gpio_rc=-1;run(16000,true,0);assert(slept);
 /* Exact release policy is independent of poll granularity. */
 assert(button_hold_release(14999,false)==BUTTON_HOLD_CANCEL);
 assert(button_hold_release(15000,false)==BUTTON_HOLD_RECOVER);
 assert(button_hold_release(17999,false)==BUTTON_HOLD_RECOVER);
 assert(button_hold_release(18000,false)==BUTTON_HOLD_CANCEL);
 assert(button_hold_release(9999,true)==BUTTON_HOLD_NORMAL);
 assert(button_hold_release(10000,true)==BUTTON_HOLD_CANCEL);
}
''')
