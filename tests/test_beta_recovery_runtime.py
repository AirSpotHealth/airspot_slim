"""Fault injection for real OTA authorization and worker-deadline code."""
import re
import unittest
from native_support import ROOT, run_c
from history_support import STUBS

def implementation(name):
    return re.sub(r'^#include .*$', '', (ROOT/'application/src/services'/name).read_text(), flags=re.M)

class RecoveryTests(unittest.TestCase):
    def test_ota_repair_and_trial_authorization(self):
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <services/health_service.h>
#include <services/diagnostics.h>
#define SENSOR_FAULT_STARTING 0x40
typedef struct {bool valid; uint8_t error_code;} sensor_data_t;
typedef int atomic_t;
#define atomic_get(p) (*(p))
#define atomic_set(p,v) (*(p)=(v))
#define LOG_MODULE_REGISTER(...)
#define LOG_INF(...)
#define LOG_ERR(...)
#define ARG_UNUSED(x) (void)(x)
#define MGMT_EVT_OP_CMD_DONE 2
#define MGMT_EVT_OP_CMD_RECV 1
#define MGMT_OP_WRITE 2
#define MGMT_EVT_OP_IMG_MGMT_DFU_STARTED 4
#define MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED 8
#define MGMT_GROUP_ID_IMAGE 1
#define MGMT_GROUP_ID_OS 0
#define MGMT_ERR_EBUSY 10
#define MGMT_ERR_EBADSTATE 11
#define SYS_REBOOT_COLD 0
enum mgmt_cb_return {MGMT_CB_OK,MGMT_CB_ERROR_RC};
struct mgmt_evt_op_cmd_arg {uint16_t group;uint8_t id;uint8_t op;};
struct mgmt_callback {void *callback;int event_id;};
static bool confirmed;
static int battery_error, voltage=3900, reboots, confirms;
static int64_t now;
static bool boot_is_img_confirmed(void) {return confirmed;}
static int boot_write_img_confirmed(void) {confirms++;confirmed=true;return 0;}
static int battery_get_ota_voltage(uint16_t *mv) {*mv=voltage;return battery_error;}
static int64_t k_uptime_get(void) {return now;}
static void mgmt_callback_register(struct mgmt_callback *c) {}
static void sys_reboot(int mode) {reboots++;}
void health_progress(enum health_worker w,uint32_t ms) {}
uint32_t health_failed(void) {return 0;}
void diagnostics_record(enum diagnostic_event e,uint32_t d) {}
void diagnostics_reboot_event(enum diagnostic_event e,uint32_t d) {}
''' + STUBS + implementation('ota_service.c') + r'''
int main(void) {
 int32_t rc=0;uint16_t group=0;bool abort=false;
 struct mgmt_evt_op_cmd_arg cmd={1,1,MGMT_OP_WRITE};
 confirmed=true;ota_service_init();ota_service_startup_complete(false);
 sensor_data_t bad={false};ota_service_observe_sample(&bad);
 assert(command_check(1,MGMT_CB_OK,&rc,&group,&abort,&cmd,sizeof(cmd))==MGMT_CB_OK);
 voltage=3599;
 assert(command_check(1,MGMT_CB_OK,&rc,&group,&abort,&cmd,sizeof(cmd))==MGMT_CB_ERROR_RC);
 voltage=3900;battery_error=-1;
 assert(command_check(1,MGMT_CB_OK,&rc,&group,&abort,&cmd,sizeof(cmd))==MGMT_CB_ERROR_RC);
 battery_error=0;confirmed=false;ota_service_init();
 assert(command_check(1,MGMT_CB_OK,&rc,&group,&abort,&cmd,sizeof(cmd))==MGMT_CB_ERROR_RC);
 cmd.op=0;
 assert(command_check(1,MGMT_CB_OK,&rc,&group,&abort,&cmd,sizeof(cmd))==MGMT_CB_OK);
 now=120001;ota_service_poll();assert(reboots==1 && confirms==0);
 unsigned failures=history_count;
 image_event(MGMT_EVT_OP_IMG_MGMT_DFU_STARTED,MGMT_CB_OK,&rc,&group,&abort,NULL,0);
 image_event(MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED,MGMT_CB_OK,&rc,&group,&abort,NULL,0);
 assert(history_records[failures].type==RECORD_TYPE_DFU && history_records[failures].value==1);
 assert(history_records[failures+1].type==RECORD_TYPE_DFU_FAIL && history_records[failures+1].value==0);
 now=0;ota_service_init();ota_service_startup_complete(true);
 ota_service_sensor_ready();
 ota_service_poll();assert(confirms==0);
 unsigned startup_events=history_count;
 sensor_data_t starting={false,SENSOR_FAULT_STARTING};
 ota_service_observe_sample(&starting);ota_service_poll();
 assert(confirms==0 && history_count==startup_events);
 sensor_data_t good={true};ota_service_observe_sample(&good);ota_service_poll();
 assert(confirms==1);
 unsigned before=history_count;
 ota_service_observe_sample(&bad);ota_service_observe_sample(&bad);
 assert(history_count==before+1 && history_records[before].type==RECORD_TYPE_SENSOR_ERROR);
 ota_service_observe_sample(&good);ota_service_observe_sample(&bad);
 assert(history_count==before+2);
 /* Recovered sensors can confirm after an initial failed attempt. */
 confirmed=false;now=0;ota_service_init();ota_service_startup_complete(true);
 ota_service_observe_sample(&bad);ota_service_poll();assert(confirms==1);
 ota_service_observe_sample(&good);ota_service_poll();assert(confirms==1);
 now=60000;ota_service_sensor_ready();ota_service_poll();assert(confirms==1);
 ota_service_observe_sample(&good);ota_service_poll();assert(confirms==2);
 /* Non-sensor startup failures cannot be cleared by a recovered sensor. */
 confirmed=false;now=0;ota_service_init();ota_service_startup_complete(false);
 ota_service_sensor_ready();ota_service_observe_sample(&good);ota_service_poll();
 assert(confirms==2);now=120001;ota_service_poll();assert(reboots==2);
 /* A stale good sample followed by a fault must not confirm. */
 confirmed=false;now=0;ota_service_init();ota_service_startup_complete(true);
 ota_service_sensor_ready();ota_service_observe_sample(&good);
 ota_service_observe_sample(&bad);ota_service_poll();assert(confirms==2);
 now=120001;ota_service_poll();assert(reboots==3);

}
''')

    def test_expired_workers_cannot_hide_stalls(self):
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <services/health_service.h>
static int64_t now;
static int64_t k_uptime_get(void) {return now;}
struct k_spinlock {int dummy;};
typedef int k_spinlock_key_t;
static int k_spin_lock(struct k_spinlock *l) {return 0;}
static void k_spin_unlock(struct k_spinlock *l,int key) {}
struct k_work {int dummy;};
#define K_WORK_DELAYABLE_DEFINE(n,f) int n
#define ARG_UNUSED(x) (void)(x)
#define K_SECONDS(x) (x)
#define K_NO_WAIT 0
static void k_work_schedule(int *w,int timeout) {}
''' + implementation('health_service.c') + r'''
int main(void) {
 health_start();assert(health_failed()==0);
 now=4000;health_progress(HEALTH_MAIN,5000);assert(health_failed()==0);
 health_progress(HEALTH_SENSOR,180000+35000);
 now=5000;assert(health_failed()==0);
 health_progress(HEALTH_PROTOCOL,10);
 now=5011;health_progress(HEALTH_PROTOCOL,0);
 assert(health_failed()==(1U<<HEALTH_PROTOCOL));
 failed=0;health_progress(HEALTH_PROTOCOL,10);
 now=5022;health_progress(HEALTH_PROTOCOL,1000);
 assert(health_failed()==(1U<<HEALTH_PROTOCOL));
 failed=0;health_progress(HEALTH_PROTOCOL,0);
 now=35001;health_progress(HEALTH_WORKQUEUE,35000);
 assert(health_failed() & (1U<<HEALTH_WORKQUEUE));
 assert(!(health_failed() & (1U<<HEALTH_SENSOR)));
}
''')
