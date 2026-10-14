"""Run the production event queue with flash failures and warm-reset evidence."""
import re
import unittest
from native_support import ROOT, run_c
from history_support import declarations

SHIMS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <services/diagnostics.h>
#define K_MUTEX_DEFINE(n) static int n
#define K_FOREVER -1
#define __noinit
#define LOG_MODULE_REGISTER(...)
#define LOG_ERR(...)
#define RESET_PIN 1
#define RESET_WATCHDOG 2
#define RESET_SOFTWARE 4
#define RESET_CPU_LOCKUP 8
#define RESET_LOW_POWER_WAKE 16
static void k_mutex_lock(int *m,int timeout) {assert(!*m);*m=1;}
static void k_mutex_unlock(int *m) {assert(*m);*m=0;}
static int64_t uptime;
static uint32_t clock_time;
static bool synced;
static int64_t k_uptime_get(void) {return uptime;}
static bool user_settings_is_time_set(void) {return synced;}
static uint32_t user_settings_get_time_now(void) {return clock_time;}
static void user_settings_set_timebase(uint32_t t) {clock_time=t;synced=true;}
static int storage_error, stored_count, diag_count;
static struct {uint32_t timestamp;uint16_t value;record_type_t type;} stored[256];
void diagnostics_record(enum diagnostic_event e,uint32_t detail) {
  assert(e==DIAG_STORAGE_FAILED);diag_count++;
}
static int data_log_store_record(uint32_t t,uint16_t v,record_type_t type) {
  if(storage_error)return storage_error;
  stored[stored_count].timestamp=t;stored[stored_count].value=v;
  stored[stored_count++].type=type;return 0;
}
static int data_log_erase_all(void) {if(storage_error)return storage_error;stored_count=0;return 0;}
'''

class HistoryEventsTests(unittest.TestCase):
    def execute(self, body):
        code=(ROOT/'application/src/services/history_events.c').read_text()
        code=re.sub(r'^#include .*$', '', code, flags=re.M)
        run_c(declarations()+SHIMS+code+'\nint main(void) {\n'+body+'\n}')

    def test_order_first_sync_and_retry(self):
        self.execute(r'''
        history_events_boot(RESET_SOFTWARE);
        uptime=350000;
        assert(history_event_record(500,RECORD_TYPE_CO2)==0);
        history_time_sync(1789600000);
        struct history_event group[]={{1,RECORD_TYPE_MANUAL_CALIB_START},
          {(uint16_t)-374,RECORD_TYPE_CALIBRATION_CORRECTION_OLD},
          {426,RECORD_TYPE_CALIB_TARGET}};
        assert(history_event_batch(group,3)==0);
        history_time_sync(1789600010); /* Only one first-sync marker per boot. */
        assert(history_event_record(600,RECORD_TYPE_CO2)==0);
        storage_error=-EIO;assert(history_events_process()==-EIO);
        assert(count==7 && stored_count==0 && diag_count==1);
        storage_error=0;history_events_flush();assert(count==0 && stored_count==7);
        assert(stored[0].type==RECORD_TYPE_RESET_REASON && stored[0].value==3 && stored[0].timestamp==1);
        assert(stored[1].value==500 && stored[1].timestamp==350);
        assert(stored[2].type==RECORD_TYPE_TIME_SET && stored[2].value==350 && stored[2].timestamp==1789600000);
        for(int i=0;i<3;i++)assert(stored[3+i].type==group[i].type && stored[3+i].value==group[i].value);
        assert(stored[6].value==600 && stored[6].timestamp==1789600010);
        assert(history_events_process()==0);
        ''')

    def test_capacity_event_reserve_atomic_group_and_ring_wrap(self):
        self.execute(r'''
        for(int i=0;i<56;i++)assert(history_event_record(i,RECORD_TYPE_CO2)==0);
        assert(history_event_record(100,RECORD_TYPE_CO2)==-ENOSPC && count==56);
        for(int i=0;i<6;i++)assert(history_event_record(i,RECORD_TYPE_SENSOR_ERROR)==0);
        struct history_event group[]={{1,RECORD_TYPE_MANUAL_CALIB_START},
            {2,RECORD_TYPE_CALIBRATION_CORRECTION_OLD},{426,RECORD_TYPE_CALIB_TARGET}};
        assert(history_event_batch(group,3)==-ENOSPC && count==62);
        assert(history_event_record(1,RECORD_TYPE_SENSOR_AUTO_CALIBRATION)==0);
        assert(history_event_record(0,RECORD_TYPE_SENSOR_AUTO_CALIBRATION)==0);
        assert(history_event_record(0,RECORD_TYPE_DFU)==-ENOSPC && count==64);
        history_events_flush();assert(stored_count==64 && count==0);
        for(int i=0;i<56;i++)assert(stored[i].value==i && stored[i].type==RECORD_TYPE_CO2);
        for(int i=0;i<100;i++) {assert(history_event_record(i,RECORD_TYPE_CO2)==0);assert(history_events_process()==1);}
        assert(stored_count==164);
        for(int i=0;i<100;i++)assert(stored[64+i].value==i);
        assert(history_event_batch(NULL,1)==-EINVAL);
        ''')

    def test_fault_retention_requires_valid_warm_reset_and_successful_write(self):
        self.execute(r'''
        history_retain_fault(42);history_events_boot(RESET_SOFTWARE);
        assert(count==2);assert(history_events_process()==1);
        storage_error=-EIO;assert(history_events_process()==-EIO && fault.magic==FAULT_MAGIC);
        storage_error=0;assert(history_events_process()==1 && fault.magic==0);
        assert(stored[1].type==RECORD_TYPE_FAULT_RESET && stored[1].value==42);
        history_retain_fault(43);history_events_boot(0);history_events_flush();
        assert(stored_count==3 && fault.magic==0); /* Power-on rejects stale RAM. */
        history_retain_fault(44);fault.inverse^=1;
        history_events_boot(RESET_WATCHDOG);history_events_flush();
        assert(stored_count==4 && stored[3].value==2 && fault.magic==0);
        history_events_boot(RESET_PIN);history_events_flush();assert(stored[4].value==1);
        history_events_boot(RESET_CPU_LOCKUP);history_events_flush();assert(stored[5].value==4);
        ''')

    def test_erase_does_not_replay_old_queued_records(self):
        self.execute(r'''
        assert(history_event_record(500,RECORD_TYPE_CO2)==0);
        storage_error=-EIO;assert(history_events_erase_all()==-EIO && count==1);
        storage_error=0;assert(history_events_erase_all()==0 && count==0);
        assert(history_event_record(600,RECORD_TYPE_CO2)==0);
        history_events_flush();assert(stored_count==1 && stored[0].value==600);
        ''')
