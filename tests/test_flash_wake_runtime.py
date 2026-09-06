"""Exercise the real wake helper with a flash that needs 1200 us to resume."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

class FlashWakeTests(unittest.TestCase):
    def test_waits_before_caller_can_access_flash(self):
        source=(ROOT/'application/src/drivers/spi_flash.c').read_text()
        start=source.index('int spi_flash_power_up(void)');end=source.index('\n}',start)+2
        code=r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#define CMD_RELEASE_PD 0xab
static unsigned now,ready_at;
static int failure;
static int spi_flash_transact(const uint8_t *tx,size_t n,uint8_t *rx,size_t rn) {
 assert(n==1 && *tx==0xab && !rx && !rn);
 ready_at=now+1200;return failure;
}
static void k_usleep(unsigned us) {now+=us;}
'''+source[start:end]+r'''
int main(void) {
 assert(spi_flash_power_up()==0);assert(now>=ready_at);
 now=0;failure=-EIO;assert(spi_flash_power_up()==-EIO);assert(now==0);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c=Path(tmp)/'test.c';exe=Path(tmp)/'test';c.write_text(code)
            subprocess.run(['cc',str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
