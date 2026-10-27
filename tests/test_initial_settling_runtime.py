import re, unittest
from native_support import ROOT, run_c
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
static int settings_save_one(const char *key,const void *value,size_t len){writes++;stored=*(const uint8_t*)value;return 0;}
static void led_indicator_show_settling_tick(void){blinks++;}
"""+source+r"""
int main(void){
 assert(initial_settling_remaining_seconds()==3600);
 initial_settling_observe(true,false,false,false);assert(blinks==0 && writes==0);
 for(int i=0;i<=360;i++){clock_ms=i*10000LL;initial_settling_observe(true,true,true,false);}
 assert(initial_settling_remaining_seconds()==0);
 assert(complete && stored==1 && writes==1 && blinks==360);
 int old=blinks;clock_ms+=10000;initial_settling_observe(true,true,true,false);assert(blinks==old);
 assert(!initial_settling_reset() && !complete && stored==0);
 assert(initial_settling_remaining_seconds()==3600);
 initial_settling_observe(true,false,true,true);assert(blinks==old);
 clock_ms+=50000;initial_settling_observe(true,false,true,false);assert(measured_ms==0);
 confirmed=false;measured_ms=3600000;initial_settling_observe(true,false,true,false);assert(!complete);
 confirmed=true;clock_ms+=30000;initial_settling_observe(true,false,true,false);assert(complete);
}
""")
