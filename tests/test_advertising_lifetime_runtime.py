"""Advertising payload must survive the BLE initialization stack frame."""
import os
import re
import unittest
from unittest.mock import patch
from native_support import ROOT, run_c


class AdvertisingLifetimeTests(unittest.TestCase):
    @patch.dict(os.environ, {'ASAN_OPTIONS': 'detect_stack_use_after_return=1'})
    def test_general_discovery_flags_survive_initialization(self):
        source = (ROOT / 'application/src/services/ble_service.c').read_text()
        declaration = re.search(
            r'static struct bt_data ad\[2\](?:\s*=\s*\{.*?\})?;',
            source, re.S).group(0)
        init = source[source.index('  /* Build advertising data'):]
        init = init[:init.index('  initialized = true;')]
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
struct bt_data { uint8_t type, data_len; const uint8_t *data; };
#define BT_DATA_FLAGS 1
#define BT_DATA_NAME_COMPLETE 9
#define BT_LE_AD_GENERAL 2
#define BT_LE_AD_NO_BREDR 4
#define BT_DATA(t,d,n) { .type=t, .data_len=n, .data=(const uint8_t *)(d) }
#define BT_DATA_BYTES(t,...) BT_DATA(t, ((uint8_t[]){__VA_ARGS__}), sizeof((uint8_t[]){__VA_ARGS__}))
static char device_name[]="AirSpot-123456";
static uint8_t device_name_len=sizeof(device_name)-1;
''' + declaration + '\n__attribute__((noinline)) static void init(void) {\n' + init + r'''
}
int main(void) {
  init();
  for (int scan = 0; scan < 100; scan++) {
    assert(ad[0].type == BT_DATA_FLAGS && ad[0].data_len == 1);
    assert(ad[0].data[0] == 6); /* General, never limited discoverability. */
    assert(ad[1].data_len == strlen(device_name));
    assert(memcmp(ad[1].data, device_name, ad[1].data_len) == 0);
  }
}
''')
