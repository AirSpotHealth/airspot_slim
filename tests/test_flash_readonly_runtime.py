"""Verify the forensic candidate cannot program or erase the external flash."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

class FlashReadonlyTests(unittest.TestCase):
    def test_mutations_never_reach_spi(self):
        source=(ROOT/'application/src/drivers/spi_flash.c').read_text()
        def function(name):
            start=re.search(r'(?:static )?int '+name+r'\(', source).start()
            opening=source.index('{',start); depth=1;end=opening+1
            while depth:
                depth += (source[end]=='{')-(source[end]=='}');end+=1
            return source[start:end]
        code=r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define CONFIG_AIRSPOT_FORENSIC_HISTORY 1
#define SPI_FLASH_SIZE (2*1024*1024)
#define SPI_FLASH_PAGE_SIZE 256
#define LOG_WRN(...)
static bool initialized=true;
static int spi_spec;
struct spi_buf {void *buf;size_t len;};
struct spi_buf_set {const struct spi_buf *buffers;size_t count;};
static int spi_flash_transact(const uint8_t *a,size_t b,uint8_t *c,size_t d) {assert(0);return 0;}
static int spi_flash_wait_ready(uint32_t t) {assert(0);return 0;}
static int spi_write_dt(const int *s,const struct spi_buf_set *b) {assert(0);return 0;}
'''
        for line in source.splitlines():
            if re.match(r'#define (CMD_|STATUS_|PAGE_PROGRAM_TIME|SECTOR_ERASE_TIME|BLOCK_ERASE_TIME|CHIP_ERASE_TIME)',line):code+=line+'\n'
        for name in ['spi_flash_write_enable','spi_flash_write','spi_flash_erase_sector','spi_flash_erase_block','spi_flash_erase_chip']:
            code+=function(name)+'\n'
        code+='''int main(void) { uint8_t b=0; assert(spi_flash_write(0x65000,&b,1)==-EROFS); assert(spi_flash_erase_sector(0x65000)==-EROFS); assert(spi_flash_erase_block(0x60000)==-EROFS); assert(spi_flash_erase_chip()==-EROFS); return 0; }'''
        with tempfile.TemporaryDirectory() as tmp:
            c=Path(tmp)/'test.c';exe=Path(tmp)/'test';c.write_text(code)
            subprocess.run(['cc',str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
