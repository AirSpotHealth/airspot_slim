"""Exercise the production async conditioning transaction, failures and leases."""
import unittest
from native_support import ROOT, run_c
class DeferredConditioningTests(unittest.TestCase):
 def test_transaction_and_retries(self):
  s=(ROOT/'application/src/services/sensor_manager.c').read_text()
  a=s.index('static void condition_if_due(void)');b=s.index('\n#endif',a)
  run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define K_FOREVER 0
#define LED_COLOR_WHITE 1
#define HEALTH_SENSOR 0
#define SENSOR_FAULT_STARTING 1
#define SENSOR_FAULT_CO2 2
#define LOG_ERR(...)
#define LOG_INF(...)
static int64_t now=1;static int64_t k_uptime_get(void){return now;}
static int meas_mutex,mode_change_mutex,leases,ends,commands,starts,sleeps,stops,complete,fault;
static bool recovery_enabled=true,initialized=true,use_single_shot_mode,stcc4_conditioned,pending;
static bool running=true, conditioning_requested;
static int do_measurement_locked(void){return 0;}
static bool busy;static int command_error,restore_error;static int64_t next_report_ms;
static void k_mutex_lock(int*m,int t){assert(!*m);*m=1;}
static void k_mutex_unlock(int*m){assert(*m);*m=0;}
static int software_asc_begin_manual(void){leases++;return busy?-1:0;}
static void software_asc_end_manual(void){ends++;}
static bool conditioning_due(bool single){return pending;}
static void conditioning_completed(void){complete++;pending=false;}
static void set_sensor_fault(int f){fault=f;}
static void settling_observe(bool b){}
static void health_progress(int w,int ms){assert(ms>=23000);}
static int stcc4_exit_sleep_mode(void){return 0;}
static int stcc4_stop(void){stops++;return 0;}
static void conditioning_keepalive(void){}
static int stcc4_perform_conditioning(void(*f)(void)){assert(meas_mutex&&mode_change_mutex);commands++;now+=22000;return command_error;}
static int stcc4_start_continuous(void){starts++;return restore_error;}
static int stcc4_sleep_after_sample(void){sleeps++;return restore_error;}
static void led_indicator_start_flash_pulse(int a,int b,int c){}
static void led_indicator_stop(void){}
'''+s[a:b]+r'''
int main(void){
 condition_if_due();assert(leases==0&&commands==0); // no lease/reset in normal sampling
 pending=true;busy=true;condition_if_due();assert(commands==0&&ends==0);
 busy=false;condition_if_due();assert(commands==1&&complete==1&&starts==1&&!pending&&stcc4_conditioned);
 condition_if_due();assert(commands==1); // duplicate time sync cannot repeat
 pending=true;use_single_shot_mode=true;command_error=-5;
 condition_if_due();assert(commands==2&&pending&&complete==1&&sleeps==1&&fault==2);
 condition_if_due();assert(commands==2); // backoff
 now+=30000;command_error=0;condition_if_due();assert(commands==3&&!pending&&sleeps==2);
 pending=true;restore_error=-5;condition_if_due();assert(complete==3&&!pending&&fault==2);
 pending=true;initialized=false;now+=30000;condition_if_due();assert(commands==4);
 initialized=true;recovery_enabled=false;condition_if_due();assert(commands==4);
 assert(meas_mutex==0&&mode_change_mutex==0);
}
''')
