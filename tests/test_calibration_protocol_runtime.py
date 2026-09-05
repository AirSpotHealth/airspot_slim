"""Exercise calibration BLE handlers, preserving legacy layouts on failures."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class CalibrationProtocolTests(unittest.TestCase):
    def test_software_toggle_and_reset_ack(self):
        source=(ROOT/'application/src/services/protocol.c').read_text()
        def case(start,end): return source[source.index('  case '+start+':'):source.index('  case '+end+':')]
        cases=(case('CMD_SET_SELF_CALIB','CMD_GET_ASC_COUNT')+
               case('CMD_RESET_SENSOR','CMD_FIND_MY_AIRSPOT')+
               case('CMD_SET_ASC_CHECK_DURATION','CMD_GET_SENSOR_VARIANT'))
        harness=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <services/protocol.h>
#define LOG_INF(...)
#define LOG_WRN(...)
static int save_error, reset_error, measure_error, enabled, mode_commands, state_reads, duration;
static uint8_t reply[16];static uint16_t reply_len;
static int software_asc_set_enabled(uint8_t e) {if(!save_error)enabled=e;return save_error;}
static int sensor_manager_reset_co2_sensor(void) {return reset_error;}
static int sensor_manager_measure_now(void) {return measure_error;}
static void protocol_send_device_state(void) {state_reads++;}
static void software_asc_set_check_duration(uint16_t s) {duration=s;}
static uint8_t software_asc_get_day_count(void) {return 4;}
static void set_frame_checksum(uint8_t *f,uint16_t n) {uint8_t s=0;for(int i=0;i<n-1;i++)s+=f[i];f[n-1]=s;}
static void send_frame(const uint8_t *f,uint16_t n) {memcpy(reply,f,n);reply_len=n;}
static void handle(const uint8_t *frame,uint16_t len) {switch(frame[2]) {
'''
        tests=r'''
}}
int main(void) {
  uint8_t f[7]={255,170,CMD_SET_SELF_CALIB,1,1,0,0};
  handle(f,6);assert(enabled==1 && reply[4]==2 && reply_len==6 && state_reads==1);
  save_error=-1;f[4]=0;handle(f,6);assert(enabled==1 && reply[4]==255 && state_reads==2);
  save_error=0;handle(f,6);assert(enabled==0 && reply[4]==2);
  f[2]=CMD_RESET_SENSOR;handle(f,6);assert(reply[4]==1 && enabled==0);
  reset_error=-1;handle(f,6);assert(reply[4]==255);
  reset_error=0;measure_error=-1;handle(f,6);assert(reply[4]==255);
  f[2]=CMD_SET_ASC_CHECK_DURATION;f[3]=2;f[4]=1;f[5]=2;
  handle(f,6);assert(duration==0);handle(f,7);assert(duration==258);
  f[2]=CMD_GET_ASC_DAY_COUNT;handle(f,5);assert(reply[4]==4 && reply_len==6);
}
'''
        # Header declares protocol_send_device_state externally; use matching linkage.
        harness=harness.replace('static void protocol_send_device_state','void protocol_send_device_state')
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);(p/'test.c').write_text(harness+cases+tests)
            subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I',str(ROOT/'application/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
