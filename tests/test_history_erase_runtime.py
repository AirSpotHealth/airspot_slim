"""Run the production erase path against persistent metadata and injected failures."""
import unittest
from native_support import ROOT, run_c

class HistoryEraseTests(unittest.TestCase):
    def test_durable_cursor_and_failures(self):
        source = (ROOT/'application/src/services/data_log.c').read_text()
        function = source[source.index('int data_log_erase_all(void)'):source.index('/*\n * =============================================================================\n * Utility Functions', source.index('int data_log_erase_all(void)'))]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#define LOG_INF(...)
#define LOG_WRN(...)
#define LOG_ERR(...)
#define K_MSEC(x) (x)
#define FLASH_METADATA_OFFSET 0x64000
#define FLASH_SECTOR_COUNT 3
#define FLASH_ADDR_OF_SECTOR(s) (0x65000+(s)*4096)
static int data_log_mutex;
static bool initialized=true, metadata_dirty;
static uint32_t total_records_count, metadata_seq_num;
static struct {int sector,page,count;} fst_store_area,cur_store_area;
static char rec_page[256],cur_record[8];
static int metadata_valid, persisted_sector, persisted_count;
static int fail_metadata_erase, fail_history_erase, fail_write, erases, feeds;
static int k_mutex_lock(int *m,int t){return 0;}
static void k_mutex_unlock(int *m){}
static int spi_flash_power_up(void){return 0;}
static int spi_flash_power_down(void){return 0;}
static void k_usleep(int t){}
static void k_yield(void){}
static void watchdog_feed(void){feeds++;}
static int spi_flash_erase_sector(uint32_t addr){
 assert(addr>=FLASH_METADATA_OFFSET); /* Never erase firmware. */
 if(addr==FLASH_METADATA_OFFSET){
  if(fail_metadata_erase)return -EIO;
  metadata_valid=0;return 0;
 }
 assert(!metadata_valid); /* Cursor invalidated before history changes. */
 if(fail_history_erase)return -EIO;
 erases++;return 0;
}
static int metadata_write(uint32_t t){
 if(fail_write)return -EIO;
 assert(erases==FLASH_SECTOR_COUNT);
 assert(total_records_count==0 && metadata_seq_num==0);
 persisted_sector=cur_store_area.sector;
 persisted_count=cur_store_area.count;
 metadata_valid=1;metadata_dirty=false;return 0;
}
static void setup(void){
 cur_store_area.sector=fst_store_area.sector=172;
 cur_store_area.page=fst_store_area.page=8;
 cur_store_area.count=12;total_records_count=5500;metadata_seq_num=88;
 metadata_valid=1;persisted_sector=172;persisted_count=12;
 feeds=erases=fail_metadata_erase=fail_history_erase=fail_write=0;
}
''' + function + r'''
int main(void){
 setup(); assert(data_log_erase_all()==0);
 assert(metadata_valid && persisted_sector==0 && persisted_count==0);
 assert(feeds==FLASH_SECTOR_COUNT);
 assert(fst_store_area.sector==0 && fst_store_area.page==0);
 assert(cur_store_area.page==0 && total_records_count==0);
 /* Simulate restart loading the durable cursor rather than old RAM. */
 cur_store_area.sector=999;cur_store_area.sector=persisted_sector;
 assert(cur_store_area.sector==0);
 setup();fail_metadata_erase=1;
 assert(data_log_erase_all()==-EIO && erases==0 && metadata_valid);
 setup();fail_history_erase=1;
 assert(data_log_erase_all()==-EIO && !metadata_valid);
 setup();fail_write=1;
 assert(data_log_erase_all()==-EIO && !metadata_valid && metadata_dirty);
}
''')
