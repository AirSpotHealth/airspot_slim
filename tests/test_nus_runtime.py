"""Execute the real NUS implementation with disconnect injected during notify."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

class NusRuntimeTests(unittest.TestCase):
    def test_disconnect_lifetime_and_gatt_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shared = '''#ifndef FAKE_ZEPHYR_H
#define FAKE_ZEPHYR_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <errno.h>
#define ARG_UNUSED(x) (void)(x)
#define BT_GATT_CCC_NOTIFY 1
#define BT_GATT_WRITE_FLAG_PREPARE 1
#define BT_ATT_ERR_INVALID_OFFSET 7
#define BT_ATT_ERR_NOT_SUPPORTED 6
#define BT_GATT_ERR(x) (-(x))
#define BT_GATT_SERVICE_DEFINE(name,...) struct {struct bt_gatt_attr attrs[5];} name
struct bt_gatt_attr {int unused;};
struct bt_conn {int refs;};
struct k_spinlock {int unused;};
typedef int k_spinlock_key_t;
static int k_spin_lock(struct k_spinlock *l) {return 0;}
static void k_spin_unlock(struct k_spinlock *l,int k) {}
static struct bt_conn *bt_conn_ref(struct bt_conn *c) {c->refs++; return c;}
static void bt_conn_unref(struct bt_conn *c) {c->refs--;}
int bt_gatt_notify(struct bt_conn *,const struct bt_gatt_attr *,const void *,uint16_t);
#endif
'''
            for name in ['kernel.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h','bluetooth/uuid.h']:
                p=root/'zephyr'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(shared)
            p=root/'zephyr/logging/log.h';p.parent.mkdir(parents=True,exist_ok=True)
            p.write_text('#define LOG_MODULE_REGISTER(...)\n#define LOG_INF(...)\n#define LOG_WRN(...)\n#define LOG_ERR(...)\n#define LOG_DBG(...)\n')
            source=root/'test.c'
            source.write_text('#include <assert.h>\n#include "'+str(ROOT/'application/src/services/airspot_nus.c')+'"\n'+'''
static int received;
static void receive(const uint8_t *data,uint16_t len) {received++;}
int bt_gatt_notify(struct bt_conn *c,const struct bt_gatt_attr *a,const void *b,uint16_t n) {
  assert(c->refs==3);
  airspot_nus_set_conn(NULL);
  assert(c->refs==2); /* sender retains ownership after disconnect */
  return -ENOTCONN;
}
int main(void) {
  struct bt_conn c={.refs=1};
  uint8_t value=1;
  airspot_nus_init(receive);
  airspot_nus_set_conn(&c);
  assert(c.refs==2);
  assert(airspot_nus_send_session(&value,1,airspot_nus_generation()+1)==-ENOTCONN);
  tx_ccc_changed(NULL,BT_GATT_CCC_NOTIFY);
  assert(airspot_nus_is_ready());
  assert(airspot_nus_send(&value,1)==-ENOTCONN);
  assert(c.refs==1 && !airspot_nus_is_ready());
  assert(rx_write_cb(&c,NULL,&value,1,1,0)<0);
  assert(rx_write_cb(&c,NULL,&value,1,0,BT_GATT_WRITE_FLAG_PREPARE)<0);
  assert(received==0);
  assert(rx_write_cb(&c,NULL,&value,1,0,0)==1 && received==1);
  airspot_nus_set_conn(&c);
  airspot_nus_set_conn(&c);
  assert(c.refs==2); /* replacement cannot leak references */
  airspot_nus_set_conn(NULL);
  assert(c.refs==1);
}
''')
            subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-I',str(root),'-I',str(ROOT/'application/include'),str(source),'-o',str(root/'test')],check=True)
            subprocess.run([str(root/'test')],check=True)
