"""Compile the actual pressure driver against a register-level I2C fake."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class PressureRuntimeTests(unittest.TestCase):
    def test_readiness_recovery_and_coherent_reads(self):
        source = (ROOT / 'application/src/drivers/lps22hh.c').read_text()
        source = re.sub(r'^#include .*$', '', source, flags=re.M)
        prelude = r'''
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <drivers/lps22hh.h>
#include <services/diagnostics.h>
static uint32_t failure_detail;
static void watchdog_feed(void) {}
void diagnostics_record(enum diagnostic_event event,uint32_t detail) {assert(event==DIAG_SENSOR_FAILED);failure_detail=detail;}
#define LOG_MODULE_REGISTER(...)
#define LOG_INF(...) ((void)0)
#define LOG_WRN(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define I2C_ADDR_LPS22HH 0x5d
static uint8_t regs[256];
static bool stuck, recoverable, boot_stuck, reset_stuck;
static int fail_write = -1, fail_read = -1, resets, output_reads, elapsed, id_failures;
static void k_msleep(int ms) {elapsed += ms;}
static bool i2c_bus_is_ready(void) {return true;}
static int i2c_bus_write(uint8_t addr, const uint8_t *b, size_t len) {
 assert(addr == 0x5d && len == 2);
 if (b[0] == fail_write) return -EIO;
 regs[b[0]] = b[1];
 if (b[0] == 0x11 && (b[1] & 4)) {
  resets++;
  if (!reset_stuck) {
   memset(regs, 0, sizeof(regs)); regs[0x0f]=0xb3;
   if(recoverable)stuck=false;
  }
 }
 if (b[0] == 0x11 && (b[1] & 0x80) && !boot_stuck) regs[0x11] &= ~0x80;
 if (b[0] == 0x11 && (b[1] & 1)) regs[0x27] = stuck ? 0 : 3;
 return 0;
}
static int i2c_bus_write_read(uint8_t addr,const uint8_t *w,size_t wn,uint8_t *b,size_t n) {
 assert(addr==0x5d && wn==1);
 if (*w == 0x0f && id_failures>0) {id_failures--;return -EIO;}
 if (*w == fail_read) return -EIO;
 if (*w == 0x2b) {assert(n==2);output_reads++;b[0]=0xc4;b[1]=9;return 0;}
 if (*w == 0x28) {
  assert(n==3 && (output_reads%2)==1); /* Temperature must precede pressure. */
  output_reads++;b[0]=0;b[1]=0x50;b[2]=0x3f;return 0; /* 1013 hPa */
 }
 memcpy(b,regs+*w,n);return 0;
}
'''
        main = r'''
static void fresh(void) {
 memset(regs,0,sizeof(regs));regs[0xf]=0xb3;
 initialized=false;startup_recovery_attempted=false;has_valid_sample=false;
 last_data=(lps22hh_data_t){.pressure=0,.temperature=NAN};
 stuck=recoverable=boot_stuck=reset_stuck=false;
 fail_write=fail_read=-1;resets=output_reads=elapsed=id_failures=0;
}
int main(void) {
 lps22hh_data_t d={.pressure=777,.temperature=22};
 fresh();assert(lps22hh_init()==0);assert(regs[0x0e]==2);assert(lps22hh_read(&d)==0);
 assert(d.pressure==1013 && d.temperature==25 && output_reads==2 && resets==0);
 /* Failure after successful operation must not reset/retry or publish stale data. */
 stuck=true;d.pressure=777;assert(lps22hh_read(&d)==-ETIMEDOUT);
 assert(d.pressure==777 && lps22hh_get_pressure()==1013 && output_reads==2 && resets==0);
 stuck=false;fail_write=0x11;assert(lps22hh_read(&d)==-EIO);assert(output_reads==2);
 fail_write=-1;fail_read=0x27;assert(lps22hh_read(&d)==-EIO);assert(output_reads==2);
 /* A first-sample timeout gets exactly one bounded volatile reset. */
 fresh();stuck=recoverable=true;assert(lps22hh_init()==0);assert(lps22hh_read(&d)==0);
 assert(resets==1 && d.pressure==1013 && elapsed<1500 && regs[0x0e]==2);
 fresh();stuck=true;assert(lps22hh_init()==0);d.pressure=777;
 assert(lps22hh_read(&d)==-ETIMEDOUT && resets==1 && output_reads==0 && d.pressure==777);
 assert(lps22hh_read(&d)==-ETIMEDOUT && resets==1);
 /* Configuration and reset failures cannot be reported as success. */
 /* Power-restoration failure: unreadable identity must not initialize or reset
  * the sensor, and a later successful identity read can retry initialization. */
 fresh();fail_read=0x0f;assert(lps22hh_init()==-EIO && !initialized);
 assert(resets==0 && output_reads==0 && failure_detail==0x50020005);
 fail_read=-1;assert(lps22hh_init()==0 && lps22hh_read(&d)==0);
 assert(d.pressure==1013 && resets==0);
 fresh();id_failures=2;assert(lps22hh_init()==0 && elapsed>=100 && regs[0x0e]==2);
 fresh();id_failures=3;assert(lps22hh_init()==0 && initialized && resets==1 && elapsed<2000);
 assert(failure_detail==0x50020005);
 fresh();id_failures=4;assert(lps22hh_init()==-EIO && !initialized && resets==0);
 fresh();fail_write=0x0e;assert(lps22hh_init()==-EIO && !initialized);
 assert(failure_detail==0x50080005 && resets==1);
 fresh();fail_read=0x0e;assert(lps22hh_init()==-EIO && !initialized);
 assert(failure_detail==0x50090005 && resets==1);
 fresh();boot_stuck=true;assert(lps22hh_init()==-ETIMEDOUT && !initialized);
 assert(failure_detail==(0x50060000U | ETIMEDOUT));
 fresh();fail_write=0x10;assert(lps22hh_init()==-EIO && !initialized);
 fresh();fail_write=0x11;assert(lps22hh_init()==-EIO && !initialized);
 fresh();fail_read=0x11;assert(lps22hh_init()==-EIO && !initialized);
 fresh();reset_stuck=true;assert(lps22hh_soft_reset()==-ETIMEDOUT);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c = Path(tmp) / 'pressure.c'
            exe = Path(tmp) / 'pressure'
            c.write_text(prelude + source + main)
            subprocess.run(['cc', '-std=c11', '-Werror=implicit-function-declaration',
                            '-I', str(ROOT/'application/include'), str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
