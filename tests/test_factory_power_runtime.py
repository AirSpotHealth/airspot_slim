import re
import unittest
from native_support import ROOT, run_c

class FactoryPowerTests(unittest.TestCase):
    def test_pad_button_and_failed_cut(self):
        source=(ROOT/'application/src/services/power_manager.c').read_text()
        source=re.sub(r'^#include .*$', '', source, flags=re.M)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
typedef int atomic_t;
static void atomic_clear(int *a){*a=0;}
static void atomic_set(int *a,int v){*a=v;}
static int atomic_get(int *a){return *a;}
static bool atomic_cas(int *a,int old,int v){if(*a!=old)return false;*a=v;return true;}
struct device {int unused;};static struct device dev;
#define DEVICE_DT_GET(x) (&dev)
#define DT_NODELABEL(x) 0
#define GPIO_OUTPUT_INACTIVE 0
static bool pad,pressed,ready=true;
static int read_error,configure_error,set_error,high,ms;
static int button_driver_read_now(bool *p){*p=pressed;return read_error;}
static bool qi_charger_is_charging(void){return pad;}
static bool device_is_ready(const struct device *d){return ready;}
static int gpio_pin_configure(const struct device *d,int pin,int flags){assert(pin==3);return configure_error;}
static int gpio_pin_set(const struct device *d,int pin,int value){assert(pin==3);high=value;return set_error;}
static void k_msleep(int n){ms+=n;}
''' + source + r'''
int main(void){
 assert(power_manager_init()==0);
 power_manager_enter_off_state();assert(power_manager_take_off_request());
 assert(!power_manager_factory_off_requested());
 pad=true;assert(power_manager_cut_battery()==-EBUSY&&!high&&!ms);
 pad=false;pressed=true;assert(power_manager_cut_battery()==-EBUSY&&!high&&!ms);
 pressed=false;read_error=-EIO;assert(power_manager_prepare_factory_off()==-EIO);
 read_error=0;configure_error=-EIO;assert(power_manager_prepare_factory_off()==-EIO);
 configure_error=0;assert(power_manager_prepare_factory_off()==0);
 power_manager_request_factory_off();assert(power_manager_factory_off_requested());
 /* If execution survives, report failure and lower SHPACT to permit charging. */
 assert(power_manager_cut_battery()==-ETIMEDOUT && ms==1000 && !high);
}
''')

    def test_calibration_erase_does_not_depend_on_pressure(self):
        source=(ROOT/'application/src/services/sensor_manager.c').read_text()
        fn=source[source.index('int sensor_manager_factory_erase_calibration(void)'):]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#define K_FOREVER 0
static int meas_mutex,step,fail,lease_error;
static int software_asc_begin_manual(void){return lease_error;}
static void software_asc_end_manual(void){}
static void sensor_manager_stop(void){assert(++step==1);}
static void k_mutex_lock(int *m,int t){}
static void k_mutex_unlock(int *m){}
static int action(void){++step;return step==fail?-EIO:0;}
static int stcc4_init(void){return action();}
static int stcc4_exit_sleep_mode(void){return action();}
static int stcc4_stop(void){return action();}
static bool user_settings_is_time_set(void){return true;}
static unsigned user_settings_get_time_now(void){return 123;}
static int settling_prepare_erase(unsigned t){assert(t==123);return action();}
static int stcc4_factory_reset(void){return action();}
static int settling_erased(void){return action();}
''' + fn + r'''
int main(void){
 assert(sensor_manager_factory_erase_calibration()==0 && step==7);
 for(fail=2;fail<=7;fail++){step=0;assert(sensor_manager_factory_erase_calibration()==-EIO && step==fail);}
 step=0;lease_error=-EBUSY;assert(sensor_manager_factory_erase_calibration()==-EBUSY && !step);
}
''')
