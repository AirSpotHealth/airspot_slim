"""Execute the production fault journal with host queue/storage boundaries."""
import re
import unittest
from native_support import ROOT, BYTEORDER, CRC, run_c
from history_support import STUBS

SHIMS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>
#include <errno.h>
#define ARG_UNUSED(x) (void)(x)
#define __noinit
#define K_NO_WAIT 0
#define K_FOREVER -1
#define RESET_WATCHDOG 1
#define RESET_SOFTWARE 2
#define AIRSPOT_FIRMWARE_VERSION_MAJOR 0
#define AIRSPOT_FIRMWARE_VERSION_MINOR 16
#define AIRSPOT_FIRMWARE_VERSION_PATCH 9
#define SETTINGS_STATIC_HANDLER_DEFINE(...)
#define K_THREAD_DEFINE(...)
struct k_spinlock { int unused; };
typedef int k_spinlock_key_t;
static int k_spin_lock(struct k_spinlock *l) {return 0;}
static void k_spin_unlock(struct k_spinlock *l,int key) {}
#define K_SEM_DEFINE(name, initial, limit) static int name
static int k_sem_take(int *s,int timeout) {return 0;}
static void k_sem_give(int *s) {}
static uint32_t k_uptime_get_32(void) {return 42000;}
struct queue {uint8_t data[16][12]; unsigned begin,count;};
#define K_MSGQ_DEFINE(name, size, count, align) static struct queue name
static jmp_buf drained;
static int k_msgq_put(struct queue *q,const void *p,int timeout) {
    if(q->count==16)return -ENOSPC;
    memcpy(q->data[(q->begin+q->count++)%16],p,12);return 0;
}
static int k_msgq_get(struct queue *q,void *p,int timeout) {
    if(!q->count)longjmp(drained,1);
    memcpy(p,q->data[q->begin++%16],12);q->count--;return 0;
}
typedef int (*settings_read_cb)(void *,void *,size_t);
static int settings_subsys_init(void);
static int settings_load_subtree(const char *name);
static int settings_save_one(const char *name,const void *p,size_t n);
'''
BOUNDARIES = r'''
static uint8_t disk[16][28];
static size_t disk_len[16];
static int save_calls, fail_save, fail_init, short_read;
static int reader(void *arg,void *out,size_t size) {
    memcpy(out,arg,size);return short_read ? (int)size-1 : (int)size;
}
static int settings_subsys_init(void) {return fail_init ? -EIO : 0;}
static int settings_load_subtree(const char *name) {
    for(unsigned i=0;i<16;i++)if(disk_len[i]) {
        char key[3];snprintf(key,sizeof(key),"%02u",i);
        assert(load(key,disk_len[i],reader,disk[i])==0);
    }
    return 0;
}
static int settings_save_one(const char *name,const void *p,size_t n) {
    save_calls++;
    if(fail_save)return -EIO;
    unsigned index; assert(sscanf(name,"diag/%u",&index)==1&&index<16&&n==28);
    memcpy(disk[index],p,n);disk_len[index]=n;return 0;
}
static void boot(uint32_t cause) {
    memset(records,0,sizeof(records));head=seen=0;ready=false;
    memset(&events,0,sizeof(events)); diagnostics_init(cause);
    if(!setjmp(drained))journal_thread(NULL,NULL,NULL);
}
static void reset_fixture(void) {
    memset(disk,0,sizeof(disk));memset(disk_len,0,sizeof(disk_len));
    memset(&retained,0,sizeof(retained));memset(&events,0,sizeof(events));
    memset(records,0,sizeof(records)); head=seen=0;ready=false;
    save_calls=fail_save=fail_init=short_read=0;
}
'''

class DiagnosticsRuntime(unittest.TestCase):
    def execute(self, body):
        code = (ROOT / 'application/src/services/diagnostics.c').read_text()
        code = re.sub(r'^#include <(?:zephyr/|airspot/)[^>]+>\n', '', code, flags=re.M)
        code = code.replace("#include <services/history_events.h>\n", "")
        run_c(SHIMS + STUBS + BYTEORDER + CRC + code + BOUNDARIES + '\nint main(void) {\n' + body + '\nreturn 0;}')

    def test_screen_event_mirrors_keep_full_detail_in_journal(self):
        self.execute(r'''
        reset_fixture();history_count=0;
        diagnostics_record(DIAG_STARTUP_FAILED, (11U<<16)|5);
        diagnostics_record(DIAG_STARTUP_FAILED, (12U<<16)|6);
        assert(history_count==1 && history_records[0].type==RECORD_TYPE_SDK_ERROR && history_records[0].value==5);
        diagnostics_watchdog(9);
        assert(history_count==2 && history_records[1].type==RECORD_TYPE_FAULT_RESET && history_records[1].value==9);
        diagnostics_record(DIAG_OTA_REJECTED,1);
        assert(history_count==3 && history_records[2].type==RECORD_TYPE_DFU_FAIL);
        diagnostics_record(DIAG_STORAGE_FAILED,5);
        assert(history_count==3); /* No recursive history/storage failure loop. */
        ''')

    def test_ring_wrap_persists_order_and_snapshot_consistency(self):
        self.execute(r'''
        reset_fixture();
        for(int i=0;i<20;i++)boot(RESET_SOFTWARE);
        assert(head==20 && save_calls==20);
        uint8_t out[36];
        for(int i=0;i<16;i++) {
            assert(diagnostics_read(i,20,out)==36 && out[1]==0 && out[3]==16);
            assert(sys_get_be32(out+16)==20-i);
            assert(valid(out+8));
        }
        assert(diagnostics_read(16,20,out)==8 && out[1]==1);
        assert(diagnostics_read(0,19,out)==8 && out[1]==3);
        ''')

    def test_bad_crc_truncated_read_wrong_slot_and_schema_are_ignored(self):
        self.execute(r'''
        reset_fixture();boot(0);
        uint8_t good[28];memcpy(good,disk[0],28);
        head=0;memset(records,0,sizeof(records));
        disk[0][16]^=1;assert(load("00",28,reader,disk[0])==0&&head==0);
        assert(load("00",27,reader,good)==0&&head==0);
        short_read=1;assert(load("00",28,reader,good)==0&&head==0);short_read=0;
        assert(load("01",28,reader,good)==0&&head==0);
        assert(load("99",28,reader,good)==0&&head==0);
        uint8_t bad[28];memcpy(bad,good,28);bad[0]=2;
        sys_put_be32(crc32_ieee(bad,24),bad+24);
        assert(load("00",28,reader,bad)==0&&head==0);
        assert(load("00",28,reader,good)==0&&head==1);
        ''')

    def test_write_failure_stops_flash_retries_but_keeps_ram_events(self):
        self.execute(r'''
        reset_fixture();fail_save=1;
        diagnostics_record(DIAG_SENSOR_FAILED,7);
        diagnostics_record(DIAG_SENSOR_FAILED,8); /* bounded once per boot */
        diagnostics_record(DIAG_OTA_REJECTED,1);
        diagnostics_init(0);
        if(!setjmp(drained))journal_thread(NULL,NULL,NULL);
        assert(head==3&&save_calls==1);
        uint8_t out[36];assert(diagnostics_read(2,3,out)==36);
        assert(out[9]==DIAG_SENSOR_FAILED&&sys_get_be32(out+24)==7);
        fail_save=0;boot(0);assert(head==1); /* failed records were never durable */
        ''')

    def test_storage_initialization_failure_remains_readable(self):
        self.execute(r'''
        reset_fixture();fail_init=1;boot(0);
        uint8_t out[36];assert(diagnostics_read(0,0,out)==36);
        assert(head==1&&save_calls==0);
        ''')

    def test_retained_rollback_and_watchdog_evidence_survive_once(self):
        self.execute(r'''
        reset_fixture();diagnostics_reboot_event(DIAG_ROLLBACK_REQUESTED,123);
        boot(RESET_SOFTWARE);uint8_t out[36];
        assert(head==2&&diagnostics_read(0,2,out)==36);
        assert(out[9]==DIAG_ROLLBACK_REQUESTED&&sys_get_be32(out+24)==123);
        boot(RESET_SOFTWARE);assert(head==3); /* retained marker consumed */
        diagnostics_watchdog(5);boot(RESET_WATCHDOG);
        assert(head==5&&diagnostics_read(0,5,out)==36);
        assert(out[9]==DIAG_WATCHDOG&&sys_get_be32(out+24)==5);
        ''')

    def test_invalid_retention_and_cold_boot_do_not_invent_faults(self):
        self.execute(r'''
        reset_fixture();diagnostics_reboot_event(DIAG_WATCHDOG,5);
        retained.inverse^=1;boot(RESET_WATCHDOG);assert(head==1);
        diagnostics_reboot_event(DIAG_WATCHDOG,5);boot(0);assert(head==2);
        diagnostics_reboot_event(DIAG_SENSOR_FAILED,5);boot(RESET_SOFTWARE);assert(head==3);
        ''')

    def test_sensor_evidence_is_bounded_and_survives_reboot(self):
        self.execute(r'''
        reset_fixture();boot(0);
        for(int i=0;i<100;i++) diagnostics_record(DIAG_I2C_FAILED,0x6402fffb);
        diagnostics_record(DIAG_SAMPLE_FAILED,0x4402ffb6);
        diagnostics_record(DIAG_CO2_CONTEXT,0x0084ffff);
        diagnostics_record(DIAG_SENSOR_POWER,3700U<<16);
        diagnostics_record(DIAG_RECOVERY_RESULT,0x44020000);
        if(!setjmp(drained))journal_thread(NULL,NULL,NULL);assert(head==6);
        boot(RESET_SOFTWARE);assert(head==7);
        uint8_t out[36];assert(diagnostics_read(5,7,out)==36);
        assert(out[9]==DIAG_I2C_FAILED && sys_get_be32(out+24)==0x6402fffb);
        ''')

if __name__ == '__main__':
    unittest.main()
