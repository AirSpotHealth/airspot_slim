"""Run the real receive accumulator with fragmented and oversized wire frames."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

class ProtocolRxTests(unittest.TestCase):
    def test_fragmentation_and_oversized_recovery(self):
        text=(ROOT/'application/src/services/protocol.c').read_text()
        start=text.index('static void nus_rx_callback(const uint8_t *data, uint16_t len)\n{')
        end=text.index('\n}',start)+2
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)
            (path/'test.c').write_text('''#include <assert.h>
#include <stdint.h>
#include <string.h>
#define LOG_WRN(...)
#define RX_FRAME_MAX_LEN 64
#define CMD_FIRST_BYTE 255
#define CMD_SECOND_BYTE 170
#define K_NO_WAIT 0
static uint8_t rx_frame[64],rx_frame_idx;
static uint32_t rx_generation,session=1;
static uint32_t airspot_nus_generation(void) {return session;}
typedef struct {uint8_t data[64];uint16_t len;uint32_t generation;} protocol_frame_t;
static int protocol_msgq,count;
static protocol_frame_t last;
static int k_msgq_put(int *q, const protocol_frame_t *f,int timeout) {last=*f;count++;return 0;}
''' + text[start:end] + '''
int main(void) {
  uint8_t good[]={255,170,8,0,177};
  for(int split=0;split<=5;split++) {
    count=0;rx_frame_idx=0;
    nus_rx_callback(good,split);
    nus_rx_callback(good+split,5-split);
    assert(count==1 && last.len==5 && memcmp(last.data,good,5)==0);
  }
  count=0;
  uint8_t oversized[]={255,170,5,255};
  nus_rx_callback(oversized,4);
  nus_rx_callback(good,5);
  assert(count==1 && memcmp(last.data,good,5)==0);
  count=0;nus_rx_callback(good,3);session++;
  nus_rx_callback(good,5);
  assert(count==1 && last.generation==session && last.len==5);
  uint8_t joined[10];memcpy(joined,good,5);memcpy(joined+5,good,5);
  count=0;nus_rx_callback(joined,10);assert(count==2);
  uint8_t maximum[64]={255,170,9,59};
  count=0;nus_rx_callback(maximum,64);assert(count==1 && last.len==64);
}
''')
            subprocess.run(['cc','-std=c11','-fsanitize=address,undefined',str(path/'test.c'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)
