"""New history requests cannot replace the identity of a read already in flight."""
import unittest
from native_support import ROOT, run_c
class HistoryRequestTests(unittest.TestCase):
    def test_request_arriving_during_read_remains_pending(self):
        text=(ROOT/'application/src/services/data_log.c').read_text()
        code=text[text.index('void data_log_request('):text.index('void data_log_request_erase_all(')]
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <stddef.h>
#define LOG_INF(...)
#define LOG_WRN(...)
#define LOG_DBG(...)
#define LOG_ERR(...)
#define K_MSEC(x) (x)
#define FLASH_HALF_PAGE_COUNT 100
#define HISTORY_SIZE 256
#define RECORDS_PER_PAGE 32
#define GET_HIS_ADDR(a) ((a)->page*256)
struct k_spinlock {int dummy;};typedef int k_spinlock_key_t;
static struct k_spinlock request_lock;
static int k_spin_lock(struct k_spinlock *l) {return 0;}
static void k_spin_unlock(struct k_spinlock *l,int k) {}
static int data_log_mutex,data_log_request_sem;
static int k_mutex_lock(int *m,int t) {assert(!*m);*m=1;return 0;}
static void k_mutex_unlock(int *m) {assert(*m);*m=0;}
static void k_sem_give(int *s) {}
static void k_usleep(int t) {}
static bool initialized=true,request_pending;
static struct {uint16_t half_page_number;} pending_request;
static uint32_t pending_session;
typedef struct {uint16_t page;} store_area_t;
typedef struct {uint8_t buf[256];int records[32];} record_page_t;
static int spi_flash_power_up(void) {return 0;}
static int spi_flash_power_down(void) {return 0;}
static void set_store_area_from_half_page(store_area_t *a,uint16_t p) {a->page=p/2;}
static bool data_log_is_valid_record(int *r) {return false;}
void data_log_request(uint16_t p,uint32_t session);
static bool inject=true;
static int flash_read_chunked(uint32_t addr,void *buf,size_t size) {
 if(inject) {inject=false;data_log_request(8,42);}
 return 0;
}
''' + code + r'''
int main(void) {
 record_page_t page;uint8_t count;uint16_t number;uint32_t session;
 data_log_request(7,41);
 assert(data_log_process_request(&page,&count,&number,&session)==0);
 assert(number==7 && session==41 && data_log_has_pending_request());
 assert(data_log_process_request(&page,&count,&number,&session)==0);
 assert(number==8 && session==42 && !data_log_has_pending_request());
 assert(data_log_process_request(&page,&count,&number,&session)==-EINVAL);
}
''')
