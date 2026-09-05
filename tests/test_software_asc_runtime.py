"""Run the production ASC service with deterministic time and failing storage."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class SoftwareAscTests(unittest.TestCase):
    def test_calibration_policy_and_failures(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            headers = {
                'zephyr/kernel.h': '''#pragma once
#include <stdint.h>
#define K_MUTEX_DEFINE(x) int x
#define K_FOREVER 0
#define K_SECONDS(x) (x)
#define ARG_UNUSED(x) (void)(x)
struct k_work {int unused;};
struct k_work_delayable {int unused;};
#define K_WORK_DELAYABLE_DEFINE(x,f) struct k_work_delayable x
static void k_work_schedule(struct k_work_delayable *w,int t) {}
#define CLAMP(x,l,h) ((x)<(l)?(l):((x)>(h)?(h):(x)))
static int64_t uptime;
static int64_t k_uptime_get(void) { return uptime; }
static uint32_t k_uptime_get_32(void) {return (uint32_t)uptime;}
#define k_mutex_lock(m,t) ((void)0)
#define k_mutex_unlock(m) ((void)0)
''',
                'zephyr/logging/log.h': '#define LOG_MODULE_REGISTER(...)\n#define LOG_ERR(...)\n#define LOG_INF(...)\n',
                'services/user_settings.h': '''#pragma once
#include <stdbool.h>
#include <stdint.h>
static uint8_t enabled, flight, power = 3;
static int16_t correction;
static uint16_t target = 426;
static uint32_t clock_now;
static bool time_set;
static int storage_error;
static uint8_t user_settings_get_auto_calibrate(void) { return enabled; }
static int user_settings_set_auto_calibrate(uint8_t v) { if (!storage_error) enabled=v; return storage_error; }
static uint8_t user_settings_get_flight_mode(void) { return flight; }
static uint8_t user_settings_get_power_mode(void) { return power; }
static int user_settings_set_power_mode(uint8_t v) {if(!storage_error)power=v;return storage_error;}
static uint16_t user_settings_get_calib_target(void) { return target; }
static int user_settings_set_calib_target(uint16_t v) { if (!storage_error) target=v; return storage_error; }
static int16_t user_settings_get_co2_correction(void) { return correction; }
static int user_settings_set_co2_correction(int16_t v) { if (!storage_error) correction=v; return storage_error; }
static bool user_settings_is_time_set(void) { return time_set; }
static uint32_t user_settings_get_time_now(void) { return clock_now; }
''',
                'services/data_log.h': '''#pragma once
typedef enum {RECORD_TYPE_ASC_ADJUSTMENT, RECORD_TYPE_CALIB_TARGET,
RECORD_TYPE_ASC_LOWEST, RECORD_TYPE_CALIBRATION_CORRECTION_OLD,
RECORD_TYPE_CALIBRATION_CORRECTION, RECORD_TYPE_SENSOR_AUTO_CALIBRATION,
RECORD_TYPE_CALIBRATION_TARGET_UPDATE, RECORD_TYPE_MANUAL_CALIB_START} record_type_t;
static int logs;
static int data_log_store_record(uint32_t t, uint16_t v, record_type_t k) { logs++; return 0; }
''',
            }
            for name, text in headers.items():
                dest=p/name; dest.parent.mkdir(parents=True, exist_ok=True); dest.write_text(text)
            (p/'test.c').write_text('''#include <assert.h>
#include "''' + str(ROOT/'application/src/services/software_asc.c') + '''"
#include "services/calibration_service.c"
static int finished, failed, mode;
void sensor_manager_set_power_mode(uint8_t m) {mode=m;}
static void done(int16_t d) {finished++;}
static void error(int e) {failed++;}
static void feed(int n, uint16_t value) { while(n--) software_asc_process_measurement(value); }
static void day(int d) { clock_now=d*86400+86340; software_asc_tick(); }
static void fresh(void) {
  enabled=flight=time_set=storage_error=correction=0; target=426; power=3;
  clock_now=0; uptime=0; software_asc_init(); uptime=180000;
}
int main(void) {
  fresh(); uptime=0; enabled=1; software_asc_init();
  software_asc_set_check_duration(1); feed(400,300);
  uptime=179999; software_asc_tick(); feed(3,300);
  assert(correction==0 && asc.records==0 && asc.count==0 && asc.days==0);
  software_asc_set_enabled(1); software_asc_set_target(500);
  feed(3,300); assert(correction==0); /* toggles/targets cannot bypass guard */
  uptime=180000; feed(2,300); assert(correction==0);
  feed(1,300); assert(correction==120 && asc.records==3);
  correction=0; software_asc_sensor_restarted();
  assert(asc.records==0 && asc.count==0 && asc.days==0);
  uptime+=179999;feed(400,300);software_asc_tick();
  assert(correction==0 && asc.records==0 && asc.days==0);
  uptime++;feed(3,300);assert(correction==120);

  fresh(); feed(400, 300); assert(correction==0);
  assert(software_asc_set_enabled(1)==0);
  feed(2, 300); assert(correction==0);
  feed(1, 300); assert(correction==120);
  feed(3, 300); assert(correction==120); /* old window never applies twice */
  fresh(); enabled=1; flight=1; feed(3, 300); assert(correction==0);
  flight=0; feed(1, 300); assert(correction==120);
  fresh(); enabled=1; storage_error=-EIO; feed(3, 300);
  assert(correction==0 && logs>0); storage_error=0; feed(1,300); assert(correction==120);
  fresh(); enabled=1; time_set=1;
  for(int d=1; d<=7; d++) { feed(400, 800); day(d); software_asc_tick(); }
  assert(correction==-374 && software_asc_get_day_count()==0);
  feed(400,800); day(7); assert(software_asc_get_day_count()==0);
  day(6); assert(software_asc_get_day_count()==0); /* backward sync */
  day(8); assert(software_asc_get_day_count()==1);
  fresh(); enabled=1; feed(400,800); day(1); assert(asc.days==0); /* no clock */
  time_set=1; day(1); assert(asc.days==1);
  day(2); assert(asc.days==1); /* no samples */
  feed(399,800); day(3); assert(asc.days==1);
  feed(1,800); day(4); assert(asc.days==2);
  day(100); assert(asc.days==2); /* no backfilling missing days */
  fresh(); enabled=1; software_asc_set_check_duration(1);
  for(int i=0; i<10; i++) { uptime+=1000; software_asc_tick(); }
  assert(correction==0 && asc.days==0); /* no minimum */
  feed(3,800); power=0; uptime+=1000; software_asc_tick(); assert(asc.days==0);
  power=3;
  for(int i=0; i<7; i++) { uptime+=1000; software_asc_tick(); }
  assert(correction==-374 && asc.days==0);
  fresh(); enabled=1; time_set=1;
  for(int d=1;d<7;d++) {feed(400,800);day(d);}
  storage_error=-EIO; feed(400,800); day(7); assert(correction==0 && asc.days==6);
  storage_error=0; day(8); assert(correction==-374 && asc.days==0);
  fresh(); enabled=1; feed(400,800); time_set=1; day(1);
  software_asc_init(); assert(enabled==1 && asc.days==0 && asc.records==0);
  assert(software_asc_set_target(500)==0 && target==500 && asc.count==0);
  storage_error=-EIO; assert(software_asc_set_enabled(0)==-EIO && enabled==1);
  assert(software_asc_set_target(600)==-EIO && target==500); storage_error=0;
  int16_t delta=0;
  assert(software_asc_begin_manual()==0); assert(software_asc_begin_manual()==-EBUSY);
  feed(500,300); day(2); assert(correction==0 && asc.days==0);
  assert(software_asc_set_target(600)==-EBUSY);
  assert(software_asc_finish_manual(426,800,true,&delta)==0);
  assert(correction==-374 && delta==-374 && asc.count==0 && !asc.manual);
  feed(3,800); assert(correction==-374); /* raw stays raw; offset only once */
  software_asc_begin_manual(); storage_error=-EIO;
  assert(software_asc_finish_manual(426,700,true,&delta)==-EIO && correction==-374);
  storage_error=0; software_asc_begin_manual();
  assert(software_asc_finish_manual(426,0,false,&delta)==-ENODATA && correction==-374);
  software_asc_begin_manual();
  assert(software_asc_finish_manual(65535,1,true,&delta)==-ERANGE && correction==-374);
  correction=INT16_MIN; software_asc_begin_manual();
  assert(software_asc_finish_manual(426,1,true,&delta)==-ERANGE && correction==INT16_MIN);
  fresh(); enabled=1; target=65535; correction=INT16_MAX; feed(3,1);
  software_asc_set_check_duration(1);
  for(int i=0;i<7;i++) {uptime+=1000;software_asc_tick();}
  assert(correction==INT16_MAX && asc.days==6);
  fresh(); power=0;
  calibration_callbacks_t cb={.on_done=done,.on_error=error};
  calibration_service_init(&cb);
  assert(calibration_service_start(426)==0 && power==2 && mode==2);
  assert(calibration_service_start(426)==-EBUSY);
  calibration_service_process_measurement(800);
  calibration_service_process_measurement(1000);
  while(calib_active) calibration_tick_work_handler(NULL);
  assert(correction==-474 && finished==1 && failed==0 && power==0 && mode==0);
  assert(!software_asc_manual_active());
  assert(calibration_service_start(426)==0);
  while(calib_active) calibration_tick_work_handler(NULL);
  assert(correction==-474 && finished==1 && failed==1);
  assert(calibration_service_start(426)==0);
  calibration_service_process_measurement(800);storage_error=-EIO;
  while(calib_active) calibration_tick_work_handler(NULL);
  assert(correction==-474 && finished==1 && failed==2);
  storage_error=0; power=0; storage_error=-EIO;
  assert(calibration_service_start(426)==-EIO && !software_asc_manual_active());
  return 0;
}
''')
            subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I',str(p),
                            '-I',str(ROOT/'application/include'),'-I',str(ROOT/'application/src'),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
