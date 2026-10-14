"""Run actual guidance persistence and timing against a simulated device clock/NVS."""
import unittest, tempfile, subprocess
from pathlib import Path
from native_support import ROOT, BYTEORDER, CRC
class SettlingTests(unittest.TestCase):
 def test_lifecycle_and_power_loss(self):
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
#include "'''+str(ROOT/'application/src/services/settling.c')+r'''"
static uint8_t saved[24];static int fail,saves;
int settings_save_one(const char*k,const void*v,size_t n){assert(n==24);saves++;if(fail)return -EIO;memcpy(saved,v,n);return 0;}
static ssize_t rd(void*arg,void*out,size_t n){memcpy(out,arg,n);return n;}
static void tick(unsigned seconds){for(unsigned i=0;i<seconds;i+=5){now+=5000;settling_observe(true);}}
int main(void){
 settling_status_t out;settling_get(&out);assert(out.stage==SETTLING_NONE);
 confirmed=0;assert(settling_prepare_erase(100)==-EBUSY);assert(state.stage==SETTLING_NONE);
 confirmed=1;assert(!settling_prepare_erase(100));assert(state.stage==SETTLING_ERASE_PENDING);
 assert(!load("state",24,rd,saved));assert(state.stage==SETTLING_ERASE_PENDING);
 assert(!settling_erased());assert(state.stage==SETTLING_RUN);
 settling_observe(true);tick(600);assert(state.seconds==600);
 now+=60000;settling_observe(true);assert(state.seconds==0); // disconnected clock gap isn't earned
 tick(300);assert(state.seconds==300);settling_observe(false);assert(!state.seconds&&!state.running);
 settling_observe(true);tick(300);assert(state.seconds==300);
 assert(!load("state",24,rd,saved));assert(state.seconds==0&&!state.running); // reboot restarts continuity
 settling_observe(true);tick(43200);assert(state.stage==SETTLING_FRESH&&state.seconds==43200);
 assert(!load("state",24,rd,saved));assert(state.stage==SETTLING_FRESH); // completed settling survives reboot
 assert(settling_action(1,state.generation+1)==-ESTALE);
 fail=1;assert(settling_action(1,state.generation)==-EIO);assert(state.stage==SETTLING_FRESH);fail=0;
 assert(!settling_action(1,state.generation));settling_observe(true);tick(300);
 assert(settling_action(2,state.generation)==-EAGAIN);
 settling_observe(false);assert(state.stage==SETTLING_FRESH&&!state.fresh_seconds);
 assert(!settling_action(1,state.generation));settling_observe(true);tick(600);
 assert(!settling_action(2,state.generation));assert(state.stage==SETTLING_DONE);
 assert(!load("state",24,rd,saved));assert(state.stage==SETTLING_DONE);
 unsigned gen=state.generation;assert(!settling_prepare_erase(200));assert(state.generation==gen+1);
 assert(!settling_erased());fail=1;settling_observe(true);tick(43200);assert(state.error==-EIO);
 assert(settling_action(1,state.generation)==-EIO);fail=0;now+=30000;settling_observe(true);assert(!state.error);
 saved[4]^=1;assert(load("state",24,rd,saved)==-EIO);assert(state.error==-EIO);
 assert(load("state",1,rd,saved)==-EIO);
}
''')
   subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I',str(p),'-I',str(ROOT/'application/include'),str(p/'t.c'),'-o',str(p/'t')],check=True)
   subprocess.run([str(p/'t')],check=True)
