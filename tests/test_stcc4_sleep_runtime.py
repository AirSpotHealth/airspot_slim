"""Real driver sleep/wake command ordering, preserved policy and bus failures."""
import re
import unittest
from native_support import ROOT, run_c

class SleepTests(unittest.TestCase):
    def test_wake_before_every_host_command(self):
        source = re.sub(r'^#include .*$', '', (ROOT/'application/src/drivers/stcc4.c').read_text(), flags=re.M)
        run_c(r'''
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <drivers/stcc4.h>
#define LOG_MODULE_REGISTER(...)
#define LOG_ERR(...)
#define LOG_INF(...)
#define LOG_WRN(...)
#define LOG_DBG(...)
#define ARG_UNUSED(x) (void)(x)
#define I2C_ADDR_STCC4 0x64
static int64_t now;
static int count,fail;
static uint16_t commands[80];
static int64_t k_uptime_get(void){return now;}
static void k_msleep(int ms){now+=ms;}
static bool i2c_bus_is_ready(void){return true;}
static int i2c_bus_write(uint8_t addr,const uint8_t *p,size_t n){
 uint16_t c=n==1?0:((p[0]<<8)|p[1]);commands[count++]=c;
 if(n==1)return -EIO; /* expected wake-byte NACK */
 return c==fail?-EIO:0;
}
static int i2c_bus_read(uint8_t a,uint8_t *p,size_t n){memset(p,0,n);return 0;}
static float sht40_get_temperature(void){return 22;}
static float sht40_get_humidity(void){return 45;}
''' + source + r'''
int main(void){
 assert(!stcc4_init());stcc4_set_asc_policy(true);
 assert(!stcc4_sleep_after_sample()&&sleeping&&commands[0]==0x3650&&now==1);
 assert(!stcc4_sleep_after_sample()&&count==1); // no extra stop or sleep
 assert(!stcc4_set_ambient(22,45));assert(commands[1]==0&&commands[2]==0xe000&&now>=7&&!sleeping);
 assert(!stcc4_sleep_after_sample());int start=count;
 assert(!stcc4_set_pressure(1013));assert(commands[start]==0&&commands[start+1]==0xe016);
 assert(!stcc4_sleep_after_sample());start=count;
 assert(!stcc4_start_continuous());assert(commands[start]==0&&commands[start+1]==0x218b);
 assert(stcc4_sleep_after_sample()==-EBUSY);assert(stcc4_native_asc_expected);
 assert(!stcc4_stop());fail=0x3650;assert(stcc4_sleep_after_sample()==-EIO&&sleeping);
 fail=0;start=count;assert(!stcc4_set_pressure(1013));assert(commands[start]==0);
 assert(!stcc4_sleep_after_sample());fail=0xe016;
 assert(stcc4_set_pressure(1013)==-EIO); // wake NACK isn't mistaken for command success
 fail=0;assert(!stcc4_sleep_after_sample());start=count;
 assert(!stcc4_enable_testing_mode());assert(commands[start]==0&&commands[start+1]==0x3fbc);
 assert(!stcc4_sleep_after_sample());int16_t correction;
 assert(stcc4_perform_forced_recalibration(400,&correction)==-EBUSY);
}
''')
