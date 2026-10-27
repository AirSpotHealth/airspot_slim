"""Exercise real colour selection with GPIO's nonzero-means-on behaviour."""
import unittest
from native_support import ROOT, run_c

class AmberLedTests(unittest.TestCase):
    def test_amber_duty_thresholds_and_cancellation(self):
        source = (ROOT / 'application/src/services/led_indicator.c').read_text()
        start = source.index('static struct k_spinlock color_lock;')
        end = source.index('/* ========================================', start)
        helpers = source[start:end]
        start = source.index('co2_level_t led_indicator_get_co2_level(')
        end = source.index('/* ========================================', start)
        run_c(r'''
#include <assert.h>
#include <stddef.h>
#include <services/led_indicator.h>
#define ARG_UNUSED(x) (void)(x)
#define K_MSEC(x) (x)
struct k_spinlock { int unused; };
typedef int k_spinlock_key_t;
static int k_spin_lock(struct k_spinlock *l) {return 0;}
static void k_spin_unlock(struct k_spinlock *l,int key) {}
struct k_timer {int running;};
#define K_TIMER_DEFINE(name, tick, stop) static struct k_timer name
static void k_timer_start(struct k_timer *t,int first,int period) {
 assert(first==1 && period==1);t->running=1;
}
static void k_timer_stop(struct k_timer *t) {t->running=0;}
static bool pins[3];
static int led_driver_set_brightness(int channel,int brightness) {
 pins[channel]=brightness>0;return 0;
}
static uint16_t yellow=800,red=1000;
static uint16_t user_settings_get_co2_yellow_start(void) {return yellow;}
static uint16_t user_settings_get_co2_red_start(void) {return red;}
''' + helpers + source[start:end] + r'''
int main(void) {
 assert(led_indicator_get_co2_color(799)==LED_COLOR_GREEN);
 assert(led_indicator_get_co2_color(800)==LED_COLOR_AMBER);
 assert(led_indicator_get_co2_color(999)==LED_COLOR_AMBER);
 assert(led_indicator_get_co2_color(1000)==LED_COLOR_RED);
 yellow=900;red=1200;
 assert(led_indicator_get_co2_color(899)==LED_COLOR_GREEN);
 assert(led_indicator_get_co2_color(900)==LED_COLOR_AMBER);
 assert(led_indicator_get_co2_color(1200)==LED_COLOR_RED);
 set_color(led_indicator_get_co2_color(1000),100);
 int green_ticks=0;
 for(int i=0;i<20;i++) {
   assert(pins[LED_RED] && !pins[LED_BLUE]);
   green_ticks+=pins[LED_GREEN];
   amber_pwm_tick(NULL);
   /* Repeated pattern updates must not restart the PWM phase. */
   set_color(LED_COLOR_AMBER,100);
 }
 assert(green_ticks==5 && amber_pwm_timer.running);
 set_color(LED_COLOR_AMBER,0); /* flash gap */
 amber_pwm_tick(NULL); /* late interrupt must not relight green */
 assert(!pins[0] && !pins[1] && !pins[2] && !amber_pwm_timer.running);
 set_color(LED_COLOR_AMBER,100);
 set_color(LED_COLOR_RED,100);
 amber_pwm_tick(NULL);
 assert(pins[LED_RED] && !pins[LED_GREEN] && !amber_pwm_timer.running);
 set_color(LED_COLOR_AMBER,100);
 set_color(LED_COLOR_OFF,0);
 amber_pwm_tick(NULL);
 assert(!pins[0] && !pins[1] && !pins[2] && !amber_pwm_timer.running);
 set_color(LED_COLOR_GREEN,100);
 assert(!pins[LED_RED] && pins[LED_GREEN] && !pins[LED_BLUE]);
}
''')
