"""Actual power monitor and packet encoder with independent charger/ADC inputs."""
import re
import unittest
from native_support import ROOT, run_c

class PowerReportingTests(unittest.TestCase):
    def test_on_demand_protection_charger_edges_reconnect_and_retry(self):
        source = (ROOT/'application/src/services/battery_monitor.c').read_text()
        source = re.sub(r'^#include .*$', '', source, flags=re.M)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define BATTERY_LOCKOUT_MV 3300
static int64_t now;
static bool charging, ready;
static uint32_t session=1;
static int adc_error, send_error, sends, reads, bas;
static uint16_t voltage=3900;
static int64_t k_uptime_get(void) {return now;}
static bool qi_charger_is_charging(void) {return charging;}
static bool airspot_nus_is_ready(void) {return ready;}
static uint32_t airspot_nus_generation(void) {return session;}
static int battery_get_ota_voltage(uint16_t *v) {reads++;*v=voltage;return adc_error;}
static uint8_t battery_get_percentage(void) {return 70;}
static void ble_service_update_battery_level(uint8_t p) {bas++;assert(p==70);}
static int protocol_send_battery_level(void) {sends++;return send_error;}
''' + source + r'''
int main(void) {
 assert(!battery_monitor_poll() && reads==1 && sends==0);
 now=100;ready=true;assert(!battery_monitor_poll() && sends==1);
 now=200;charging=true;assert(!battery_monitor_poll() && sends==2);
 now=300;charging=false;assert(!battery_monitor_poll() && sends==3);
 now=400;assert(!battery_monitor_poll() && sends==3);
 /* A dip between minute reports must still trigger protection. */
 now=1300;voltage=2926;assert(battery_monitor_poll() && sends==3);
 now=2300;voltage=3900;assert(!battery_monitor_poll() && sends==3);
 now=60300;assert(!battery_monitor_poll() && sends==4);
 /* A reconnect between polls must still send, even if ready never read false. */
 now++;session++;assert(!battery_monitor_poll() && sends==5);
 ready=false;now++;assert(!battery_monitor_poll());
 ready=true;send_error=-1;now++;assert(!battery_monitor_poll() && sends==6);
 now+=999;assert(!battery_monitor_poll() && sends==6);
 send_error=0;now++;assert(!battery_monitor_poll() && sends==7);
 /* No CO2 measurement is involved, including while disconnected. */
 ready=false;now+=60000;voltage=3200;assert(battery_monitor_poll());
 charging=true;now++;assert(!battery_monitor_poll());
 charging=false;adc_error=-1;voltage=0;now++;
 int previous=bas;assert(!battery_monitor_poll() && bas==previous);
 adc_error=0;voltage=3200;now+=5000;assert(battery_monitor_poll());
}
''')

    def test_power_packet_uses_live_charger_without_sensor_snapshot(self):
        source=(ROOT/'application/src/services/protocol.c').read_text()
        code=source[source.index('static int send_power_state('):source.index('/**\n * @brief Send full device state')]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <services/protocol.h>
static bool charging;
static int adc_error, protocol_thread_data;
static uint32_t command_generation=17;
static uint8_t reply[7];
static int battery_get_ota_voltage(uint16_t *v) {*v=3900;return adc_error;}
static uint8_t battery_get_percentage(void) {return 70;}
static bool qi_charger_is_charging(void) {return charging;}
static void *k_current_get(void) {return &protocol_thread_data;}
static void set_frame_checksum(uint8_t *f,int n) {for(int i=0;i<n-1;i++)f[n-1]+=f[i];}
static int airspot_nus_send_session(const uint8_t *f,int n,uint32_t g) {
 assert(n==7 && g==17);memcpy(reply,f,n);return 0;
}
''' + code + r'''
int main(void) {
 protocol_send_battery_level();assert(reply[4]==70 && reply[5]==0 && reply[3]==1);
 charging=true;protocol_send_battery_level();assert(reply[5]==1);
 charging=false;protocol_send_battery_level();assert(reply[5]==0);
 adc_error=-1;charging=true;protocol_send_battery_level();assert(reply[4]==255 && reply[5]==1);
 protocol_send_low_battery_lockout();assert(reply[2]==0x46 && reply[3]==2 && reply[4]==255);
 adc_error=0;protocol_send_low_battery_lockout();assert(reply[4]==1);
 uint8_t sum=0;for(int i=0;i<6;i++)sum+=reply[i];assert(sum==reply[6]);
}
''')
