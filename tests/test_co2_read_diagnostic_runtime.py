"""Read-only wire diagnostics distinguish signed under-range data from bus errors."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class Co2ReadDiagnosticTests(unittest.TestCase):
    def test_signed_read_result_and_read_only_contract(self):
        source = (ROOT / 'application/src/services/protocol.c').read_text()
        case = source[source.index('  case CMD_CO2_READ_DIAGNOSTIC:'):source.index('  case CMD_RESET_NATIVE_CALIBRATION:')]
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <services/protocol.h>
#include <services/sensor_manager.h>
static sensor_asc_diagnostic_t snapshot;
static uint8_t reply[28];static int size, reads;
void sensor_manager_get_asc_diagnostic(sensor_asc_diagnostic_t *v) {*v=snapshot;reads++;}
static void sys_put_be16(uint16_t n,uint8_t *p) {p[0]=n>>8;p[1]=n;}
static void sys_put_be32(uint32_t n,uint8_t *p) {p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static void set_frame_checksum(uint8_t *p,int n) {uint8_t sum=0;for(int i=0;i<n-1;i++)sum+=p[i];p[n-1]=sum;}
static void send_frame(uint8_t *p,int n) {size=n;memcpy(reply,p,n);}
static void handle(uint8_t *frame,int len) {switch(frame[2]) {
''' + case + r'''
}}
int main(void) {
 snapshot.read=(sensor_co2_diagnostic_t){.flags=3,.fault=0x44,.error=0,.raw_co2_ppm=-155,.status=0x84,.sequence=0x12345678,.uptime_ms=1000,.temperature_centi=-100,.humidity_centi=6000,.pressure_hpa=1013};
 uint8_t frame[6]={255,170,CMD_CO2_READ_DIAGNOSTIC,0,0,0};handle(frame,5);
 assert(size==28 && reply[3]==23 && reply[4]==1);
 assert(reply[5]==3 && reply[6]==0x44 && reply[7]==0 && reply[8]==0);
 assert(reply[9]==0xff && reply[10]==0x65 && reply[12]==0x84);
 assert(reply[13]==0x12 && reply[16]==0x78 && reply[19]==3 && reply[20]==0xe8);
 assert(reply[21]==0xff && reply[22]==0x9c && reply[23]==0x17 && reply[24]==0x70);
 assert(reply[25]==3 && reply[26]==0xf5);
 uint8_t checksum=0;for(int i=0;i<27;i++)checksum+=reply[i];assert(reply[27]==checksum);
 snapshot.read.error=-5;snapshot.read.flags=0;handle(frame,5);assert(reply[7]==0xff && reply[8]==0xfb && reply[5]==0);
 int prior=reads;size=0;frame[3]=1;handle(frame,6);assert(reads==prior && size==0);
 frame[3]=0;handle(frame,6);assert(reads==prior && size==0);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);(p/'test.c').write_text(code)
            subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I',str(ROOT/'application/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
