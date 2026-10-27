"""Exercise the real sensor manager with injected hardware/status failures."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from history_support import STUBS
ROOT=Path(__file__).resolve().parents[1]

class SensorAscTests(unittest.TestCase):
    def _run_policy(self, native=False, recovery=False, sleep=False, rob=False, all_modes=False, native_test=False):
        source=(ROOT/'application/src/services/sensor_manager.c').read_text()
        source=re.sub(r'^#include .*$', '', source, flags=re.M)
        harness='#include <assert.h>\n' + STUBS + r'''
#include <assert.h>
#include <services/diagnostics.h>
void diagnostics_record(enum diagnostic_event event, uint32_t detail) {}
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <services/sensor_manager.h>
#include <services/health_service.h>
void health_progress(enum health_worker w,uint32_t ms) {}
#include <drivers/stcc4.h>
#if defined(CONFIG_AIRSPOT_STCC4_INTERNAL_ASC_EXPERIMENT)
bool stcc4_native_asc_expected=true;static uint8_t native_mode=2;
#else
bool stcc4_native_asc_expected;static uint8_t native_mode;
#endif
void stcc4_set_asc_policy(bool v) {stcc4_native_asc_expected=v;}
static uint8_t user_settings_get_native_asc_mode(void) {return native_mode;}
static int user_settings_set_native_asc_mode(uint8_t v);
#define LOG_MODULE_REGISTER(...)
#define LOG_ERR(...)
#define LOG_WRN(...)
#define LOG_DBG(...)
#define LOG_INF(...)
#define IS_ENABLED(x) 0
#define K_MUTEX_DEFINE(x) int x
#define K_FOREVER 0
#define K_NO_WAIT 0
#define K_MSEC(x) (x)
#define ARG_UNUSED(x) (void)(x)
struct k_work {int unused;};
struct k_work_delayable {int unused;};
struct k_work_sync {int unused;};
static int pause_calls, resume_calls, reset_calls, restart_calls, pause_error, read_error, reset_error, save_error;
static bool work_executing, work_cancelling;
static int pressure_error, ambient_write_error, pressure_write_error, ambient_writes, pressure_writes, callbacks, scheduled_delay;
static int ambient_error, sensor_restart_notifications, self_test_error, start_error, stop_error;
static uint16_t self_test_word;
static int stale_policy_reads;static bool force_paused_status, unknown_status_mapping;
static int64_t now=1,last_feed;static bool check_watchdog;
static bool internal_paused, manual, callback_valid, continuous, require_continuous;
static bool bypass_active;
bool stcc4_is_starting(void) {return bypass_active;}
static int ota_samples;
void ota_service_observe_sample(const sensor_data_t *d) {if(d->valid)ota_samples++;}
static uint8_t auto_flag, power=2;
static int16_t correction=123;
static void k_mutex_lock(int *m,int t) { (*m)++; }
static void k_mutex_unlock(int *m) { assert(*m>0); (*m)--; }
static void k_msleep(int ms) {now+=ms;if(check_watchdog)assert(now-last_feed<5000);}
static uint32_t k_uptime_get_32(void) {return now;}
static int64_t k_uptime_get(void) {return now;}
static void k_work_init_delayable(struct k_work_delayable *w,void (*f)(struct k_work *)) {}
/* Match Zephyr work.c: schedule ignores a CANCELLING item; reschedule
 * replaces a delayed deadline without asynchronous cancellation. */
static void k_work_schedule(struct k_work_delayable *w,int t) {if(!work_cancelling)scheduled_delay=t;}
static void k_work_reschedule(struct k_work_delayable *w,int t) {assert(!work_cancelling);scheduled_delay=t;}
static void k_work_cancel_delayable(struct k_work_delayable *w) {if(work_executing)work_cancelling=true;scheduled_delay=-1;}
static void k_work_cancel_delayable_sync(struct k_work_delayable *w,struct k_work_sync *s) {}
void watchdog_feed(void) {last_feed=now;}
static int i2c_bus_init(void) {return 0;}
static int sht40_init(void) {return 0;}
static int ambient_resets, pressure_resets, factory_resets;
static bool heal_co2, heal_ambient, heal_pressure;
static int sht40_soft_reset(void) {ambient_resets++;if(heal_ambient)ambient_error=0;return ambient_error;}
typedef struct {float temperature,humidity;} sht40_data_t;
#define SHT40_PRECISION_HIGH 0
static int sht40_measure(sht40_data_t *d,int p) {now+=12;d->temperature=22;d->humidity=45;return ambient_error;}
typedef struct {float pressure;} lps22hh_data_t;
static int lps22hh_init(void) {return 0;}
static int lps22hh_soft_reset(void) {pressure_resets++;if(heal_pressure)pressure_error=0;return pressure_error;}
static int lps22hh_read(lps22hh_data_t *d) {now+=2;d->pressure=1013;return pressure_error;}
static int battery_init(void) {return 0;}
static int qi_charger_init(void) {return 0;}
static int battery_reads;
static uint16_t battery_read_voltage_mv(void) {battery_reads++;return 3900;}
static uint8_t battery_get_percentage(void) {return 80;}
static bool battery_is_low(void) {return false;}
static bool battery_is_critical(void) {return false;}
static bool qi_charger_is_charging(void) {return false;}
static void ble_service_update_battery_level(uint8_t p) {}
static uint8_t user_settings_get_auto_calibrate(void) {return auto_flag;}
static uint8_t user_settings_get_power_mode(void) {return power;}
static int user_settings_set_native_asc_mode(uint8_t v) {if(!save_error)native_mode=v;return save_error;}
static int user_settings_set_power_mode(uint8_t v) {if(!save_error)power=v;return save_error;}
static int sht40_read_serial(uint32_t *v) {*v=1;return 0;}
static int lps22hh_read_id(uint8_t *v) {*v=0xB3;return 0;}
int stcc4_get_product_id(uint8_t *v,size_t n) {return 0;}
static int settling_prepared, settling_started, settling_error;
static int settling_prepare_erase(uint32_t t) {settling_prepared++;return settling_error;}
static int settling_erased(void) {settling_started++;return 0;}
static void settling_observe(bool v) {}
static bool user_settings_is_time_set(void) {return false;}
static uint32_t user_settings_get_time_now(void) {return 0;}
static int user_settings_start_settling(void) {if(!save_error){native_mode=2;power=3;correction=0;auto_flag=0;}return save_error;}
static int user_settings_set_co2_correction(int16_t v) {if(!save_error)correction=v;return save_error;}
static void software_asc_sensor_restarted(void) {sensor_restart_notifications++;}
static void software_asc_end_manual(void) {manual=false;restart_calls++;}
static int software_asc_begin_manual(void) {if(manual)return -EBUSY;manual=true;return 0;}
int stcc4_init(void) {return 0;}
int stcc4_exit_sleep_mode(void) {return 0;}
int stcc4_enable_testing_mode(void) {pause_calls++;if(!pause_error)internal_paused=true;return pause_error;}
int stcc4_disable_testing_mode(void) {resume_calls++;if(!pause_error)internal_paused=false;return pause_error;}
static int shot_calls;
int stcc4_trigger_single_shot(void) {shot_calls++;continuous=false;return 0;}
static int16_t mock_raw_co2=800;
int stcc4_read(stcc4_data_t *d) {
  *d=(stcc4_data_t){.co2_ppm=mock_raw_co2<0?0:(uint16_t)mock_raw_co2,.raw_co2_ppm=mock_raw_co2,.has_raw_co2=!read_error,.valid=true,.status=internal_paused?0x0084:0};
  if (stale_policy_reads || force_paused_status || (require_continuous && !continuous)) {
    d->status=0x0084;
    if(stale_policy_reads)stale_policy_reads--;
  }
  if (unknown_status_mapping) d->status=4;
  return read_error ? read_error : (stcc4_status_matches_asc_policy(d->status) ? 0 : -EACCES);
}
void stcc4_get_reset_diagnostic(stcc4_reset_diagnostic_t *d) {*d=(stcc4_reset_diagnostic_t){.stage=reset_error?4:5,.result=reset_error?65535:0};}
int stcc4_factory_reset(void) {factory_resets++;reset_calls++;assert(!continuous);internal_paused=false;return reset_error;}
int stcc4_reset(void) {reset_calls++;if(heal_co2)read_error=0;internal_paused=false;return reset_error;}
int stcc4_set_ambient(float t,float h) {ambient_writes++;now++;return ambient_write_error;}
int stcc4_set_pressure(uint16_t p) {pressure_writes++;now++;return pressure_write_error;}
uint16_t stcc4_get_co2(void) {return 777;}
static int sleep_calls,sleep_error;
int stcc4_sleep_after_sample(void) {assert(!continuous);sleep_calls++;return sleep_error;}
int stcc4_enter_sleep_mode(void) {return 0;}
int stcc4_stop(void) {if(!stop_error){continuous=false;k_msleep(1200);}return stop_error;}
int stcc4_start_continuous(void) {if(!start_error)continuous=true;return start_error;}
extern int meas_mutex;
int stcc4_perform_self_test(uint16_t *r) {assert(meas_mutex==1 && manual);*r=self_test_word;return self_test_error;}
static int frc_calls, frc_error;
int stcc4_perform_forced_recalibration(uint16_t t,int16_t *a) {assert(!continuous);frc_calls++;*a=-100;return frc_error;}
'''
        tests=r'''
static void observed(const sensor_data_t *d) {callback_valid=d->valid;callbacks++;}
int main(void) {
  for(auto_flag=0;auto_flag<=1;auto_flag++) {
    initialized=false; internal_paused=false;
    assert(sensor_manager_init()==0 && internal_paused);
    sensor_asc_diagnostic_t diagnostic;
    sensor_manager_get_asc_diagnostic(&diagnostic);
    assert(diagnostic.stage==5 && diagnostic.error==0 && diagnostic.status==0x0084);
    sensor_manager_register_callback(observed);
    assert(sensor_manager_measure_now()==0 && callback_valid);
    internal_paused=false;
    assert(sensor_manager_measure_now()==-EIO && !callback_valid);
    sensor_data_t snapshot;
    assert(sensor_manager_get_data(&snapshot)==0 && !snapshot.valid);
    correction=123;
    assert(sensor_manager_reset_co2_sensor()==0 && internal_paused && correction==123);
    assert(sensor_manager_measure_now()==0 && callback_valid);
  }
  assert(pause_calls==4 && resume_calls==0 && sensor_restart_notifications==4);
  initialized=false;ambient_error=-EIO;assert(sensor_manager_init()==-EIO);
  ambient_error=0;
  initialized=false;pause_error=-EIO;internal_paused=false;
  assert(sensor_manager_init()==-EIO && !initialized);
  sensor_asc_diagnostic_t diagnostic;
  sensor_manager_get_asc_diagnostic(&diagnostic);
  assert(diagnostic.stage==2 && diagnostic.error==-EIO && diagnostic.status==0);
  assert(sensor_manager_measure_now()==-ENODEV);
  pause_error=0;read_error=-EIO;assert(sensor_manager_init()==-EIO && !initialized);
  read_error=0;assert(sensor_manager_reset_co2_sensor()==0 && initialized && !manual);
  manual=true;int previous=reset_calls;
  assert(sensor_manager_reset_co2_sensor()==-EBUSY && reset_calls==previous);manual=false;
  correction=100;reset_error=-EIO;
  assert(sensor_manager_reset_co2_sensor()==-EIO && correction==100 && !manual);
  reset_error=0;pause_error=-EIO;
  assert(sensor_manager_reset_co2_sensor()==-EIO && correction==100 && !manual);
  pause_error=0;save_error=-EIO;
  assert(sensor_manager_reset_co2_sensor()==0 && correction==100 && !manual);
  save_error=0;assert(sensor_manager_reset_co2_sensor()==0 && correction==100);
  correction=123;power=3;
  int erases_before_intent=factory_resets;
  settling_error=-EIO;
  assert(sensor_manager_reset_native_calibration()==-EIO && factory_resets==erases_before_intent && correction==123 && !manual);
  settling_error=0;
  assert(sensor_manager_reset_native_calibration()==0 && correction==0 && native_mode==2 && continuous && !internal_paused && !manual);
  correction=100;reset_error=-EIO;
  assert(sensor_manager_reset_native_calibration()==-EIO && correction==100 && !manual);
  assert(continuous && !use_single_shot_mode);
  sensor_reset_diagnostic_t reset_diag;
  sensor_manager_get_reset_diagnostic(&reset_diag);
  assert(reset_diag.stage==3 && reset_diag.erase_stage==4 && reset_diag.sensor_result==65535 && reset_diag.error==-EIO && !reset_diag.recovery_error);
  assert(sensor_manager_measure_now()==0);
  reset_error=0;stop_error=-EIO;
  assert(sensor_manager_reset_native_calibration()==-EIO && correction==100 && !manual);
  stop_error=0;
  assert(sensor_manager_reset_native_calibration()==0 && native_mode==2 && continuous && !internal_paused);
  reset_error=-EIO;pause_error=-EACCES;
  assert(sensor_manager_reset_native_calibration()==-EIO && !initialized && !manual);
  sensor_manager_get_reset_diagnostic(&reset_diag);
  assert(reset_diag.stage==3 && reset_diag.error==-EIO && reset_diag.recovery_error==-EACCES);
  assert(sensor_manager_measure_now()==-ENODEV);
  reset_error=0;pause_error=0;
  assert(sensor_manager_reset_co2_sensor()==0 && initialized && continuous);
  sensor_manager_get_reset_diagnostic(&reset_diag);
  assert(reset_diag.error==-EIO && reset_diag.recovery_error==-EACCES);
  /* Subsequent cases exercise the original caller policy independently. */
#if defined(CONFIG_AIRSPOT_STCC4_INTERNAL_ASC_EXPERIMENT)
  native_mode=2;
#else
  native_mode=0;
#endif
  assert(configure_stcc4_asc(native_mode==2)==0);resume_calls=0;
  uint16_t result;
  correction=123;use_single_shot_mode=false;
  for(self_test_word=0;self_test_word<=0x10;self_test_word+=0x10) {
    assert(sensor_manager_self_test(&result)==0 && result==self_test_word);
    assert(internal_paused && correction==123 && !manual && !use_single_shot_mode);
  }
  self_test_error=-EIO;assert(sensor_manager_self_test(&result)==-EIO && !manual);
  self_test_error=0;stop_error=-EIO;
  assert(sensor_manager_self_test(&result)==-EIO && result==UINT16_MAX && !manual);
  stop_error=0;manual=true;assert(sensor_manager_self_test(&result)==-EBUSY);manual=false;
  start_error=-EIO;use_single_shot_mode=true;
  assert(sensor_manager_self_test(&result)==0 && initialized && use_single_shot_mode);
  use_single_shot_mode=false;assert(sensor_manager_self_test(&result)==-EIO && !initialized);
  assert(sensor_manager_measure_now()==-ENODEV && correction==123);
  assert(sensor_manager_self_test(&result)==-ENODEV && result==UINT16_MAX && !manual);
  assert(sensor_manager_self_test(NULL)==-EINVAL);

  start_error=0;assert(sensor_manager_init()==0);
  sensor_data_t fault_sample;
  ambient_error=-EIO;assert(sensor_manager_measure_now()!=0);
  sensor_manager_get_data(&fault_sample);assert(fault_sample.error_code==SENSOR_FAULT_AMBIENT && !fault_sample.valid);
  ambient_error=0;pressure_error=-EIO;assert(sensor_manager_measure_now()!=0);
  sensor_manager_get_data(&fault_sample);assert(fault_sample.error_code==SENSOR_FAULT_PRESSURE);
  pressure_error=0;ambient_write_error=-EIO;assert(sensor_manager_measure_now()!=0);
  sensor_manager_get_data(&fault_sample);assert(fault_sample.error_code==SENSOR_FAULT_COMPENSATION);
  ambient_write_error=0;read_error=-EIO;assert(sensor_manager_measure_now()!=0);
  sensor_manager_get_data(&fault_sample);assert(fault_sample.error_code==SENSOR_FAULT_CO2);
  read_error=0;assert(sensor_manager_measure_now()==0);
  sensor_manager_get_data(&fault_sample);assert(fault_sample.error_code==0 && fault_sample.valid);

  /* Continuous mode: 1 s compensation starts, one publication per 5 s.
   * I2C work costs 16 ms; scheduling must account for that execution time. */
  start_error=0;assert(sensor_manager_init()==0);
  /* Exact boot order: start with the saved interval, then select hardware mode.
   * The equal 5000 ms interval must not suppress 1 Hz scheduling. */
  sensor_manager_start(5000);
  assert(use_single_shot_mode && scheduled_delay==0);
  /* Work has entered while main holds meas_mutex during the mode command. */
  work_executing=true;
  sensor_manager_set_power_mode(3);
  assert(!work_cancelling);
  work_executing=false;
  assert(scheduled_delay==1000);
  assert(!use_single_shot_mode && measurement_interval_ms==5000);
  sensor_manager_start(5000);
  callbacks=ambient_writes=pressure_writes=0;
  now=100000;next_report_ms=0;
  for(int i=0;i<11;i++) {
    sensor_work_handler(NULL);
    assert(scheduled_delay==984);
    now+=scheduled_delay;
  }
  assert(ambient_writes==11 && pressure_writes==11 && callbacks==3);
  /* Compensation-only ticks neither publish nor feed calibration through callback. */
  int previous_callbacks=callbacks;
  pressure_error=-EIO;sensor_work_handler(NULL);
  assert(callbacks==previous_callbacks && !latest_data.valid);
  pressure_error=0;
  ambient_write_error=-EIO;assert(sensor_manager_measure_now()==-EIO && !callback_valid);
  ambient_write_error=0;pressure_write_error=-EIO;
  assert(sensor_manager_measure_now()==-EIO && !callback_valid);
  pressure_write_error=0;assert(sensor_manager_measure_now()==0 && callback_valid);
  sensor_manager_set_power_mode(1);
  assert(use_single_shot_mode && measurement_interval_ms==180000);
  callbacks=0;sensor_work_handler(NULL);assert(callbacks==1 && scheduled_delay==179434);
  sensor_manager_set_power_mode(0);assert(!running);
  previous_callbacks=callbacks;sensor_work_handler(NULL);assert(callbacks==previous_callbacks);
  sensor_manager_set_power_mode(3);sensor_manager_stop();
  assert(!running);previous_callbacks=callbacks;sensor_work_handler(NULL);
  assert(callbacks==previous_callbacks);
  sensor_manager_start(5000);power=3;assert(sensor_manager_reset_co2_sensor()==0);
  assert(!use_single_shot_mode && measurement_interval_ms==5000);
  sensor_work_handler(NULL);assert(scheduled_delay==984);
  sensor_manager_set_power_mode(1);start_error=-EIO;
  sensor_manager_set_power_mode(3);
  assert(use_single_shot_mode && measurement_interval_ms==180000);
  assert(meas_mutex==0 && data_mutex==0 && resume_calls==0);
  initialized=true;start_error=stop_error=save_error=0;
  unsigned before_mode_events=history_count;
  assert(sensor_manager_change_power_mode(1)==0);
  assert(history_count==before_mode_events+1);
  assert(history_records[history_count-1].type==RECORD_TYPE_MEASUREMENT_MODE_CHANGE);
  assert(history_records[history_count-1].value==1);
  assert(sensor_manager_change_power_mode(1)==0 && history_count==before_mode_events+1);
  assert(power==1 && use_single_shot_mode);
  start_error=-EIO;
  assert(sensor_manager_change_power_mode(3)==-EIO && power==1);
  start_error=0;save_error=-EIO;
  assert(sensor_manager_change_power_mode(3)==-EIO && power==1 && use_single_shot_mode);
  assert(history_count==before_mode_events+1);
}
'''
        if native:
            tests = tests.replace('&& internal_paused', '&& !internal_paused')
            tests = tests.replace('assert(internal_paused &&', 'assert(!internal_paused &&')
            tests = tests.replace('diagnostic.status==0x0084', 'diagnostic.status==0')
            tests = tests.replace('internal_paused=false;\n    assert(sensor_manager_measure_now()', 'internal_paused=true;\n    assert(sensor_manager_measure_now()')
            tests = tests.replace('pause_calls==4 && resume_calls==0', 'pause_calls==4 && resume_calls==4')
            tests = tests.replace('data_mutex==0 && resume_calls==0', 'data_mutex==0')
            tests = tests[:tests.rfind('}')] + r'''
  initialized=false;internal_paused=true;stale_policy_reads=2;
  assert(sensor_manager_init()==0 && !internal_paused && stale_policy_reads==0);
  initialized=false;force_paused_status=true;
  assert(sensor_manager_init()==-EACCES && !initialized);
  last_feed=now;check_watchdog=true;
  assert(configure_stcc4_asc(true)==-EACCES);
  check_watchdog=false;force_paused_status=false;
  initialized=false;require_continuous=true;
  assert(sensor_manager_init()==0 && !continuous);
  initialized=false;start_error=-EIO;
  assert(sensor_manager_init()==-EIO && !initialized);
  start_error=0;initialized=false;stop_error=-EIO;
  assert(sensor_manager_init()==-EIO && !initialized);
  stop_error=0;require_continuous=false;
  initialized=false;unknown_status_mapping=true;
  assert(sensor_manager_init()==-EACCES && !initialized);
  unknown_status_mapping=false;
}
'''

        tests = tests[:tests.rfind('}')] + r'''
  /* Native setting persists only after fresh hardware verification. */
  start_error=stop_error=save_error=read_error=pause_error=0;
  initialized=true;manual=false;unknown_status_mapping=false;force_paused_status=false;
  require_continuous=false;stale_policy_reads=0;use_single_shot_mode=false;
  correction=165;
  assert(sensor_manager_change_native_asc(true)==0);
  assert(native_mode==2 && continuous && !internal_paused && correction==165);
  power=3; /* Reset restores the saved sampling mode. */
  int erases_before=factory_resets;
  assert(sensor_manager_reset_native_calibration()==0);
  assert(factory_resets==erases_before+1 && native_mode==2 && continuous && !internal_paused && correction==0 && !manual);
  assert(sensor_manager_perform_frc(426,NULL)==-EBUSY);
  int previous_resumes=resume_calls;
  assert(sensor_manager_change_native_asc(true)==0 && resume_calls==previous_resumes);
  assert(sensor_manager_change_native_asc(false)==0);
  assert(native_mode==1 && internal_paused && continuous && correction==0);
  manual=true;assert(sensor_manager_change_native_asc(true)==-EBUSY);manual=false;
  save_error=-EIO;
  assert(sensor_manager_change_native_asc(true)==-EIO);
  assert(native_mode==1 && internal_paused && continuous && initialized);
  save_error=0;
  save_error=-EBUSY;power=3;
  assert(sensor_manager_reset_native_calibration()==-EBUSY && native_mode==1 && correction==0 && internal_paused && continuous && !manual);
  save_error=0;
  mock_raw_co2=-155;
  assert(sensor_manager_measure_now()==-EIO);
  sensor_manager_get_asc_diagnostic(&diagnostic);
  assert(diagnostic.read.error==0 && diagnostic.read.raw_co2_ppm==-155);
  assert(diagnostic.read.flags==3 && diagnostic.read.fault==SENSOR_FAULT_CO2);
  uint32_t read_sequence=diagnostic.read.sequence;
  read_error=-EIO;
  assert(sensor_manager_measure_now()==-EIO);
  sensor_manager_get_asc_diagnostic(&diagnostic);
  assert(diagnostic.read.error==-EIO && !(diagnostic.read.flags&1));
  assert(diagnostic.read.sequence==read_sequence+1);
  read_error=0;mock_raw_co2=800;
  assert(sensor_manager_measure_now()==0);
  sensor_manager_get_asc_diagnostic(&diagnostic);
  assert(diagnostic.read.flags==7 && diagnostic.read.fault==0);
  pause_error=-EIO;
  assert(sensor_manager_change_native_asc(true)==-EIO);
  assert(native_mode==1 && !initialized && !latest_data.valid);
}
'''
        if recovery:
            tests = (ROOT / 'tests/automatic_recovery_cases.c').read_text()
        if sleep:
            tests = r'''
int main(void) {
 assert(!sensor_manager_init());
 assert(!sensor_manager_set_power_mode(2));
 int before=sleep_calls;
 assert(!sensor_manager_measure_now()&&sleep_calls==before+1);
 sleep_error=-EIO;assert(sensor_manager_measure_now()==-EIO&&!latest_data.valid);
 sleep_error=0;read_error=-EIO;before=sleep_calls;
 assert(sensor_manager_measure_now()==-EIO&&sleep_calls==before);
 read_error=0;assert(!sensor_manager_set_power_mode(3));before=sleep_calls;
 assert(!sensor_manager_measure_now()&&sleep_calls==before);
 assert(!sensor_manager_set_power_mode(1));before=sleep_calls;
 assert(!sensor_manager_measure_now()&&sleep_calls==before+1);
 int16_t correction;assert(sensor_manager_perform_frc(400,&correction)==-EBUSY);
 assert(!sensor_manager_set_power_mode(0));before=sleep_calls;
 assert(!sensor_manager_measure_now()&&sleep_calls==before+1);
}
'''
        if rob:
            tests=r'''
int main(void) {
 native_mode=2;
 assert(!sensor_manager_init());
 assert(pause_calls==0 && resume_calls==1 && factory_resets==0);
 assert(asc_diagnostic.stage==5 && !asc_diagnostic.error);
 assert(!sensor_manager_set_power_mode(3));
 assert(!sensor_manager_change_native_asc(false));
 assert(native_mode==1 && pause_calls==1 && continuous);
 int16_t applied=0;
 assert(!sensor_manager_perform_frc(426,&applied));
 assert(frc_calls==1 && applied==-100 && continuous && !latest_data.valid);
 stop_error=-EIO;assert(sensor_manager_perform_frc(426,&applied)==-EIO);
 assert(frc_calls==1);stop_error=0;
 frc_error=-EIO;assert(sensor_manager_perform_frc(426,&applied)==-EIO);
 assert(frc_calls==2 && continuous);frc_error=0;
 assert(sensor_manager_perform_frc(32001,&applied)==-EINVAL && frc_calls==2);
 assert(!sensor_manager_change_native_asc(true));
 assert(native_mode==2 && pause_calls==1 && continuous);
 assert(sensor_manager_perform_frc(426,&applied)==-EBUSY && frc_calls==2);
 assert(!sensor_manager_change_native_asc(false));
 start_error=-EIO;assert(sensor_manager_perform_frc(426,&applied)==-EIO);
 assert(!initialized && !latest_data.valid);
}
'''
        if all_modes:
            tests = r'''
static void observed(const sensor_data_t *d) {callback_valid=d->valid;callbacks++;}
int main(void) {
 native_mode=2;
 assert(!sensor_manager_init());
 sensor_manager_register_callback(observed);
 for (int mode=0;mode<4;mode++) {
  assert(!sensor_manager_set_power_mode(mode));
  assert(continuous && !use_single_shot_mode && running && scheduled_delay==1000);
  int count=callbacks, aw=ambient_writes, pw=pressure_writes, br=battery_reads, os=ota_samples;
  uint32_t seq=co2_diagnostic.sequence;
  int64_t start=now;
  for(int i=0;i<4;i++) {now=start+i*1000;sensor_work_handler(NULL);}
  assert(ambient_writes==aw+4 && pressure_writes==pw+4);
  assert(battery_reads<=br+1);
  assert(co2_diagnostic.sequence==seq+4 && callbacks==count && ota_samples==os+4);
  assert(scheduled_delay>0 && scheduled_delay<=1000 && latest_data.valid);
  assert((latest_data.sample_flags>>6)==mode && (latest_data.sample_flags&16));
  if(mode) {now=next_report_ms;sensor_work_handler(NULL);assert(callbacks==count+1);}
  else {now+=200000;sensor_work_handler(NULL);assert(callbacks==count);}
  count=callbacks;assert(!sensor_manager_measure_now());assert(callbacks==count+1);
 }
 assert(!sensor_manager_set_power_mode(0));
 bypass_active=true;mock_raw_co2=390;
 int os=ota_samples, aw=ambient_writes, pw=pressure_writes;
 for(int i=0;i<22;i++) {
  now+=1000;sensor_work_handler(NULL);
  assert(!latest_data.valid && latest_data.error_code==SENSOR_FAULT_STARTING);
  assert(latest_data.co2_ppm==0 && continuous);
 }
 assert(ambient_writes==aw+22 && pressure_writes==pw+22 && ota_samples==os);
 assert(sensor_manager_measure_now()!=0 && !callback_valid);
 bypass_active=false;now+=1000;sensor_work_handler(NULL);
 assert(latest_data.valid && latest_data.co2_ppm==390); /* Real 390 remains valid. */
 int count=callbacks;ambient_error=-EIO;now+=1000;sensor_work_handler(NULL);
 assert(!latest_data.valid && latest_data.error_code==SENSOR_FAULT_AMBIENT && callbacks==count);
 ambient_error=0;now+=1000;sensor_work_handler(NULL);assert(latest_data.valid);
 assert(!sensor_manager_set_power_mode(2));
 assert(continuous && running);
 bypass_active=true;count=callbacks;now+=1000;sensor_work_handler(NULL);
 assert(!latest_data.valid && callbacks==count && !recovery_due());
 ambient_error=-EIO;now+=1000;sensor_work_handler(NULL);
 assert(latest_data.error_code==SENSOR_FAULT_AMBIENT); /* Do not hide real faults. */
 ambient_error=0;now+=1000;sensor_work_handler(NULL);
 bypass_active=false;now+=1000;sensor_work_handler(NULL);
 assert(latest_data.valid && callbacks==count+1); /* Prompt ready report in slow mode. */
 sensor_manager_stop();assert(!running);
}
'''
        if native_test:
            harness += r"""
