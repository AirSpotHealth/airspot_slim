"""Compile the actual System OFF wake gate with deterministic boot/GPIO timing."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class WakeGateTests(unittest.TestCase):
    def test_initial_charger_read_precedes_gesture_validation(self):
        source = (ROOT / 'application/src/main.c').read_text()
        start = source.index('static void validate_system_off_wake(uint32_t hold_start)')
        function = source[start:source.index('\n}', start) + 2]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#define LOG_INF(...)
#define LOG_ERR(...)
#define K_SECONDS(s) ((s) * 1000)
#define POWER_ON_FADE_DELAY_MS 2000
#define POWER_ON_HOLD_MS 5000
#define LED_COLOR_WHITE 0
static bool soft_off_active, ready, held, charging, release_during_ramp;
static int wait_result, slept;
static uint32_t now;
static jmp_buf off;
static int qi_charger_wait_ready(int timeout) {
  assert(timeout == 1000);
  now += 310; ready = wait_result == 0;
  return wait_result;
}
static bool power_on_hold_active(void) {
  assert(ready); /* Regression: never inspect the initial false Qi cache. */
  return held && charging;
}
static uint32_t k_uptime_get_32(void) { return now; }
static void k_msleep(int ms) { now += ms; }
static bool led_indicator_ramp(int c, int lo, int hi, uint32_t ms, bool (*check)(void)) {
  assert(now + ms == POWER_ON_HOLD_MS);
  now += ms;
  if (release_during_ramp) { held = false; }
  return check();
}
static void enter_system_off(bool started) {
  assert(!started); slept++; longjmp(off, 1);
}
''' + function + r'''
static void run(bool press, bool charger, int read_result, bool release) {
  ready = false; held = press; charging = charger; wait_result = read_result;
  release_during_ramp = release; now = 0; slept = 0;
  if (setjmp(off) == 0) { validate_system_off_wake(0); }
}
int main(void) {
  run(true, true, 0, false);
  assert(!slept && !soft_off_active && now == 5000);
  run(false, true, 0, false); assert(slept);
  run(true, false, 0, false); assert(slept);
  run(true, true, -1, false); assert(slept && !ready);
  run(true, true, 0, true); assert(slept);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.c').write_text(harness)
            subprocess.run(['cc', '-std=c11', '-fsanitize=address,undefined',
                            str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)
