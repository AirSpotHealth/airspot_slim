"""Slim's shared state frame reserves Screen fields without exposing controls."""
import unittest
from native_support import ROOT,run_c
class SlimStateTests(unittest.TestCase):
    def test_supported_settings_keep_wire_offsets(self):
        source=(ROOT/'application/src/services/protocol.c').read_text()
        start=source.index('void protocol_send_device_state(void)')
        code=source[start:source.index('\n}',start)+2]
        run_c(r'''
#include <assert.h>
#include <string.h>
#include <services/user_settings.h>
#include <services/protocol.h>
#define LOG_INF(...)
static uint8_t reply[80];static int length;
void user_settings_get_snapshot(user_settings_t *s) {
 memset(s,0,sizeof(*s));s->power_mode=3;s->auto_calibrate=1;
 s->co2_yellow_start=900;s->co2_red_start=1200;s->calib_target=426;
}
uint16_t user_settings_get_calib_target(void) {return 426;}
static void set_frame_checksum(uint8_t *f,int n) {for(int i=0;i<n-1;i++)f[n-1]+=f[i];}
static void send_frame(uint8_t *f,int n) {memcpy(reply,f,n);length=n;}
'''+code+r'''
int main(void) {
 protocol_send_device_state();assert(length==73 && reply[3]==68);
 assert(reply[4]==0 && reply[5]==0 && reply[6]==3);
 assert(((reply[7]<<8)|reply[8])==900 && ((reply[9]<<8)|reply[10])==1200);
 assert(reply[11]==0 && reply[12]==1);
 for(int i=13;i<18;i++)assert(reply[i]==0);
 assert(((reply[18]<<8)|reply[19])==426);
 assert(reply[71]==0);
 uint8_t sum=0;for(int i=0;i<72;i++)sum+=reply[i];assert(sum==reply[72]);
}
''')
