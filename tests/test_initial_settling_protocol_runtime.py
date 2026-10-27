import unittest
from native_support import ROOT, BYTEORDER, run_c

class InitialSettlingProtocolTests(unittest.TestCase):
 def test_read_only_status(self):
  source=(ROOT/'application/src/services/protocol.c').read_text()
  handler=source[source.index('  case CMD_INITIAL_SETTLING:'):source.index('  case CMD_SETTLING:')]
  run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <services/protocol.h>
''' + BYTEORDER + r'''
static int sent;
static bool active=true;
static uint32_t seconds=3540;
static uint8_t reply[11];
static void sensor_manager_get_initial_settling(bool *a,uint32_t *s){*a=active;*s=seconds;}
static void set_frame_checksum(uint8_t *p,int n){uint8_t sum=0;for(int i=0;i<n-1;i++)sum+=p[i];p[n-1]=sum;}
static void send_frame(uint8_t *p,int n){assert(n==11);sent++;memcpy(reply,p,n);}
static void handle(uint8_t *frame,int len){switch(frame[2]){
''' + handler + r'''}}
int main(void){
 uint8_t f[]={255,170,CMD_INITIAL_SETTLING,0,231};handle(f,5);
 assert(sent==1&&reply[3]==6&&reply[4]==1&&reply[5]==1&&sys_get_be32(reply+6)==3540);
 uint8_t sum=0;for(int i=0;i<10;i++)sum+=reply[i];assert(sum==reply[10]);
 f[3]=1;handle(f,5);assert(sent==1);
 f[3]=0;handle(f,4);assert(sent==1);
 active=false;seconds=0;handle(f,5);assert(sent==2&&reply[5]==0&&sys_get_be32(reply+6)==0);
}
''')
