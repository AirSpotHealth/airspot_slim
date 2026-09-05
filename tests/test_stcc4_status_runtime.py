"""Decode actual STCC4 wire data, including CRC and ASC-paused status."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class Stcc4StatusTests(unittest.TestCase):
    def test_status_and_reset_failures(self):
        source=re.sub(r'^#include .*$', '', (ROOT/'application/src/drivers/stcc4.c').read_text(), flags=re.M)
        prefix=r'''
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <drivers/stcc4.h>
#define LOG_MODULE_REGISTER(...)
#define LOG_ERR(...)
#define LOG_INF(...)
#define LOG_WRN(...)
#define LOG_DBG(...)
#define ARG_UNUSED(x) (void)(x)
#define I2C_ADDR_STCC4 0x64
static int64_t now;
static uint16_t failed_command,last_command;
static uint8_t wire[12];
static int64_t k_uptime_get(void) {return now;}
static void k_msleep(int ms) {now+=ms;}
static bool i2c_bus_is_ready(void) {return true;}
static int i2c_bus_write(uint8_t addr,const uint8_t *p,size_t len) {
  last_command=len>=2?((p[0]<<8)|p[1]):p[0];
  return failed_command && last_command==failed_command ? -EIO:0;
}
static int i2c_bus_read(uint8_t addr,uint8_t *out,size_t len) {memcpy(out,wire,len);return 0;}
static float sht40_get_temperature(void) {return 22;}
static float sht40_get_humidity(void) {return 45;}
'''
        tests=r'''
static void sample(uint16_t status) {
  memset(wire,0,sizeof(wire)); wire[0]=3;wire[1]=32;
  wire[9]=status>>8;wire[10]=status&255;
  for(int i=0;i<12;i+=3)wire[i+2]=stcc4_crc8(wire+i,2);
}
int main(void) {
  assert(stcc4_init()==0);
  assert(stcc4_enable_testing_mode()==0 && last_command==0x3fbc);
  stcc4_data_t d;
  sample(0x0004);assert(stcc4_read(&d)==0 && d.valid && d.co2_ppm==800);
  sample(0x0084);assert(stcc4_read(&d)==0 && d.valid);
  sample(0x0080);assert(stcc4_read(&d)==-EACCES);
  sample(0x0040);assert(stcc4_read(&d)==-EACCES);
  sample(0);assert(stcc4_read(&d)==-EACCES && !d.valid && stcc4_get_co2()==0);
  sample(0x4000);assert(stcc4_read(&d)==-EACCES); /* correct byte/bit required */
  sample(0x0004);wire[11]^=1;assert(stcc4_read(&d)==-EIO);
  failed_command=0x3fbc;assert(stcc4_enable_testing_mode()==-EIO);
  failed_command=0x3f86;assert(stcc4_reset()==-EIO && last_command==0x3f86);
  failed_command=0x3650;assert(stcc4_reset()==-EIO && last_command==0x3650);
  failed_command=0;assert(stcc4_reset()==0 && !stcc4_data_ready());
}
'''
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);(p/'test.c').write_text(prefix+source+tests)
            subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I',str(ROOT/'application/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
