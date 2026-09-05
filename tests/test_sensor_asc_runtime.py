"""Exercise the real sensor manager with injected hardware/status failures."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

class SensorAscTests(unittest.TestCase):
    def test_startup_reset_and_measurement_policy(self):
        source=(ROOT/'application/src/services/sensor_manager.c').read_text()
        source=re.sub(r'^#include .*$', '', source, flags=re.M)
        harness=r'''
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <services/sensor_manager.h>
#include <drivers/stcc4.h>
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
static int64_t now=1;
static bool internal_paused, manual, callback_valid;
static uint8_t auto_flag, power=2;
static int16_t correction=123;
static void k_mutex_lock(int *m,int t) { (*m)++; }
static void k_mutex_unlock(int *m) { assert(*m>0); (*m)--; }
static void k_msleep(int ms) {now+=ms;}
static uint32_t k_uptime_get_32(void) {return now;}
static int64_t k_uptime_get(void) {return now;}
static void k_work_init_delayable(struct k_work_delayable *w,void (*f)(struct k_work *)) {}
/* Match Zephyr work.c: schedule ignores a CANCELLING item; reschedule
 * replaces a delayed deadline without asynchronous cancellation. */
static void k_work_schedule(struct k_work_delayable *w,int t) {if(!work_cancelling)scheduled_delay=t;}
static void k_work_reschedule(struct k_work_delayable *w,int t) {assert(!work_cancelling);scheduled_delay=t;}
static void k_work_cancel_delayable(struct k_work_delayable *w) {if(work_executing)work_cancelling=true;scheduled_delay=-1;}
static void k_work_cancel_delayable_sync(struct k_work_delayable *w,struct k_work_sync *s) {}
void watchdog_feed(void) {}
static int i2c_bus_init(void) {return 0;}
static int sht40_init(void) {return 0;}
typedef struct {float temperature,humidity;} sht40_data_t;
#define SHT40_PRECISION_HIGH 0
static int sht40_measure(sht40_data_t *d,int p) {now+=12;d->temperature=22;d->humidity=45;return ambient_error;}
typedef struct {float pressure;} lps22hh_data_t;
static int lps22hh_init(void) {return 0;}
static int lps22hh_read(lps22hh_data_t *d) {now+=2;d->pressure=1013;return pressure_error;}
static int battery_init(void) {return 0;}
static int qi_charger_init(void) {return 0;}
static uint16_t battery_read_voltage_mv(void) {return 3900;}
static uint8_t battery_get_percentage(void) {return 80;}
static bool battery_is_low(void) {return false;}
static bool battery_is_critical(void) {return false;}
static bool qi_charger_is_charging(void) {return false;}
static void ble_service_update_battery_level(uint8_t p) {}
static uint8_t user_settings_get_power_mode(void) {return power;}
static int user_settings_set_co2_correction(int16_t v) {if(!save_error)correction=v;return save_error;}
static void software_asc_sensor_restarted(void) {sensor_restart_notifications++;}
static void software_asc_end_manual(void) {manual=false;restart_calls++;}
static int software_asc_begin_manual(void) {if(manual)return -EBUSY;manual=true;return 0;}
int stcc4_init(void) {return 0;}
int stcc4_enable_testing_mode(void) {pause_calls++;if(!pause_error)internal_paused=true;return pause_error;}
int stcc4_disable_testing_mode(void) {resume_calls++;internal_paused=false;return 0;}
int stcc4_trigger_single_shot(void) {return 0;}
int stcc4_read(stcc4_data_t *d) {
  *d=(stcc4_data_t){.co2_ppm=800,.valid=true,.status=internal_paused?0x0084:0};
  return read_error;
}
int stcc4_reset(void) {reset_calls++;internal_paused=false;return reset_error;}
int stcc4_set_ambient(float t,float h) {ambient_writes++;now++;return ambient_write_error;}
int stcc4_set_pressure(uint16_t p) {pressure_writes++;now++;return pressure_write_error;}
uint16_t stcc4_get_co2(void) {return 777;}
int stcc4_enter_sleep_mode(void) {return 0;}
int stcc4_stop(void) {return stop_error;}
int stcc4_start_continuous(void) {return start_error;}
extern int meas_mutex;
int stcc4_perform_self_test(uint16_t *r) {assert(meas_mutex==1 && manual);*r=self_test_word;return self_test_error;}
int stcc4_perform_forced_recalibration(uint16_t t,int16_t *a) {assert(false);return 0;}
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
    assert(sensor_manager_reset_co2_sensor()==0 && internal_paused && correction==0);
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
  assert(sensor_manager_reset_co2_sensor()==-EIO && correction==100 && !manual);
  save_error=0;assert(sensor_manager_reset_co2_sensor()==0 && correction==0);
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
  now=100000;next_report_ms=0;last_compensation_ms=0;timing=(sensor_timing_diagnostic_t){0};
  for(int i=0;i<11;i++) {
    sensor_work_handler(NULL);
    assert(scheduled_delay==984);
    now+=scheduled_delay;
  }
  assert(ambient_writes==11 && pressure_writes==11 && callbacks==3);
  sensor_timing_diagnostic_t timing_snapshot;
  sensor_manager_get_timing_diagnostic(&timing_snapshot);
  assert(timing_snapshot.continuous && timing_snapshot.updates==11);
  assert(timing_snapshot.last_gap_ms==1000 && timing_snapshot.max_gap_ms==1000);
  assert(timing_snapshot.report_interval_ms==5000 && timing_snapshot.failures==0);
  /* Compensation-only ticks neither publish nor feed calibration through callback. */
  int previous_callbacks=callbacks;
  pressure_error=-EIO;sensor_work_handler(NULL);
  assert(callbacks==previous_callbacks && !latest_data.valid && timing.failures==1);
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
}
'''
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);(p/'test.c').write_text(harness+source+tests)
            subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I',str(ROOT/'application/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
