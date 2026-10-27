import re, unittest
from native_support import ROOT, run_c, BYTEORDER, CRC
class InitialSettlingTests(unittest.TestCase):
 def test_setup_hour_persistence_reset_and_low_battery(self):
  source=(ROOT/'application/src/services/initial_settling.c').read_text()
  source=re.sub(r'^#include .*$', '', source, flags=re.M)
  run_c(r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
typedef int (*settings_read_cb)(void*,void*,size_t);
#define SETTINGS_STATIC_HANDLER_DEFINE(...)
static int64_t clock_ms;
static int writes,blinks,stored;
static bool confirmed=true;
static int64_t k_uptime_get(void){return clock_ms;}
static bool boot_is_img_confirmed(void){return confirmed;}
static uint8_t record[12];
static int settings_save_one(const char *key,const void *value,size_t len){assert(len==12);writes++;memcpy(record,value,len);stored=record[4]==0 && record[5]==0x36 && record[6]==0xee && record[7]==0x80;return 0;}
static int rd(void *arg,void *out,size_t len){memcpy(out,arg,len);return len;}
static void led_indicator_show_settling_tick(void){blinks++;}
"""+BYTEORDER+CRC+source+r"""
int main(void){
 assert(initial_settling_remaining_seconds()==3600);
 initial_settling_observe(true,false,false,false);assert(blinks==0 && writes==0);
 for(int i=0;i<=360;i++){clock_ms=i*10000LL;initial_settling_observe(true,true,true,false);}
 assert(initial_settling_remaining_seconds()==0);
 assert(complete && stored==1 && writes==60 && blinks==360);
 int old=blinks;clock_ms+=10000;initial_settling_observe(true,true,true,false);assert(blinks==old);
 // Legacy completed devices remain completed after upgrade.
 uint8_t legacy=1;assert(!load("complete",1,rd,&legacy));assert(complete);
 assert(!initial_settling_reset() && !complete && stored==0);
 assert(initial_settling_remaining_seconds()==3600);
 initial_settling_observe(true,false,true,true);assert(blinks==old);
 clock_ms+=50000;initial_settling_observe(true,false,true,false);assert(measured_ms==0);
 // Persist partial progress, reboot, and do not count time powered off.
 for(int i=0;i<61;i++){clock_ms+=1000;initial_settling_observe(true,false,true,false);}
 assert(measured_ms>=60000);uint32_t checkpoint= persisted_ms;assert(checkpoint>=60000);
 measured_ms=0;last_sample=-1;assert(!load("complete",12,rd,record));
 assert(measured_ms==checkpoint && !complete);
 clock_ms+=14400000;initial_settling_observe(true,false,true,false);
 assert(measured_ms==checkpoint);
 record[8]^=1;assert(load("complete",12,rd,record)==-EINVAL);
 confirmed=false;measured_ms=3600000;initial_settling_observe(true,false,true,false);assert(!complete);
 confirmed=true;clock_ms+=30000;initial_settling_observe(true,false,true,false);assert(complete);
}
""")
