"""Run actual guidance persistence and timing against a simulated device clock/NVS."""
import unittest, tempfile, subprocess
from pathlib import Path
from native_support import ROOT, BYTEORDER, CRC
class ConditioningPolicyTests(unittest.TestCase):
 def test_clock_gaps_persistence_and_power_loss(self):
  with tempfile.TemporaryDirectory() as td:
   p=Path(td)
   headers={
    'kernel.h': '#include <stdint.h>\n#define K_MUTEX_DEFINE(x) int x\n#define K_FOREVER 0\nstatic int64_t now;\nstatic int64_t k_uptime_get(void){return now;}\nstatic void k_mutex_lock(int*m,int t){}\nstatic void k_mutex_unlock(int*m){}\n',
    'sys/byteorder.h': BYTEORDER, 'sys/crc.h':CRC,
    'dfu/mcuboot.h':'static int confirmed=1;static int boot_is_img_confirmed(void){return confirmed;}\n',
    'settings/settings.h': '#include <stddef.h>\n#include <sys/types.h>\ntypedef ssize_t (*settings_read_cb)(void*,void*,size_t);\nint settings_save_one(const char*,const void*,size_t);\n#define SETTINGS_STATIC_HANDLER_DEFINE(...)\n'}
   for n,t in headers.items():
    f=p/'zephyr'/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(t)
   (p/'t.c').write_text(r'''
#include <assert.h>
#include <string.h>
#include "'''+str(ROOT/'application/src/services/conditioning.c')+r'''"
static uint8_t saved[12];static int fail,saves;
int settings_save_one(const char*k,const void*v,size_t n){assert(n==12);saves++;if(fail)return -EIO;memcpy(saved,v,n);return 0;}
static ssize_t rd(void*arg,void*out,size_t n){memcpy(out,arg,n);return n;}
static void boot(uint32_t stamp){
 saved_time=stamp;anchor_time=0;anchor_ms=resume_ms=0;
 last_sample_ms=conditioned_ms=-1;next_checkpoint=0;begun=waiting=needed=false;now=1000;
 assert(!conditioning_begin());assert(saved_time==stamp);
}
int main(void){
 const uint32_t t=1800000000;
 boot(0);assert(conditioning_due(false));conditioning_completed();
 assert(!conditioning_due(false));conditioning_observe();
 assert(!conditioning_shutdown());assert(sys_get_be32(saved+4)==0); // no app time
 conditioning_time_sync(t);assert(!conditioning_due(false));
 now+=10000;conditioning_observe();assert(!conditioning_shutdown());
 assert(sys_get_be32(saved+4)==t+10);
 assert(!load("last",12,rd,saved));assert(saved_time==t+10);
 // Prior timestamp survives boot; awaiting phone time does not erase it.
 boot(t);assert(waiting&&!conditioning_due(false));
 now+=7200000;conditioning_observe();conditioning_time_sync(t+7200+10800);
 assert(!waiting&&!conditioning_due(false)); // exactly 3h at resume: skip, despite 2h sync delay
 conditioning_time_sync(t+7200+20000);assert(!conditioning_due(false)); // duplicate never reruns
 boot(t);now+=7200000;conditioning_observe();conditioning_time_sync(t+7200+10801);
 assert(conditioning_due(false));conditioning_completed();assert(!conditioning_due(false));
 conditioning_time_sync(t+7200+20000);assert(!conditioning_due(false));
 // Invalid or backwards clocks neither trigger nor replace the old reference.
 boot(t);conditioning_time_sync(0);assert(waiting&&!anchor_time);
 conditioning_time_sync(t-1);assert(waiting&&!anchor_time);
 now+=14400000;assert(!conditioning_due(true)); // still waiting, not timeout-triggered
 conditioning_time_sync(t+14400+1);assert(!waiting&&!conditioning_due(false));
 // Live single-shot idle uses monotonic time, continuous operation never does.
 conditioning_observe();now+=10800000;assert(!conditioning_due(true));
 now+=1;assert(conditioning_due(true));assert(!conditioning_due(false));
 conditioning_completed();assert(!conditioning_due(true));
 // A normal reboot retains the minute checkpoint without requiring clean shutdown.
 boot(t);conditioning_time_sync(t+30);conditioning_observe();
 assert(saved_time==t+30);int count=saves;
 now+=10000;conditioning_observe();assert(saves==count);
 now+=50000;conditioning_observe();assert(saved_time==t+90 && saves==count+1);
 uint32_t recent=saved_time;boot(recent);assert(!conditioning_due(false));
 conditioning_time_sync(recent+30);assert(!waiting&&!conditioning_due(false));
 // Storage failure preserves the older reference and never prevents startup.
 fail=1;conditioning_observe();assert(saved_time==recent);
 now+=60000;fail=0;conditioning_observe();assert(saved_time==recent+90);
 // Shutdown while the clock is unknown must not destroy the prior reference.
 boot(t);count=saves;assert(!conditioning_shutdown());assert(saved_time==t && saves==count);

 // Corrupt/old timestamp is treated as missing, never used for arithmetic.
 saved[4]^=1;assert(load("last",12,rd,saved)==0);assert(saved_time==0);
 assert(load("last",1,rd,saved)==0);
}
''')
   subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I',str(p),'-I',str(ROOT/'application/include'),str(p/'t.c'),'-o',str(p/'t')],check=True)
   subprocess.run([str(p/'t')],check=True)
