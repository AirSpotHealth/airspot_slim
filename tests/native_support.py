"""Small host boundary shims; the tests compile production C implementations."""
from pathlib import Path
import subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
BYTEORDER = r"""
#include <stdint.h>
static uint32_t sys_get_be32(const uint8_t *p) {return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void sys_put_be32(uint32_t v,uint8_t *p) {for(int i=0;i<4;i++)p[i]=v>>(24-8*i);}
static void sys_put_be16(uint16_t v,uint8_t *p) {p[0]=v>>8;p[1]=v;}
static uint32_t sys_get_le32(const uint8_t *p) {return (uint32_t)p[3]<<24|(uint32_t)p[2]<<16|(uint32_t)p[1]<<8|p[0];}
static void sys_put_le32(uint32_t v,uint8_t *p) {for(int i=0;i<4;i++)p[i]=v>>(8*i);}
static uint16_t sys_get_le16(const uint8_t *p) {return (uint16_t)p[1]<<8|p[0];}
static void sys_put_le16(uint16_t v,uint8_t *p) {p[0]=v;p[1]=v>>8;}
"""
CRC = r"""
#include <stdint.h>
#include <stddef.h>
static uint32_t crc32_ieee(const uint8_t *p,size_t n) {uint32_t c=~0u;while(n--){c^=*p++;for(int i=0;i<8;i++)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
"""
def run_c(source):
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory)
        (root/'test.c').write_text(source)
        subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I',str(ROOT/'application/include'),str(root/'test.c'),'-o',str(root/'test')],check=True)
        subprocess.run([str(root/'test')],check=True)
