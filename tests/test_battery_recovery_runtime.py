"""Compile production recovery and display logic at voltage/time boundaries."""
import unittest
from native_support import ROOT, run_c

class BatteryRecoveryTests(unittest.TestCase):
    def test_recovery_requires_uninterrupted_charge_and_valid_voltage(self):
        source=(ROOT/'application/src/main.c').read_text()
        code=source[source.index('static bool battery_recovery_ready('):source.index('static void enter_low_battery_lockout(')]
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <drivers/battery.h>
static int64_t now;
static int64_t k_uptime_get(void) {return now;}
''' + code + r'''
int main(void) {
 int64_t since=-1;
 assert(BATTERY_LOCKOUT_MV==3300 && BATTERY_RECOVER_MV==3400);
 assert(!battery_recovery_ready(true,true,3399,&since) && since==-1);
 assert(!battery_recovery_ready(true,true,3400,&since));
 now=9999;assert(!battery_recovery_ready(true,true,3400,&since));
 now=10000;assert(battery_recovery_ready(true,true,3400,&since));
 assert(!battery_recovery_ready(false,true,4000,&since) && since==-1);
 now=11000;assert(!battery_recovery_ready(true,true,3500,&since));
 now=20000;assert(!battery_recovery_ready(true,false,3500,&since) && since==-1);
 now=21000;assert(!battery_recovery_ready(true,true,3500,&since));
 now=30000;assert(!battery_recovery_ready(true,true,3399,&since) && since==-1);
 now=31000;assert(!battery_recovery_ready(true,true,3400,&since));
 now=40999;assert(!battery_recovery_ready(true,true,3400,&since));
 now=41000;assert(battery_recovery_ready(true,true,3400,&since));
}
''')

    def test_percentage_matches_usable_range(self):
        source=(ROOT/'application/src/drivers/battery.c').read_text()
        code=source[source.index('uint8_t battery_get_percentage('):source.index('bool battery_is_low(')]
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <drivers/battery.h>
#define BATT_VOLTAGE_FULL 4200
static bool ema_initialized=true;
static uint16_t ema_battery_mv;
uint16_t battery_read_voltage_mv(void) {return ema_battery_mv;}
''' + code + r'''
int main(void) {
 ema_battery_mv=3200;assert(battery_get_percentage()==1);
 ema_battery_mv=3300;assert(battery_get_percentage()==1);
 ema_battery_mv=3400;assert(battery_get_percentage()==12);
 ema_battery_mv=4200;assert(battery_get_percentage()==100);
 ema_battery_mv=4300;assert(battery_get_percentage()==100);
 int previous=1;
 for(int mv=3300;mv<=4300;mv++) {
  ema_battery_mv=mv;int percent=battery_get_percentage();
  assert(percent>=previous && percent<=100);previous=percent;
 }
}
''')
