"""Read-only automatic recovery diagnostics validate framing and signed errors."""
import unittest
from native_support import ROOT, run_c

class AutomaticRecoveryProtocolTests(unittest.TestCase):
    def test_read_only_status_and_payload_rejection(self):
        source=(ROOT/'application/src/services/protocol.c').read_text()
        case=source[source.index('  case CMD_SENSOR_RECOVERY_DIAGNOSTIC:'):source.index('  case CMD_GET_ASC_DIAGNOSTIC:')]
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <services/protocol.h>
#include <services/sensor_manager.h>
static int reads,size;static uint8_t reply[18];
void sensor_manager_get_recovery_diagnostic(sensor_recovery_diagnostic_t *d) {
 reads++;*d=(sensor_recovery_diagnostic_t){.stage=3,.fault=0x44,.error=-5,.total_attempts=0x12345678,.consecutive_attempts=4,.retry_in_seconds=300};
}
static void sys_put_be16(uint16_t v,uint8_t *p){p[0]=v>>8;p[1]=v;}
static void sys_put_be32(uint32_t v,uint8_t *p){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static void set_frame_checksum(uint8_t *p,int n){uint8_t sum=0;for(int i=0;i<n-1;i++)sum+=p[i];p[n-1]=sum;}
static void send_frame(uint8_t *p,int n){size=n;memcpy(reply,p,n);}
static void handle(uint8_t *frame,int len){switch(frame[2]){
''' + case + r'''}}
int main(void){
 uint8_t frame[6]={255,170,CMD_SENSOR_RECOVERY_DIAGNOSTIC,0,0,0};handle(frame,5);
 assert(reads==1 && size==18 && reply[3]==13 && reply[4]==1);
 assert(reply[5]==3 && reply[6]==0x44 && reply[7]==255 && reply[8]==251);
 assert(reply[9]==0x12 && reply[12]==0x78 && reply[13]==0 && reply[14]==4);
 assert(reply[15]==1 && reply[16]==44);
 uint8_t sum=0;for(int i=0;i<17;i++)sum+=reply[i];assert(sum==reply[17]);
 frame[3]=1;handle(frame,6);assert(reads==1);
 frame[3]=0;handle(frame,6);assert(reads==1);
}
''')
