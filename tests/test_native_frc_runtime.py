"""Run the native FRC service with a deterministic clock and failing sensor."""
from pathlib import Path
import subprocess, tempfile, unittest
ROOT = Path(__file__).resolve().parents[1]
class NativeFrcTests(unittest.TestCase):
 def _run(self, native=False):
  headers = {'zephyr/kernel.h': '''#pragma once
#include <stdint.h>
#include <stddef.h>
#define K_MUTEX_DEFINE(x) int x
#define K_FOREVER 0
#define K_SECONDS(x) (x)
#define ARG_UNUSED(x) (void)(x)
struct k_work {int unused;};
struct k_work_delayable {int unused;};
#define K_WORK_DELAYABLE_DEFINE(x,f) struct k_work_delayable x
static void k_work_schedule(struct k_work_delayable *w,int t) {}
static int64_t now;
static int64_t k_uptime_get(void) {return now;}
#define k_mutex_lock(m,t) ((void)0)
#define k_mutex_unlock(m) ((void)0)
'''}
  code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include "services/native_calibration_service.c"
static uint8_t native_mode=1, saved_mode=1, actual_mode=1;
static bool lease;
static int frc_calls, done_calls, error_calls, frc_error, mode_error, target_error;
static int16_t reported;
uint8_t user_settings_get_native_asc_mode(void) {return native_mode;}
uint8_t user_settings_get_power_mode(void) {return saved_mode;}
int software_asc_set_target(uint16_t t) {return lease?-EBUSY:target_error;}
int software_asc_begin_calibration(void) {if(lease)return -EBUSY;lease=true;return 0;}
void software_asc_end_manual(void) {lease=false;}
bool software_asc_manual_active(void) {return lease;}
int sensor_manager_set_power_mode(uint8_t m) {if(mode_error)return mode_error;actual_mode=m;return 0;}
int sensor_manager_perform_frc(uint16_t t,int16_t *v) {
 assert(lease && actual_mode==3 && t==426);frc_calls++;*v=-174;return frc_error;
}
static void done(int16_t v) {done_calls++;reported=v;}
static void error(int e) {assert(e);error_calls++;}
static void run(bool gap) {
 for(int s=1;s<=180;s++) {now+=1000;
  if(s%5==0 && !(gap && s>=50 && s<=70)) calibration_service_process_measurement(600);
  calibration_tick(NULL);
  if(s<180) assert(lease);
 }
}
int main(void) {
 calibration_callbacks_t cb={.on_done=done,.on_error=error};calibration_service_init(&cb);
 native_mode=2;assert(calibration_service_start(426)==-EBUSY && !lease);
 native_mode=1;assert(calibration_service_start(32001)==-EINVAL);
 target_error=-EIO;assert(calibration_service_start(426)==-EIO && !lease);target_error=0;
 mode_error=-EIO;assert(calibration_service_start(426)==-EIO && !lease);mode_error=0;
 assert(!calibration_service_start(426) && lease && actual_mode==3 && saved_mode==1);
 assert(calibration_service_start(426)==-EBUSY);
 run(false);assert(frc_calls==1 && done_calls==1 && !error_calls && reported==-174);
 assert(!lease && actual_mode==1 && saved_mode==1);
 assert(!calibration_service_start(426));run(true);
 assert(frc_calls==1 && done_calls==1 && error_calls==1 && !lease && actual_mode==1);
 assert(!calibration_service_start(426));frc_error=-EIO;run(false);
 assert(frc_calls==2 && done_calls==1 && error_calls==2 && !lease && actual_mode==1);
 frc_error=0;assert(!calibration_service_start(426));now+=180000;calibration_tick(NULL);
 assert(frc_calls==2 && error_calls==3 && !lease);
}
'''
  if native:
   code = '#define CONFIG_AIRSPOT_NATIVE_TEST_MODES 1\n#define CONFIG_AIRSPOT_CONTINUOUS_ALL_MODES 1\n' + code
   code = code.replace('int software_asc_begin_calibration(void)', 'int software_asc_begin_manual(void)')
   code = code.replace('s<=180', 's<=60').replace('s<180', 's<60').replace('now+=180000', 'now+=60000')
   code = code.replace('s%5==0', 's>20')
   code = code.replace('native_mode=2;assert(calibration_service_start(426)==-EBUSY && !lease);', 'native_mode=2;')
   code = code.replace('native_mode=1;assert', 'native_mode=2;assert')
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)
   for name,value in headers.items():
    f=p/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(value)
   (p/'test.c').write_text(code)
   subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I',str(p),'-I',str(ROOT/'application/include'),'-I',str(ROOT/'application/src'),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)

 def test_frc_timing_errors_and_restoration(self):
  self._run()
 def test_native_frc_with_asc_enabled_and_startup_bypass(self):
  self._run(native=True)
