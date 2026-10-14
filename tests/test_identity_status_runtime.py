import re
import unittest
from native_support import ROOT, run_c

class IdentityStatusTests(unittest.TestCase):
    def test_factory_identity_is_stable_and_distinguishes_units(self):
        code=(ROOT/'application/src/services/device_identity.c').read_text()
        code=re.sub(r'^#include .*$', '', code, flags=re.M)
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#define NRF_FICR 0
static uint32_t words[]={0x12345678,0x00009abc};
static uint32_t nrf_ficr_deviceaddr_get(int p, unsigned i) {return words[i];}
''' + code + r'''
int main(void) {
 uint8_t a[6],b[6],expected[]={0x9a,0xbc,0x12,0x34,0x56,0x78};
 assert(device_identity_get(a)==0 && !memcmp(a,expected,6));
 assert(device_identity_get(b)==0 && !memcmp(a,b,6));
 words[0]++;assert(device_identity_get(b)==0 && memcmp(a,b,6));
 words[0]=words[1]=0;assert(device_identity_get(b)==-ENODATA);
 words[0]=words[1]=UINT32_MAX;assert(device_identity_get(b)==-ENODATA);
}
''')

    def test_sensor_details_before_first_sample_never_read_uninitialized_pressure(self):
        source=(ROOT/'application/src/services/protocol.c').read_text()
        code=source[source.index('  case CMD_GET_SENSOR_DETAILS:'):source.index('  case CMD_GET_BATTERY_LEVEL:')]
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <services/protocol.h>
#include <services/sensor_manager.h>
#define LOG_INF(...)
#define LOG_ERR(...)
static int sample_error;
static bool valid=true;
static float pressure=1013;
static uint8_t reply[32];
int sensor_manager_get_data(sensor_data_t *s) {
 if(sample_error)return sample_error; // Deliberately leave the output untouched.
 s->valid=valid;s->pressure_hpa=pressure;return 0;
}
static int device_identity_get(uint8_t *s) {for(int i=0;i<6;i++)s[i]=i+1;return 0;}
static uint8_t user_settings_get_auto_calibrate(void) {return 0;}
static uint16_t user_settings_get_calib_target(void) {return 420;}
static void set_frame_checksum(uint8_t *f,int n) {for(int i=0;i<n-1;i++)f[n-1]+=f[i];}
static void send_frame(uint8_t *f,int n) {assert(n==21);memcpy(reply,f,n);}
static void request(void) {switch(CMD_GET_SENSOR_DETAILS) {
''' + code + r'''
}}
int main(void) {
 sample_error=-1;request();assert(reply[8]==0 && reply[9]==0);
 sample_error=0;request();assert(((reply[8]<<8)|reply[9])==1013);
 for(int i=0;i<6;i++)assert(reply[13+i]==i+1);
 valid=false;request();assert(reply[8]==0 && reply[9]==0);
 valid=true;pressure=NAN;request();assert(reply[8]==0 && reply[9]==0);
 pressure=INFINITY;request();assert(reply[8]==0 && reply[9]==0);
}
''')
