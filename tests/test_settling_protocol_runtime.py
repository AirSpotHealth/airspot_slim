"""Exercise real handlers: lengths, guarded actions, errors and reboot gating."""
import unittest
from native_support import ROOT, BYTEORDER, run_c

class SettlingProtocolTests(unittest.TestCase):
    def test_protocol(self):
        source = (ROOT/'application/src/services/protocol.c').read_text()
        handlers = source[source.index('  case CMD_SETTLING:'):source.index('  case CMD_NATIVE_ASC:')]
        run_c(r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <services/protocol.h>
#include <services/settling.h>
''' + BYTEORDER + r'''
static int actions,resets,reboots,size,save_error;
static uint8_t reply[27];
static void k_msleep(int ms) {}
#define SYS_REBOOT_COLD 0
static void sys_reboot(int mode) {reboots++;}
static int user_settings_reset_preferences(void) {resets++;return save_error;}
int settling_action(uint8_t action,uint32_t gen) {actions++;assert(action==1&&gen==0x12345678);return -ESTALE;}
void settling_get(settling_status_t *s) {*s=(settling_status_t){.stage=2,.generation=0x12345678,.reset_time=12,.seconds=43200,.error=save_error};}
static void set_frame_checksum(uint8_t *p,int n){uint8_t sum=0;for(int i=0;i<n-1;i++)sum+=p[i];p[n-1]=sum;}
static void send_frame(uint8_t *p,int n){size=n;memcpy(reply,p,n);}
static void handle(uint8_t *frame,int len){switch(frame[2]){
''' + handlers + r'''}}
int main(void) {
 uint8_t f[10]={255,170,CMD_SETTLING,0};handle(f,5);
 assert(size==27&&reply[3]==22&&reply[4]==1&&reply[5]==2&&reply[7]==0);
 assert(sys_get_be32(reply+10)==0x12345678&&sys_get_be32(reply+14)==12&&sys_get_be32(reply+18)==43200);
 uint8_t sum=0;for(int i=0;i<26;i++)sum+=reply[i];assert(sum==reply[26]);
 f[3]=5;f[4]=1;sys_put_be32(0x12345678,f+5);handle(f,10);
 assert(actions==1&&(int16_t)((reply[8]<<8)|reply[9])==-ESTALE);
 handle(f,9);assert(actions==1&&(int16_t)((reply[8]<<8)|reply[9])==-EINVAL);
 f[3]=0;save_error=-EIO;handle(f,5);assert((int16_t)((reply[8]<<8)|reply[9])==-EIO);
 f[2]=CMD_RESET_PREFERENCES;handle(f,5);assert(resets==1&&!reboots&&size==7&&(int16_t)((reply[4]<<8)|reply[5])==-EIO);
 save_error=0;f[3]=1;handle(f,6);assert(resets==1&&!reboots);
 f[3]=0;handle(f,5);assert(resets==2&&reboots==1&&reply[4]==0&&reply[5]==0);
}
''')
