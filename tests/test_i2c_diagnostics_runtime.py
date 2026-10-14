import re, unittest
from native_support import ROOT, run_c
class I2cDiagnostics(unittest.TestCase):
    def test_operation_address_and_errno(self):
        source=(ROOT/'application/src/drivers/i2c_bus.c').read_text()
        source=source[source.index('static int record_result'):source.index('void i2c_bus_scan')]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <services/diagnostics.h>
static bool initialized;static void *i2c_dev;static int error,calls;static uint32_t detail;
void diagnostics_record(enum diagnostic_event e,uint32_t d){assert(e==DIAG_I2C_FAILED);detail=d;calls++;}
static int i2c_write(void*d,const uint8_t*p,size_t n,uint8_t a){return error;}
static int i2c_read(void*d,uint8_t*p,size_t n,uint8_t a){return error;}
static int i2c_write_read(void*d,uint8_t a,const uint8_t*p,size_t n,uint8_t*q,size_t m){return error;}
''' + source + r'''
int main(void){
 uint8_t b=0;assert(i2c_bus_write(0x64,&b,1)==-ENODEV);
 assert(detail==((0x64U<<24)|(1U<<16)|(uint16_t)-ENODEV));
 initialized=true;error=-EIO;assert(i2c_bus_read(0x44,&b,1)==-EIO);
 assert(detail==((0x44U<<24)|(2U<<16)|(uint16_t)-EIO));
 assert(i2c_bus_write_read(0x5c,&b,1,&b,1)==-EIO);assert((detail>>16)==0x5c03);
 error=0;assert(!i2c_bus_read(0x64,&b,1));assert(calls==3);
}
''')
