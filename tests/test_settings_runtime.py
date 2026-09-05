"""Compile the real settings implementation against fault-injectable NVS callbacks."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class SettingsRuntimeTests(unittest.TestCase):
    def test_corruption_and_failed_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            headers = {
                'kernel.h': '#include <errno.h>\n#include <stddef.h>\n',
                'logging/log.h': '#define LOG_MODULE_REGISTER(...)\n#define LOG_INF(...)\n#define LOG_WRN(...)\n#define LOG_ERR(...)\n',
                'settings/settings.h': '''#include <stddef.h>
#include <sys/types.h>
typedef ssize_t (*settings_read_cb)(void *, void *, size_t);
int settings_save_one(const char *, const void *, size_t);
int settings_delete(const char *);
int settings_subsys_init(void);
int settings_load(void);
#define SETTINGS_STATIC_HANDLER_DEFINE(...)
''',
            }
            for name, text in headers.items():
                path = root / 'zephyr' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            source = root / 'test.c'
            source.write_text('''#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "''' + str(ROOT / 'application/src/services/user_settings.c') + '''"
static int save_error, load_error, short_read;
int settings_save_one(const char *k, const void *v, size_t n) { return save_error; }
int settings_delete(const char *k) { return 0; }
int settings_subsys_init(void) { return 0; }
int settings_load(void) { return load_error; }
static ssize_t read_record(void *data, void *out, size_t n) {
  size_t actual = short_read && n ? n - 1 : n;
  memcpy(out, data, actual);
  return actual;
}
int main(void) {
  user_settings_t before = settings;
  uint8_t dnd[] = {1, 20, 30, 7, 15};
  assert(settings_set("dnd", 4, read_record, dnd) == -EINVAL);
  assert(memcmp(&settings, &before, sizeof(settings)) == 0);
  short_read = 1;
  assert(settings_set("dnd", 5, read_record, dnd) == -EIO);
  assert(memcmp(&settings, &before, sizeof(settings)) == 0);
  short_read = 0;
  dnd[1] = 24;
  assert(settings_set("dnd", 5, read_record, dnd) == -EINVAL);
  dnd[1] = 20;
  assert(settings_set("dnd", 5, read_record, dnd) == 0);
  assert(settings.dnd_start_hour == 20 && settings.dnd_end_minute == 15);
  uint8_t mode = 4;
  assert(settings_set("power", 1, read_record, &mode) == -EINVAL);
  assert(user_settings_set_power_mode(4) == -EINVAL);
  save_error = -EIO;
  before = settings;
  assert(user_settings_set_power_mode(3) == -EIO);
  assert(user_settings_set_device_name("changed") == -EIO);
  assert(user_settings_set_dnd(0, 1, 2, 3, 4) == -EIO);
  assert(user_settings_set_co2_correction(123) == -EIO);
  assert(user_settings_set_paired(true) == -EIO);
  assert(memcmp(&settings, &before, sizeof(settings)) == 0);
  save_error = 0;
  assert(user_settings_set_power_mode(3) == 0);
  assert(settings.power_mode == 3);
  load_error = -EIO;
  assert(user_settings_init() == -EIO);
  assert(!initialized);
  load_error = 0;
  assert(user_settings_init() == 0);
  return 0;
}
''')
            subprocess.run(['cc', '-std=c11', '-fsanitize=address,undefined',
                            '-I', str(root), '-I', str(ROOT / 'application/include'),
                            str(source), '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)
