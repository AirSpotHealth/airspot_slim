"""Execute the actual bounded warm-reset conditioning recovery path."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class ConditioningRecoveryTests(unittest.TestCase):
 def test_success_busy_recovery_and_permanent_failure(self):
  source=(ROOT/'application/src/services/sensor_manager.c').read_text()
  begin=source.index('  LOG_INF("STCC4 conditioning (~22s, one time)...");')
  end=source.index('\n  }\n#endif',begin)
  code='''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#define LOG_INF(...)
#define LOG_WRN(...)
#define LOG_ERR(...)
#define LED_COLOR_WHITE 0
#define SENSOR_FAULT_CO2 0x44
static int last_fault;
static void set_sensor_fault(int code) {last_fault=code;}
static int failures,calls,elapsed,feeds,stops,wakes,sensor_stops,stop_error;
static int stcc4_exit_sleep_mode(void){wakes++;return 0;}
static int stcc4_stop(void){sensor_stops++;return stop_error;}
static bool boot_attempt=true, stcc4_conditioned;
static void conditioning_keepalive(void){feeds++;}
static void conditioning_completed(void){}
static void k_msleep(int ms){assert(ms<=2000);elapsed+=ms;}
static void led_indicator_start_flash_pulse(int a,int b,int c){}
static void led_indicator_stop(void){stops++;}
static int stcc4_perform_conditioning(void (*feed)(void)){
 calls++;return calls<=failures ? -EIO:0;
}
static int boot(void){int ret;
'''+source[begin:end]+'''
return 0;}
int main(void){
 for(failures=0;failures<3;failures++){
  calls=elapsed=feeds=stops=wakes=sensor_stops=0;int rc=boot();
  assert(calls==(failures?2:1));assert(stops==1);
  assert(wakes==(failures?1:0) && sensor_stops==wakes);
  if(failures==0){assert(rc==0 && elapsed==2000);}
  if(failures==1){assert(rc==0 && elapsed==26050 && feeds>=26);}
  if(failures==2){assert(rc==-EIO && elapsed==24050 && feeds>=24 && last_fault==SENSOR_FAULT_CO2);}
 }
 boot_attempt=true;failures=1;calls=elapsed=feeds=stops=wakes=sensor_stops=0;stop_error=-EIO;
 assert(boot()==0 && calls==2 && sensor_stops==1 && elapsed==27250);
 stop_error=0;
 boot_attempt=false;failures=1;calls=elapsed=feeds=stops=0;
 assert(boot()==-EIO && calls==1 && elapsed==0 && stops==1);
}
'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.c').write_text(code)
   subprocess.run(['cc','-std=c11','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
