"""Execute the actual bounded power-rail experiment with GPIO/timer faults."""
from pathlib import Path
import re,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class SupplyDiagnosticTests(unittest.TestCase):
 def test_timeout_restore_and_gpio_failures(self):
  source=re.sub(r'^#include .*$', '', (ROOT/'application/src/drivers/supply_diagnostic.c').read_text(),flags=re.M)
  prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <drivers/supply_diagnostic.h>
#define DT_NODELABEL(x) 0
#define GPIO_DT_SPEC_GET(...) {0}
#define BUILD_ASSERT(...)
#define K_MUTEX_DEFINE(n) int n
#define K_FOREVER 0
#define K_MSEC(x) (x)
#define ARG_UNUSED(x) (void)(x)
#define GPIO_DISCONNECTED 0
#define GPIO_OUTPUT_ACTIVE 1
#define GPIO_INPUT 2
struct gpio_dt_spec {int unused;};
struct k_work {int unused;};
struct k_work_delayable {int delay;};
#define K_WORK_DELAYABLE_DEFINE(n,f) struct k_work_delayable n
static int64_t now;
static int configured,config_error,level=1,configure_calls;
static bool ready=true;
static int gpio_pin_configure_dt(const struct gpio_dt_spec *p,int flags) {configure_calls++;if(config_error)return config_error;configured=flags;return 0;}
static int gpio_pin_get_dt(const struct gpio_dt_spec *p) {return level;}
static bool gpio_is_ready_dt(const struct gpio_dt_spec *p) {return ready;}
static void k_busy_wait(int us) {assert(us>=55);}
static int64_t k_uptime_get(void) {return now;}
static void k_mutex_lock(int *m,int t) {assert((*m)++==0);}
static void k_mutex_unlock(int *m) {assert((*m)--==1);}
static int k_work_reschedule(struct k_work_delayable *w,int delay) {w->delay=delay;return 0;}
static int k_work_cancel_delayable(struct k_work_delayable *w) {w->delay=0;return 0;}
'''
  tests=r'''
int main(void) {
 supply_diagnostic_t s;
 assert(supply_diagnostic_control(2,&s)==0 && !s.pwm_active && configure_calls==0);
 assert(supply_diagnostic_control(3,&s)==-EINVAL && configure_calls==0);
 assert(supply_diagnostic_control(1,NULL)==-EINVAL);
 ready=false;assert(supply_diagnostic_control(1,&s)==-ENODEV && !s.pwm_active);ready=true;
 config_error=-EIO;assert(supply_diagnostic_control(1,&s)==-EIO && !s.pwm_active);config_error=0;
 assert(supply_diagnostic_control(1,&s)==0 && s.pwm_active && s.seconds_remaining==300);
 assert(configured==(GPIO_OUTPUT_ACTIVE|GPIO_INPUT));
 now=299999;expire(NULL);assert(pwm_active);
 assert(supply_diagnostic_control(2,&s)==0 && s.seconds_remaining==1);
 now=300000;expire(NULL);assert(!pwm_active && configured==GPIO_DISCONNECTED);
 assert(supply_diagnostic_control(1,&s)==0);int calls=configure_calls;
 now+=100000;assert(supply_diagnostic_control(1,&s)==0 && configure_calls==calls);
 now+=200000;expire(NULL);assert(pwm_active); /* old deadline must not end renewed lease */
 now+=100000;config_error=-EIO;expire(NULL);assert(pwm_active && expiry_work.delay==1000);
 assert(supply_diagnostic_control(2,&s)==-EIO && s.pwm_active && s.seconds_remaining==0);
 config_error=0;now+=1000;expire(NULL);assert(!pwm_active);
 assert(supply_diagnostic_control(1,&s)==0);
 assert(supply_diagnostic_control(0,&s)==0 && !s.pwm_active);
 level=0;assert(supply_diagnostic_control(1,&s)==-EIO && !s.pwm_active && configured==GPIO_DISCONNECTED);
 level=-EIO;assert(supply_diagnostic_control(1,&s)==-EIO && !s.pwm_active);
 assert(supply_mutex==0);
}
'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.c').write_text(prefix+source+tests)
   subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I',str(ROOT/'application/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
