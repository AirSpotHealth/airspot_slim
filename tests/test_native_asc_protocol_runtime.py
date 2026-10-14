"""Exercise native ASC read/write responses and malformed-request rejection."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class NativeAscProtocolTests(unittest.TestCase):
 def test_authoritative_state_and_errors(self):
  source=(ROOT/'application/src/services/protocol.c').read_text()
  case=source[source.index('  case CMD_NATIVE_ASC:'):source.index('  case CMD_CO2_READ_DIAGNOSTIC:')]
  code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <services/protocol.h>
#include <services/sensor_manager.h>
#include <drivers/stcc4.h>
static uint8_t mode;static int calls,write_error,states;static bool ready=true;
static sensor_asc_diagnostic_t diagnostic={5,0,0x84};
static uint8_t reply[16];static int size;
int sensor_manager_change_native_asc(bool enabled) {
 calls++;if(!write_error){mode=enabled?2:1;diagnostic.status=enabled?4:0x84;}return write_error;
}
void sensor_manager_get_asc_diagnostic(sensor_asc_diagnostic_t *v) {*v=diagnostic;}
bool sensor_manager_is_ready(void) {return ready;}
static uint8_t user_settings_get_native_asc_mode(void) {return mode;}
void protocol_send_device_state(void) {states++;}
static void set_frame_checksum(uint8_t *f,int n) {unsigned s=0;for(int i=0;i<n-1;i++)s+=f[i];f[n-1]=s;}
static void send_frame(uint8_t *f,int n) {size=n;memcpy(reply,f,n);}
static void handle(uint8_t *frame,int len) {switch(frame[2]) {
'''+case+r'''
}}
int main(void) {
 uint8_t f[7]={255,170,CMD_NATIVE_ASC,0,0,0,0};
 handle(f,5);assert(calls==0 && size==12 && reply[3]==7 && reply[4]==1 && reply[5]==0 && reply[6]==1);
 f[3]=1;f[4]=1;handle(f,6);assert(calls==1 && mode==2 && reply[5]==2 && reply[6]==1 && reply[10]==4);
 write_error=-5;f[4]=0;handle(f,6);assert(calls==2 && mode==2 && reply[5]==2 && reply[7]==255 && reply[8]==251);
 write_error=0;handle(f,6);assert(calls==3 && mode==1 && reply[5]==1 && reply[10]==0x84);
 f[4]=2;handle(f,6);assert(calls==3 && reply[8]==(uint8_t)-EINVAL);
 f[4]=1;handle(f,5);assert(calls==3);f[3]=2;handle(f,7);assert(calls==3);
 f[3]=0;handle(f,6);assert(calls==3);handle(f,5);assert(reply[8]==0);
 diagnostic.status=4;handle(f,5);assert(reply[6]==0);
 diagnostic.status=0x84;ready=false;handle(f,5);assert(reply[6]==0);
 ready=true;diagnostic.error=-13;handle(f,5);assert(reply[6]==0);
}
'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.c').write_text(code)
   subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I',str(ROOT/'application/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
