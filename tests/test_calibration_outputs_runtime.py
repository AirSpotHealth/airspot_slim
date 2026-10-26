"""Live BLE and history must apply the same correction to the raw snapshot once."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from history_support import declarations
ROOT=Path(__file__).resolve().parents[1]

def function(source, name):
    start=source.index(name)
    return source[start:source.index('\n}', start)+2]

class CalibrationOutputTests(unittest.TestCase):
    def test_live_and_history_agree(self):
        protocol=(ROOT/'application/src/services/protocol.c').read_text()
        main=(ROOT/'application/src/main.c').read_text()
        settings=(ROOT/'application/src/services/user_settings.c').read_text()
        code=declarations()+r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <services/sensor_manager.h>
#include <services/user_settings.h>
#include <services/protocol.h>
#define LOG_WRN(...)
#define LOG_ERR(...)
#define LOG_DBG(...)
#define LOG_INF(...)
uint8_t user_settings_get_native_asc_mode(void) {return 0;}
uint8_t user_settings_get_auto_calibrate(void) {return 0;}
int16_t user_settings_get_co2_correction(void);
#define K_NO_WAIT 0
static user_settings_t settings;
int16_t user_settings_get_co2_correction(void) {return settings.co2_correction;}
static sensor_data_t snapshot={.co2_ppm=800,.valid=true};
static uint8_t reply[32];static int records;static uint16_t stored;
static uint32_t now=123, stored_time;
int sensor_manager_get_data(sensor_data_t *d) {*d=snapshot;return 0;}
bool user_settings_is_time_set(void) {return true;}
uint32_t user_settings_get_time_now(void) {return now;}
static uint32_t k_uptime_get_32(void) {return 1000;}
static int data_log_store_record(uint32_t t,uint16_t ppm,int type) {records++;stored=ppm;stored_time=t;return 0;}
static void set_frame_checksum(uint8_t *f,uint16_t n) {}
static void send_frame(const uint8_t *f,uint16_t n) {memcpy(reply,f,n);}
'''
        code+=r'''
static struct {uint32_t timestamp;uint16_t value;} pending[32];
static int head, tail;
static int history_event_record(uint16_t v,int type) {
  if(tail-head==32)return -1;
  pending[tail%32].timestamp=now;pending[tail++%32].value=v;return 0;
}
static struct history_event diagnostics[12];
static size_t diagnostic_count;
static int history_event_batch(const struct history_event *events, size_t count) {
  assert(count <= 12);
  diagnostic_count = count;
  memcpy(diagnostics, events, count * sizeof(*events));
  for (size_t i=0;i<count;i++)
    if (events[i].type == RECORD_TYPE_CO2)
      history_event_record(events[i].value, events[i].type);
  return 0;
}
static int history_events_process(void) {
  if(head==tail)return 0;
  int i=head++%32;
  return data_log_store_record(pending[i].timestamp,pending[i].value,0);
}
'''
        code+=function(settings,'uint16_t user_settings_apply_co2_correction(')+'\n'
        code+=function(protocol,'static uint16_t apply_co2_correction(')+'\n'
        code+=function(protocol,'void protocol_send_realtime_co2(')+'\n'
        code+=function(main,'static void queue_co2_data(')+'\n'
        code+=function(main,'static void store_co2_data(void) {')+'\n'
        code+=r'''
int main(void) {
  int16_t corrections[]={100,-374,INT16_MIN,INT16_MAX};
  uint16_t expected[]={900,426,0,33567};
  for(int i=0;i<4;i++) {
    settings.co2_correction=corrections[i];
    protocol_send_realtime_co2();queue_co2_data(&snapshot);store_co2_data();
    assert(((reply[8]<<8)|reply[9])==expected[i] && stored==expected[i]);
    assert(snapshot.co2_ppm==800);
  }
  snapshot.valid=false;int previous=records;
  protocol_send_realtime_co2();queue_co2_data(&snapshot);store_co2_data();
  assert(reply[8]==255 && reply[9]==255 && records==previous);
  assert(diagnostics[0].type==RECORD_TYPE_SAMPLE_FLAGS);
  queue_co2_data(NULL);store_co2_data();assert(records==previous);
  snapshot.valid=true;snapshot.sample_flags=31;snapshot.raw_co2_ppm=380;
  snapshot.sensor_status=32768;snapshot.battery_mv=3800;snapshot.battery_percent=72;
  snapshot.temperature_c=-2.5f;snapshot.humidity_rh=50.25f;snapshot.pressure_hpa=1013;
  settings.co2_correction=40;queue_co2_data(&snapshot);store_co2_data();
  assert(diagnostic_count==12);
  assert(diagnostics[1].type==RECORD_TYPE_RAW_CO2 && diagnostics[1].value==380);
  assert(diagnostics[2].value==32768);
  assert(diagnostics[4].value==40 && (int16_t)diagnostics[5].value==-250);
  assert(diagnostics[6].value==5025 && diagnostics[7].value==1013);
  assert(diagnostics[8].value==72 && diagnostics[9].value==3800);
  previous=records;
  /* Preserve acquisition values/times despite later measurements or settings. */
  snapshot.valid=true;settings.co2_correction=100;
  now=5;snapshot.co2_ppm=500;queue_co2_data(&snapshot);
  now=10;snapshot.co2_ppm=600;queue_co2_data(&snapshot);
  now=60;snapshot.co2_ppm=700;queue_co2_data(&snapshot);
  now=180;snapshot.co2_ppm=800;queue_co2_data(&snapshot);
  settings.co2_correction=0;snapshot.co2_ppm=999;now=1000;
  uint32_t times[]={5,10,60,180};
  for(int i=0;i<4;i++) {
    store_co2_data();assert(stored==600+i*100 && stored_time==times[i]);
  }
  assert(records==previous+4);
  store_co2_data();assert(records==previous+4); /* Never re-log cached data. */
}
'''
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);(p/'test.c').write_text(code)
            subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I',str(ROOT/'application/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
