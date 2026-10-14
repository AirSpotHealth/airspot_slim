"""Explicit native-history reset must require the full deliberate payload."""
from pathlib import Path
import subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
class NativeResetTests(unittest.TestCase):
 def test_reset_payload_and_result(self):
  source=(ROOT/'application/src/services/protocol.c').read_text()
  case=source[source.index('  case CMD_RESET_NATIVE_CALIBRATION:'):source.index('  case CMD_SENSOR_RECOVERY_DIAGNOSTIC:')]
  code=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <services/protocol.h>
#include <services/sensor_manager.h>
static int calls,events,state_calls,result,size;static uint8_t reply[14];
void sensor_manager_get_reset_diagnostic(sensor_reset_diagnostic_t *d) {*d=(sensor_reset_diagnostic_t){.stage=3,.erase_stage=4,.sensor_result=65535,.error=-5};}
#define RECORD_TYPE_SENSOR_USER_FACTORY_RESET 1
int sensor_manager_reset_native_calibration(void) {calls++;return result;}
static int history_event_record(int a,int b) {events++;return 0;}
void protocol_send_device_state(void) {state_calls++;}
static void sys_put_be16(uint16_t n,uint8_t *p) {p[0]=n>>8;p[1]=n;}
static void set_frame_checksum(uint8_t *p,int n) {uint8_t sum=0;for(int i=0;i<n-1;i++)sum+=p[i];p[n-1]=sum;}
static void send_frame(uint8_t *p,int n) {size=n;memcpy(reply,p,n);}
static void handle(uint8_t *frame,int len) {switch(frame[2]) {
''' + case + r'''}}
int main(void) {
 uint8_t frame[9]={255,170,CMD_RESET_NATIVE_CALIBRATION,3,'R','S','T',0,0};
 handle(frame,5);assert(!calls);
 frame[3]=0;handle(frame,8);assert(!calls);
 frame[3]=3;frame[4]='X';handle(frame,8);assert(!calls);
 frame[4]='R';handle(frame,9);assert(!calls);
 handle(frame,8);assert(calls==1 && events==1 && state_calls==1 && size==7 && !reply[4] && !reply[5]);
 result=-5;handle(frame,8);assert(calls==2 && events==1 && reply[4]==255 && reply[5]==251);
 uint8_t sum=0;for(int i=0;i<6;i++)sum+=reply[i];assert(sum==reply[6]);
 frame[3]=0;handle(frame,5);assert(calls==2 && size==14 && reply[4]==1 && reply[5]==3 && reply[6]==4 && reply[7]==255 && reply[8]==255 && reply[9]==255 && reply[10]==251 && !reply[11] && !reply[12]);
}
'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.c').write_text(code)
   subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I',str(ROOT/'application/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
