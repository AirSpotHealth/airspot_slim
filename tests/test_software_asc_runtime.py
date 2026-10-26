"""Run the production ASC service with deterministic time and failing storage."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from history_support import declarations

ROOT = Path(__file__).resolve().parents[1]

class SoftwareAscTests(unittest.TestCase):
    def _run_calibration(self, native=False):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            headers = {
                'zephyr/kernel.h': '''#pragma once
#include <stdint.h>
#define K_MUTEX_DEFINE(x) int x
#define K_FOREVER 0
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
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
static uint8_t enabled, power = 3;
#if defined(CONFIG_AIRSPOT_STCC4_INTERNAL_ASC_EXPERIMENT)
static uint8_t native_mode=2;
#else
static uint8_t native_mode;
#endif
static uint8_t user_settings_get_native_asc_mode(void) {return native_mode;}
static int16_t correction;
static uint16_t target = 426;
static uint32_t clock_now;
static bool time_set;
static int storage_error;
static uint8_t user_settings_get_auto_calibrate(void) { return enabled; }
static int user_settings_set_auto_calibrate(uint8_t v) { if (!storage_error) enabled=v; return storage_error; }
static uint8_t user_settings_get_power_mode(void) { return power; }
static int user_settings_set_power_mode(uint8_t v) {if(!storage_error)power=v;return storage_error;}
static uint16_t user_settings_get_calib_target(void) { return target; }
static int user_settings_set_calib_target(uint16_t v) { if (!storage_error) target=v; return storage_error; }
static int16_t user_settings_get_co2_correction(void) { return correction; }
static int user_settings_set_co2_correction(int16_t v) { if (!storage_error) correction=v; return storage_error; }
static int user_settings_commit_manual_correction(int16_t v) {if(native_mode==2)return -EBUSY;if(!storage_error){correction=v;native_mode=0;}return storage_error;}
static bool user_settings_is_time_set(void) { return time_set; }
static uint32_t user_settings_get_time_now(void) { return clock_now; }
''',
                'services/data_log.h': '#pragma once\n' + declarations(),
                'services/history_events.h': '''#pragma once
#include <stddef.h>
#include <services/data_log.h>
static int logs;
static struct history_event captured[4096];
static int history_event_record(uint16_t v, record_type_t k) {captured[logs++]=(struct history_event){v,k};return 0;}
static int history_event_batch(const struct history_event *e,size_t n) {for(size_t i=0;i<n;i++)history_event_record(e[i].value,e[i].type);return 0;}
''',
            }
            for name, text in headers.items():
                dest=p/name; dest.parent.mkdir(parents=True, exist_ok=True); dest.write_text(text)
            (p/'test.c').write_text('''#include <assert.h>
#include "''' + str(ROOT/'application/src/services/software_asc.c') + '''"
#include "services/calibration_service.c"
static int finished, failed, mode;
int sensor_manager_change_power_mode(uint8_t m) {if(storage_error)return storage_error;mode=m;power=m;return 0;}
static void done(int16_t d) {finished++;}
static void error(int e) {failed++;}
static void feed(int n, uint16_t value) { while(n--) software_asc_process_measurement(value); }
static void day(int d) { clock_now=d*86400+86340; software_asc_tick(); }
static void fresh(void) {
  enabled=time_set=storage_error=correction=0; target=426; power=3;
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
  assert(captured[logs-3].type==RECORD_TYPE_CALIBRATION_CORRECTION_OLD && captured[logs-3].value==0);
  assert(captured[logs-2].type==RECORD_TYPE_CALIBRATION_CORRECTION && captured[logs-2].value==120);
  assert(captured[logs-1].type==RECORD_TYPE_ASC_ADJUSTMENT && captured[logs-1].value==120);
  feed(3, 300); assert(correction==120); /* old window never applies twice */
  fresh(); enabled=1; feed(3, 300); assert(correction==120);
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
  fresh(); power=0; logs=0;
  calibration_callbacks_t cb={.on_done=done,.on_error=error};
  calibration_service_init(&cb);
  assert(calibration_service_start(426)==0 && power==2 && mode==2);
  assert(logs==3 && captured[0].type==RECORD_TYPE_MANUAL_CALIB_START && captured[0].value==1);
  assert(captured[1].type==RECORD_TYPE_CALIBRATION_CORRECTION_OLD && captured[1].value==0);
  assert(captured[2].type==RECORD_TYPE_CALIB_TARGET && captured[2].value==426);
  assert(calibration_service_start(426)==-EBUSY && logs==3);
  calibration_service_process_measurement(800);
  calibration_service_process_measurement(1000);
  while(calib_active) calibration_tick_work_handler(NULL);
  assert(correction==-474 && finished==1 && failed==0 && power==0 && mode==0);
  assert(!software_asc_manual_active());
  assert(logs==4 && captured[3].type==RECORD_TYPE_CALIBRATION_CORRECTION);
  assert((int16_t)captured[3].value==-474);
  assert(calibration_service_start(426)==0);
  while(calib_active) calibration_tick_work_handler(NULL);
  assert(correction==-474 && finished==1 && failed==1);
  assert(calibration_service_start(426)==0);
  calibration_service_process_measurement(800);storage_error=-EIO;
  while(calib_active) calibration_tick_work_handler(NULL);
  assert(correction==-474 && finished==1 && failed==2);
  storage_error=0; power=0; storage_error=-EIO; int logs_before=logs;
  assert(calibration_service_start(426)==-EIO && !software_asc_manual_active() && logs==logs_before);
  fresh(); uptime=0; enabled=1; software_asc_init();
  finished=failed=0; calibration_service_init(&cb);
  assert(calibration_service_start(426)==0);
  calibration_service_process_measurement(800);
  while(calib_active) calibration_tick_work_handler(NULL);
  assert(correction==-374 && finished==1 && failed==0);
  feed(400,300); assert(correction==-374 && asc.records==0);
  /* Engine's exported corrections came from these raw calibration means.
   * Reject them without saving another offset or reporting success. */
  fresh(); correction=-31102;
  uint16_t abnormal_means[]={31602,32036,32767,5001};
  for(unsigned i=0;i<sizeof(abnormal_means)/sizeof(abnormal_means[0]);i++) {
    delta=123;
    assert(software_asc_begin_manual()==0);
    assert(software_asc_finish_manual(426,abnormal_means[i],true,&delta)==-ERANGE);
    assert(correction==-31102 && delta==123 && !software_asc_manual_active());
  }
  correction=0; assert(software_asc_begin_manual()==0);
  assert(software_asc_finish_manual(426,5000,true,&delta)==0 && correction==-4574);
  /* A bad sample must not be hidden by averaging with good samples. */
  fresh(); correction=123; finished=failed=logs=0;
  calibration_service_init(&cb);
  assert(calibration_service_start(426)==0);
  calibration_service_process_measurement(800);
  calibration_service_process_measurement(5001);
  calibration_service_process_measurement(800);
  while(calib_active) calibration_tick_work_handler(NULL);
  assert(correction==123 && finished==0 && failed==1 && logs==3);
  assert(!software_asc_manual_active());
  /* A rejected session must not poison the next stable calibration. */
  assert(calibration_service_start(426)==0);
  calibration_service_process_measurement(800);
  while(calib_active) calibration_tick_work_handler(NULL);
  assert(correction==-374 && finished==1 && failed==1 && logs==7);
  return 0;
}
''')
            if native:
                text = (p/'test.c').read_text()
                text = text[:text.index('int main(void)')] + r'''
int main(void) {
  software_asc_init();enabled=1;correction=-1234;uptime=200000;
  for(int i=0;i<20;i++)software_asc_process_measurement(100);
  uptime+=8LL*86400000;software_asc_tick();
  assert(correction==-1234 && logs==0);
  assert(software_asc_set_enabled(1)==-EBUSY && enabled==1);
  assert(software_asc_set_enabled(0)==-EBUSY && enabled==1);
  assert(software_asc_set_target(400)==-EBUSY && target==426);
  assert(software_asc_set_target(426)==-EBUSY);
  assert(calibration_service_start(426)==-EBUSY && !calib_active);
  assert(correction==-1234 && logs==0);
  calibration_callbacks_t cb={.on_done=done,.on_error=error};calibration_service_init(&cb);
  native_mode=1;enabled=0;correction=0;
  assert(calibration_service_start(426)==0);
  calibration_service_process_measurement(800);
  while(calib_active) calibration_tick_work_handler(NULL);
  assert(native_mode==0 && correction==-374 && finished==1);
  return 0;
}
'''
                (p/'test.c').write_text(text)
            subprocess.run(['cc',*(['-DCONFIG_AIRSPOT_STCC4_INTERNAL_ASC_EXPERIMENT=1'] if native else []),'-std=c11','-fsanitize=address,undefined','-I',str(p),
                            '-I',str(ROOT/'application/include'),'-I',str(ROOT/'application/src'),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

    def test_calibration_policy_and_failures(self):
        self._run_calibration()

    def test_native_asc_blocks_software_adjustments_and_manual_calibration(self):
        self._run_calibration(native=True)
