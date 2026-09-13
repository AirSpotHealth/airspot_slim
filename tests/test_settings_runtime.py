"""Compile the real settings implementation against fault-injectable NVS callbacks."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from native_support import BYTEORDER, CRC

ROOT = Path(__file__).resolve().parents[1]

class SettingsRuntimeTests(unittest.TestCase):
    def test_corruption_and_failed_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            headers = {
                'kernel.h': '#include <errno.h>\n#include <stddef.h>\n#define K_MUTEX_DEFINE(x) int x\n#define K_FOREVER 0\n#define ARG_UNUSED(x) (void)(x)\nstatic void k_mutex_lock(int *m,int t) {(*m)++;}\nstatic void k_mutex_unlock(int *m) {(*m)--;}\n',
                'sys/byteorder.h': BYTEORDER,
                'sys/crc.h': CRC,
                'dfu/mcuboot.h': 'static int image_confirmed=1; static int boot_is_img_confirmed(void) {return image_confirmed;}\n',
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
static int save_error, load_error, short_read, saves;
static uint8_t persisted[49];
int settings_save_one(const char *k, const void *v, size_t n) { saves++; if (!save_error) {assert(n==49);memcpy(persisted,v,n);} return save_error; }
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
  assert(load_value("dnd", 4, read_record, dnd) == -EINVAL);
  assert(memcmp(&settings, &before, sizeof(settings)) == 0);
  short_read = 1;
  assert(load_value("dnd", 5, read_record, dnd) == -EIO);
  assert(memcmp(&settings, &before, sizeof(settings)) == 0);
  short_read = 0;
  dnd[1] = 24;
  assert(load_value("dnd", 5, read_record, dnd) == -EINVAL);
  dnd[1] = 20;
  assert(load_value("dnd", 5, read_record, dnd) == 0);
  assert(settings.dnd_start_hour == 20 && settings.dnd_end_minute == 15);
  uint8_t mode = 4;
  assert(load_value("power", 1, read_record, &mode) == -EINVAL);
  assert(user_settings_set_power_mode(4) == -EINVAL);
  save_error = -EIO;
  before = settings;
  assert(user_settings_set_power_mode(3) == -EIO);
  assert(user_settings_set_device_name("changed") == -EIO);
  assert(user_settings_set_dnd(0, 1, 2, 3, 4) == -EIO);
  assert(user_settings_set_co2_correction(123) == -EIO);
  assert(user_settings_set_paired(true) == -EIO);
  assert(memcmp(&settings, &before, sizeof(settings)) == 0);
  assert(user_settings_set_co2_thresholds(900,1200) == -EIO);
  assert(user_settings_factory_reset() == -EIO);
  assert(memcmp(&settings, &before, sizeof(settings)) == 0);
  save_error = 0;
  int saved_before=saves;
  assert(user_settings_set_co2_thresholds(900,1200)==0 && saves==saved_before+1);
  user_settings_t decoded;
  assert(decode_settings(persisted,&decoded)==0);
  assert(decoded.co2_yellow_start==900 && decoded.co2_red_start==1200);
  uint8_t good[49];memcpy(good,persisted,49);
  persisted[30]^=1;
  assert(decode_settings(persisted,&decoded)==-EINVAL);
  assert(settings_set("snapshot",49,read_record,good)==0);
  uint8_t old_power=1;
  assert(load_value("power",1,read_record,&old_power)==0);
  snapshot_commit();
  assert(settings.co2_yellow_start==900 && settings.co2_red_start==1200);
  good[30]^=1;
  assert(settings_set("snapshot",49,read_record,good)==-EINVAL);
  assert(snapshot_commit()==-EINVAL); // Zephyr ignores h_set errors.
  saved_before=saves;
  assert(user_settings_set_paired(true)==-EIO);
  assert(user_settings_set_device_name("must not overwrite")==-EIO);
  assert(saves==saved_before);
  snapshot_error=0;
  snapshot_loaded=false;
  assert(user_settings_set_power_mode(3) == 0);
  assert(settings.power_mode == 3);
  load_error = -EIO;
  assert(user_settings_init() == -EIO);
  assert(!initialized);
  saved_before=saves;
  assert(user_settings_set_paired(true)==-EIO);
  assert(user_settings_set_device_name("blocked")==-EIO);
  assert(saves==saved_before);
  load_error = 0;
  assert(user_settings_init() == 0);
  image_confirmed=0;
  assert(user_settings_set_power_mode(1)==-EBUSY);
  assert(user_settings_factory_reset()==-EBUSY);
  image_confirmed=1;
  persistence_blocked=true;
  snapshot_error=-EINVAL;
  saved_before=saves;
  assert(user_settings_factory_reset()==0 && saves==saved_before+1);
  assert(decode_settings(persisted,&decoded)==0);
  assert(decoded.co2_yellow_start==800 && decoded.co2_red_start==1000);
  assert(decoded.power_mode==1 && decoded.calib_target==426);
  return 0;
}
''')
            subprocess.run(['cc', '-std=c11', '-fsanitize=address,undefined',
                            '-I', str(root), '-I', str(ROOT / 'application/include'),
                            str(source), '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)
