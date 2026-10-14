"""History values read from flash must match the Slim app's LE wire decoder."""
import unittest
from native_support import ROOT, run_c


class HistoryEncodingTests(unittest.TestCase):
    def test_flash_record_serializes_as_little_endian_ppm(self):
        source = (ROOT / 'application/src/services/protocol.c').read_text()
        function = source[source.index('void protocol_send_history_data('):
                          source.index('void protocol_send_erase_done(')]
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define LOG_DBG(...)
#define CMD_FIRST_BYTE 0xff
#define CMD_SECOND_BYTE 0xaa
#define CMD_HISTORY_REQUEST 0x0c
#define SWAP_ENDIAN16(v) ((uint16_t)(((v) >> 8) | ((v) << 8)))
typedef struct __attribute__((packed)) {
 uint32_t timestamp; uint16_t value; uint8_t type, reserved;
} record_t;
static uint8_t sent[135];
static void set_frame_checksum(uint8_t *b, unsigned n) {
 uint8_t sum=0; for(unsigned i=0;i<n-1;i++) sum+=b[i]; b[n-1]=sum;
}
static int airspot_nus_send_session(uint8_t *b, unsigned n, uint32_t session) {
 assert(n==sizeof(sent) && session==42); memcpy(sent,b,n); return 0;
}
''' + function + r'''
int main(void) {
 /* Captured flash layout: LE timestamp, BE CO2. 491 ppm is 01 eb. */
 uint8_t page[128]; memset(page,0xff,sizeof(page));
 const uint8_t record[]={0x60,0x01,0,0,0x01,0xeb,0,0};
 memcpy(page,record,sizeof(record));
 protocol_send_history_data(page,1,6,42);
 assert(sent[0]==0xff && sent[1]==0xaa && sent[2]==0x0c && sent[3]==128);
 assert(sent[4]==0 && sent[5]==0 && sent[6]==1 && sent[7]==0x60);
 assert(((uint16_t)sent[9]<<8 | sent[8])==491);
 assert(sent[10]==0 && sent[11]==0);
 assert(sent[12]==0xff && sent[16]==0xff && sent[17]==0xff && sent[19]==0);
 assert(sent[132]==0 && sent[133]==6);
 uint8_t sum=0; for(unsigned i=0;i<134;i++) sum+=sent[i]; assert(sum==sent[134]);
 assert(memcmp(page,record,sizeof(record))==0);
}
''')
