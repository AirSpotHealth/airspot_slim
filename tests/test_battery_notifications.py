"""Exercise production BAS update behavior against a notifying stack stub."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class BatteryNotificationTests(unittest.TestCase):
    def test_only_changed_clamped_levels_notify(self):
        source = (ROOT / "application/src/services/ble_service.c").read_text()
        function = source[source.index("void ble_service_update_battery_level("):]
        # This function is the final function in the service.
        harness = r"""
#include <stdint.h>
#include <assert.h>
#define LOG_WRN(...)
static uint8_t level = 100;
static int notifications;
uint8_t bt_bas_get_battery_level(void) { return level; }
int bt_bas_set_battery_level(uint8_t value) {
    level = value; notifications++; return 0;
}
""" + function + r"""
int main(void) {
    ble_service_update_battery_level(100);
    assert(notifications == 0);
    for (int i = 0; i < 180; ++i) ble_service_update_battery_level(80);
    assert(notifications == 1 && level == 80);
    ble_service_update_battery_level(79);
    assert(notifications == 2 && level == 79);
    ble_service_update_battery_level(255);
    ble_service_update_battery_level(100);
    assert(notifications == 3 && level == 100);
    ble_service_update_battery_level(0);
    assert(notifications == 4 && level == 0);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            (path / "test.c").write_text(harness)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", str(path / "test.c"), "-o", str(path / "test")], check=True, capture_output=True)
            subprocess.run([str(path / "test")], check=True, capture_output=True)
