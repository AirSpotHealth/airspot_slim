"""Test the explicit bench module's healthy window and persistent read errors."""
import re
import unittest
from native_support import ROOT, run_c

class SensorFaultInjectionTests(unittest.TestCase):
    def test_fault_threshold_and_reboot(self):
        source = (ROOT/'tests/hardware/sensor_fault/sensor_fault.c').read_text()
        source = re.sub(r'^#include .*$', '', source, flags=re.M)
        run_c(r'''
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <drivers/stcc4.h>
#define LOG_MODULE_REGISTER(...)
#define LOG_WRN(...)
static int64_t now;
static int reads;
static int64_t k_uptime_get(void) {return now;}
int __real_stcc4_read(stcc4_data_t *d) {reads++;d->valid=true;return 0;}
''' + source + r'''
int main(void) {
 stcc4_data_t d={0};
 now=700000;assert(__wrap_stcc4_read(&d)==0 && d.valid && reads==1);
 now=760000;assert(__wrap_stcc4_read(&d)==-EIO && !d.valid && reads==1);
 now=3600000;assert(__wrap_stcc4_read(&d)==-EIO && !d.valid && reads==1);
 assert(__wrap_stcc4_read(NULL)==-EIO);
 window_started=false;now=800000;assert(__wrap_stcc4_read(&d)==0 && d.valid && reads==2);
}
''')
