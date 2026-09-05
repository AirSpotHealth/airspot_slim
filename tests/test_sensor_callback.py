"""Exercise the actual main callback with failed-sensor/battery combinations."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

class SensorCallbackTests(unittest.TestCase):
    def test_invalid_sample_still_protects_battery(self):
        text = (ROOT / 'application/src/main.c').read_text()
        start = text.index('static void sensor_data_callback(const sensor_data_t *data) {')
        end = text.index('\n}', start) + 2
        callback = text[start:end]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.c').write_text('''#include <assert.h>
#include <stddef.h>
#include <services/sensor_manager.h>
#define LOG_INF(...)
#define LOG_WRN(...)
#define LOG_DBG(...)
static bool low_battery_lockout_requested;
static int calibrated, automatic, sent;
static void software_asc_process_measurement(uint16_t co2) { automatic++; }
static void ota_service_observe_sample(const sensor_data_t *s) {}
static bool led_indicator_is_active(void) { return false; }
static void calibration_service_process_measurement(uint16_t co2) { calibrated++; }
static uint16_t user_settings_apply_co2_correction(uint16_t co2) { return co2; }
static bool ble_service_is_connected(void) { return true; }
static void protocol_send_realtime_co2(void) { sent++; }
''' + callback + '''
int main(void) {
  sensor_data_t sample = {.co2_ppm=999, .valid=false, .battery_low=true};
  sensor_data_callback(&sample);
  assert(low_battery_lockout_requested);
  assert(calibrated == 0 && automatic == 0 && sent == 1);
  low_battery_lockout_requested = false;
  sample.is_charging = true;
  sensor_data_callback(&sample);
  assert(!low_battery_lockout_requested);
  assert(calibrated == 0);
  sample.valid = true;
  sensor_data_callback(&sample);
  assert(calibrated == 1 && automatic == 1 && sent == 3);
  sensor_data_callback(NULL);
  assert(calibrated == 1);
}
''')
            subprocess.run(['cc', '-std=c11', '-fsanitize=address,undefined', '-I', str(ROOT / 'application/include'), str(path/'test.c'), '-o', str(path/'test')], check=True)
            subprocess.run([str(path/'test')], check=True)
