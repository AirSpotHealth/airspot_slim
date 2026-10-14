"""A factory reset must erase native calibration before acknowledging success."""
import unittest
from native_support import ROOT, run_c

class CompleteFactoryReset(unittest.TestCase):
    def test_order_and_failures(self):
        source = (ROOT/'application/src/services/protocol.c').read_text()
        case = source[source.index('  case CMD_FACTORY_RESET:'):source.index('  case CMD_CALIB_START:')]
        worker = source[source.index('void protocol_process_factory_reset(void)'):source.index('/**\n * @brief Handle a complete received frame')]
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <services/protocol.h>
#include <services/diagnostics.h>
#define SYS_REBOOT_COLD 0
typedef int atomic_t;
static atomic_t factory_reset_pending;
static uint32_t factory_reset_generation,command_generation=7,session=7;
static void atomic_set(int *a,int v){*a=v;}
static void atomic_clear(int *a){*a=0;}
static bool atomic_cas(int *a,int old,int v){if(*a!=old)return false;*a=v;return true;}
static uint32_t airspot_nus_generation(void){return session;}
static bool confirmed=true;static int fail,step,reboots,ack,stage;
static bool boot_is_img_confirmed(void){return confirmed;}
static int sensor_manager_factory_erase_calibration(void){assert(++step==1);return fail==1?-EIO:0;}
static int history_events_erase_all(void){assert(++step==2);return fail==2?-EIO:0;}
static int user_settings_factory_reset(void){assert(++step==3);return fail==3?-EIO:0;}
static int user_settings_start_settling(void){assert(++step==4);return fail==4?-EIO:0;}
static int preflight;
static int power_manager_prepare_factory_off(void){return preflight;}
static void power_manager_request_factory_off(void){reboots++;}
static void set_frame_checksum(uint8_t *p,int n){}
static void airspot_nus_send_session(uint8_t *p,int n,uint32_t gen){assert(gen==7 && n==6);ack=p[4];}
static void k_msleep(int ms){}
static void sys_reboot(int type){reboots++;}
void diagnostics_record(enum diagnostic_event e,uint32_t d){assert(e==DIAG_FACTORY_RESET_FAILED);stage=d>>16;}
''' + worker + r'''
static void reset(void){uint8_t frame[5]={255,170,CMD_FACTORY_RESET,0,0};unsigned len=5;switch(CMD_FACTORY_RESET){
''' + case + r''' } assert(step==0); /* BLE handler only queues; main owns erasure. */
 protocol_process_factory_reset(); }
int main(void){
 for(fail=0;fail<=4;fail++){step=reboots=ack=stage=0;reset();
   assert(step==(fail?fail:4));assert(ack==!fail);assert(reboots==!fail);assert(stage==fail);}
 preflight=-EBUSY;fail=0;step=reboots=ack=0;reset();assert(!step&&!reboots&&!ack);preflight=0;
 session=8;step=reboots=ack=0;reset();assert(!step&&!reboots&&!ack);session=7;
 confirmed=false;step=reboots=ack=0;reset();assert(!step&&!reboots&&!ack);
}
''')
