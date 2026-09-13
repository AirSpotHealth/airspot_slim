"""Real ADC averaging / OTA freshness path with injected peripheral faults."""
import unittest
from native_support import ROOT, run_c
class BatteryOtaTests(unittest.TestCase):
    def test_freshness_adc_failure_and_falling_supply(self):
        source=(ROOT/'application/src/drivers/battery.c').read_text()
        source=source[source.index('static uint16_t read_voltage_locked('):source.index('uint8_t battery_get_percentage(')]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#define LOG_WRN(...)
#define LOG_ERR(...)
#define LOG_DBG(...)
#define HAS_ADC_EN_GPIO 1
#define ADC_STABILIZATION_MS 1
#define ADC_SAMPLE_DELAY_US 200
#define ADC_NUM_SAMPLES 4
#define VOLTAGE_DIVIDER_INVERSE (1.0f/0.6f)
#define EMA_ALPHA_NUM 2
#define EMA_ALPHA_DEN 10
#define K_FOREVER 0
#define K_MSEC(x) (x)
#define MIN(a,b) ((a)<(b)?(a):(b))
static int64_t now,voltage_time;
static int battery_mutex,adc_en_gpio;
static bool initialized=true,voltage_valid,ema_initialized;
static int32_t ema_battery_mv,last_raw_adc,last_adc_mv;
static uint16_t unfiltered_mv;
static int raw=2400,reads,lock_error,enable_error,disable_error,conversion_error;
static int k_mutex_lock(int *m,int t) {if(lock_error)return lock_error;assert(!*m);*m=1;return 0;}
static void k_mutex_unlock(int *m) {assert(*m);*m=0;}
static int gpio_pin_set_dt(int *p,int value) {return value?enable_error:disable_error;}
static int32_t adc_read_single_sample(void) {assert(battery_mutex==1);reads++;return raw;}
static int32_t adc_raw_to_mv(int32_t r) {return conversion_error?conversion_error:r;}
static void k_msleep(int ms) {}
static void k_usleep(int us) {}
static int64_t k_uptime_get(void) {return now;}
''' + source + r'''
int main(void) {
 uint16_t mv;
 assert(battery_get_ota_voltage(&mv)==0 && mv>=3999);
 int previous=reads;
 now=4999;assert(battery_get_ota_voltage(&mv)==0 && reads==previous);
 now=5000;raw=-1;
 assert(battery_get_ota_voltage(&mv)==-EIO && mv==0);
 assert(ema_battery_mv>=3999); // Display cache is not permission to update.
 raw=2100;assert(battery_get_ota_voltage(&mv)==0 && mv<3600);
 now+=5000;enable_error=-EIO;
 assert(battery_get_ota_voltage(&mv)==-EIO);
 enable_error=0;disable_error=-EIO;
 assert(battery_get_ota_voltage(&mv)==-EIO);
 disable_error=0;conversion_error=-EIO;
 assert(battery_get_ota_voltage(&mv)==-EIO);
 conversion_error=0;lock_error=-EBUSY;
 assert(battery_get_ota_voltage(&mv)==-EBUSY && mv==0);
}
''')
