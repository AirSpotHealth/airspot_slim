"""Execute main's real saved-mode startup block without a Bluetooth command."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class SensorBootTests(unittest.TestCase):
 def test_saved_modes_are_applied_at_boot(self):
  s=(ROOT/'application/src/main.c').read_text()
  a=s.index('  uint8_t power_mode = user_settings_get_power_mode();')
  b=s.index('  /*\n   * ========================================\n   * Initialization Complete',a)
  code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define LOG_INF(...)
#define LOG_ERR(...)
static uint8_t saved,applied;static uint32_t interval;static int measurements;
static uint8_t user_settings_get_power_mode(void){return saved;}
static int sensor_manager_measure_now(void){measurements++;return 0;}
static int sensor_manager_start(uint32_t ms){interval=ms;return 0;}
static int sensor_manager_set_power_mode(uint8_t mode){applied=mode;return 0;}
/* Startup-result semantics are exercised with real code in test_sensor_callback. */
static int initial_measurement_result(int rc){return rc;}
static bool startup_check(uint16_t subsystem,int rc){return rc==0;}
static void boot(void){int ret;bool startup_healthy=true;
'''+s[a:b]+r'''
assert(startup_healthy);}
int main(void){
 uint32_t intervals[]={0,180000,60000,5000};
 for(saved=0;saved<4;saved++){
  applied=255;interval=0;measurements=0;boot();
  assert(applied==saved && interval==intervals[saved]);
  assert(measurements==(saved==0));
 }
}
'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'t.c').write_text(code)
   subprocess.run(['cc','-std=c11','-fsanitize=address,undefined',str(p/'t.c'),'-o',str(p/'t')],check=True)
   subprocess.run([str(p/'t')],check=True)
