"""Actual main callback: emergency recovery bypasses calibration and Qi gates."""
import unittest
from native_support import ROOT, run_c

class EmergencyButtonDispatchTests(unittest.TestCase):
    def test_recovery_and_normal_release_routing(self):
        s=(ROOT/'application/src/main.c').read_text()
        a=s.index('static void button_event_handler(button_event_t event)')
        body=s[a:s.index('\n}',a)+2]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#define LOG_INF(...)
#define LOG_WRN(...)
#define LOG_DBG(...)
#define K_MSEC(x) (x)
#define LED_COLOR_AMBER 1
#define LED_COLOR_WHITE 2
#define RAPID_PRESS_RESET_COUNT 5
#define TAP_DECISION_WINDOW_MS 500
#define SYS_REBOOT_COLD 0
typedef enum {BUTTON_EVENT_PRESSED,BUTTON_EVENT_RELEASED,BUTTON_EVENT_LONG_PRESS,
 BUTTON_EVENT_RECOVERY_WARNING,BUTTON_EVENT_RECOVERY_READY,
 BUTTON_EVENT_RECOVERY_CANCELLED,BUTTON_EVENT_RECOVERY_CONFIRMED} button_event_t;
static bool button_off_armed, soft_off_active, soft_on_requested, soft_off_requested;
static bool calibrating, charging;
static int tap_count,tap_decision_work,reboots,flashes;
static void sys_reboot(int mode) {reboots++;}
static int k_work_cancel_delayable(int *w) {return 0;}
static int k_work_reschedule(int *w,int ms) {return 0;}
static bool calibration_service_is_active(void) {return calibrating;}
static bool qi_charger_is_charging(void) {return charging;}
static void led_indicator_flash(int c,int p,int ms) {flashes++;}
static void led_indicator_stop(void) {}
static void led_indicator_show_color(int c,int ms) {}
static void k_msleep(int ms) {}
''' + body + r'''
int main(void) {
 charging=true;
 button_event_handler(BUTTON_EVENT_PRESSED);
 button_event_handler(BUTTON_EVENT_LONG_PRESS);
 assert(button_off_armed && !soft_off_requested);
 button_event_handler(BUTTON_EVENT_RELEASED);assert(soft_off_requested);
 soft_off_requested=false;
 button_event_handler(BUTTON_EVENT_PRESSED);
 button_event_handler(BUTTON_EVENT_LONG_PRESS);
 button_event_handler(BUTTON_EVENT_RECOVERY_WARNING);
 assert(!button_off_armed);
 button_event_handler(BUTTON_EVENT_RECOVERY_CANCELLED);
 button_event_handler(BUTTON_EVENT_RELEASED);
 assert(!soft_off_requested && !reboots && !tap_count);
 calibrating=true;charging=false;
 button_event_handler(BUTTON_EVENT_PRESSED);
 button_event_handler(BUTTON_EVENT_LONG_PRESS);
 button_event_handler(BUTTON_EVENT_RECOVERY_WARNING);
 button_event_handler(BUTTON_EVENT_RECOVERY_READY);
 button_event_handler(BUTTON_EVENT_RECOVERY_CONFIRMED);
 assert(reboots==1 && !soft_off_requested && flashes==3);
 calibrating=false;charging=true;soft_off_active=true;
 button_event_handler(BUTTON_EVENT_PRESSED);
 button_event_handler(BUTTON_EVENT_LONG_PRESS);
 assert(!soft_on_requested);
 button_event_handler(BUTTON_EVENT_RELEASED);assert(soft_on_requested);
}
''')