void calibration_service_process_measurement(uint16_t co2) {}
void initial_settling_observe(bool valid,bool single,bool paired,bool low) {}
uint32_t initial_settling_remaining_seconds(void) {return 1800;}
bool user_settings_is_paired(void) {return true;}
"""
            tests = r"""
static void observed(const sensor_data_t *d) {callback_valid=d->valid;callbacks++;}
int main(void) {
 native_mode=2;assert(!sensor_manager_init());
 assert(sensor_manager_change_native_asc(false)==-ENOTSUP);
 assert(native_mode==2 && !internal_paused);
 bool settling_active;uint32_t settling_remaining;
 sensor_manager_get_initial_settling(&settling_active,&settling_remaining);
 assert(settling_active && settling_remaining==1800);
 sensor_manager_register_callback(observed);
 assert(sensor_manager_set_power_mode(0)==-EINVAL);
 assert(sensor_manager_set_power_mode(2)==-EINVAL);
 for(int mode=1;mode<=3;mode+=2) {
  assert(!sensor_manager_set_power_mode(mode));
  assert(measurement_interval_ms==10000 && running);
  assert(use_single_shot_mode==(mode==1));
  int aw=ambient_writes,pw=pressure_writes,count=callbacks;
  now=next_report_ms;sensor_work_handler(NULL);
  assert(callbacks==count+1 && latest_data.valid);
  assert(ambient_writes>aw && pressure_writes>pw);
  assert((latest_data.sample_flags>>6)==mode);
  assert(scheduled_delay>0 && scheduled_delay<=1000);
  if(mode==1) {
   int shots=shot_calls, reports=callbacks, aw=ambient_writes, pw=pressure_writes;
   int64_t due=next_report_ms;
   for(int i=9;i>=1;i--) {now=due-i*1000;sensor_work_handler(NULL);}
   assert(shot_calls==shots && callbacks==reports);
   assert(ambient_writes==aw+9 && pressure_writes==pw+9);
   now=due;sensor_work_handler(NULL);
   assert(shot_calls==shots+1 && callbacks==reports+1);
  }
  assert(((latest_data.sample_flags&16)!=0)==(mode==3));
 }
 int16_t applied=0;assert(!sensor_manager_perform_frc(426,&applied));
 assert(applied==-100 && continuous && native_mode==2);
}
"""
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);(p/'test.c').write_text(harness+source+tests)
            subprocess.run(['cc',*(['-DCONFIG_AIRSPOT_NATIVE_TEST_MODES=1'] if native_test else []),*(['-DCONFIG_AIRSPOT_CONTINUOUS_ALL_MODES=1'] if all_modes else []),*(['-DCONFIG_AIRSPOT_SENSOR_NATIVE_ONLY=1'] if rob or all_modes else []),*(['-DCONFIG_AIRSPOT_STCC4_SINGLE_SHOT_SLEEP=1'] if sleep else []),*(['-DCONFIG_AIRSPOT_STCC4_INTERNAL_ASC_EXPERIMENT=1'] if native else []),'-std=c11','-fsanitize=address,undefined','-I',str(ROOT/'application/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

    def test_startup_reset_and_measurement_policy(self):
        self._run_policy()

    def test_native_asc_startup_recovery_and_compensation(self):
        self._run_policy(native=True)

    def test_automatic_recovery_faults_backoff_and_shutdown(self):
        self._run_policy(recovery=True)

    def test_automatic_recovery_preserves_native_asc(self):
        self._run_policy(native=True, recovery=True)

    def test_sleep_candidate_modes_failures_and_frc_guard(self):
        self._run_policy(sleep=True)

    def test_rob_native_policy_and_frc_failures(self):
        self._run_policy(rob=True)

    def test_continuous_acquisition_reporting_and_compensation_in_all_modes(self):
        self._run_policy(all_modes=True)

    def test_native_test_modes_cadence_compensation_and_frc(self):
        self._run_policy(all_modes=True, native_test=True)
