"""Compile the real settings implementation against fault-injectable NVS callbacks."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from native_support import BYTEORDER, CRC

ROOT = Path(__file__).resolve().parents[1]

class SettingsRuntimeTests(unittest.TestCase):
    def _run_settings(self, native=False, experiment=False, rob=False, production=False):
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
  assert(settings.native_asc_mode == 0);
  user_settings_t before = settings;
  /* Upgrade a pre-native 0.17.7 snapshot with non-default settings. */
  for (int automatic=0; automatic<=1; automatic++) {
    user_settings_t old=settings, migrated;
    old.co2_correction=-321; old.auto_calibrate=automatic;
    old.power_mode=2; old.calib_target=450;
    uint8_t legacy_snapshot[49]; encode_settings(&old,legacy_snapshot);
    memset(legacy_snapshot+26,0,8);
    sys_put_le32(crc32_ieee(legacy_snapshot,45),legacy_snapshot+45);
    assert(decode_settings(legacy_snapshot,&migrated)==0);
    assert(migrated.native_asc_mode==0 && migrated.co2_correction==-321);
    assert(migrated.auto_calibrate==automatic && migrated.calib_target==450);
    assert(migrated.power_mode==2);
  }
  for (int policy=0; policy<=2; policy++) {
    user_settings_t saved=settings, restored;
    saved.native_asc_mode=policy;
    uint8_t snapshot[49]; encode_settings(&saved,snapshot);
    assert(decode_settings(snapshot,&restored)==0);
    assert(restored.native_asc_mode==policy);
  }
  uint8_t obsolete[] = {1, 20, 30, 7, 15};
  const char *obsolete_keys[] = {"flight", "buzzer", "vibrator", "dnd"};
  for (int i=0;i<4;i++) {
    assert(settings_set(obsolete_keys[i], i==3 ? 5 : 1, read_record, obsolete)==-ENOENT);
    assert(legacy_error==0 && memcmp(&settings,&before,sizeof(settings))==0);
  }
  uint8_t good_power=2;
  assert(load_value("power",2,read_record,&good_power)==-EINVAL);
  short_read=1;
  assert(load_value("power",1,read_record,&good_power)==-EIO);
  assert(memcmp(&settings,&before,sizeof(settings))==0);
  short_read=0;
  uint8_t mode = 4;
  assert(load_value("power", 1, read_record, &mode) == -EINVAL);
  assert(user_settings_set_power_mode(4) == -EINVAL);
  save_error = -EIO;
  before = settings;
  assert(user_settings_set_power_mode(3) == -EIO);
  assert(user_settings_set_device_name("changed") == -EIO);
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
  /* Old schema-1 settings can carry enabled Screen-only fields. Ignore those
   * bytes while preserving the name, mode, correction and other offsets. */
  assert(persisted[26]==0 && persisted[27]==0x53 && persisted[28]==0xa1);
  for(int i=29;i<34;i++)assert(persisted[i]==0);
  uint8_t legacy[49];memcpy(legacy,persisted,49);
  const uint8_t old_fields[]={1,1,20,30,7,15,1,1};
  memcpy(legacy+26,old_fields,8);
  sys_put_le32(crc32_ieee(legacy,45),legacy+45);
  assert(decode_settings(legacy,&decoded)==0);
  assert(decoded.native_asc_mode==0);
  assert(decoded.auto_calibrate==settings.auto_calibrate);
  assert(decoded.co2_yellow_start==900 && decoded.co2_red_start==1200);
  assert(decoded.power_mode==settings.power_mode && decoded.co2_correction==settings.co2_correction);
  assert(decoded.calib_target==settings.calib_target && strcmp(decoded.device_name,settings.device_name)==0);
  encode_settings(&decoded,legacy);
  assert(legacy[26]==0 && legacy[27]==0x53 && legacy[28]==0xa1);
  for(int i=29;i<34;i++)assert(legacy[i]==0);
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
            if native:
                text = source.read_text()
                text = text[:text.index('int main(void)')] + r'''
int main(void) {
  assert(settings.native_asc_mode==0);
  settings.native_asc_mode=2; /* Explicit saved/user-selected preference. */
  settings.co2_correction=-1234;settings.auto_calibrate=0;settings.calib_target=426;
  user_settings_t before=settings;
  assert(user_settings_get_co2_correction()==0);
  assert(user_settings_apply_co2_correction(800)==800);
  assert(user_settings_apply_co2_correction(32767)==32767);
  assert(user_settings_get_auto_calibrate()==1);
  assert(user_settings_get_calib_target()==400);
  user_settings_t effective;user_settings_get_snapshot(&effective);
  assert(effective.co2_correction==0 && effective.auto_calibrate==1 && effective.calib_target==400);
  assert(memcmp(&settings,&before,sizeof(settings))==0 && saves==0);
  assert(user_settings_commit_manual_correction(100)==-EBUSY);
  assert(user_settings_set_native_asc_mode(1)==0);
  assert(settings.co2_correction==-1234 && user_settings_get_co2_correction()==0);
  assert(user_settings_get_auto_calibrate()==0 && user_settings_apply_co2_correction(800)==800);
  user_settings_t decoded;assert(decode_settings(persisted,&decoded)==0 && decoded.native_asc_mode==1);
  save_error=-EIO;assert(user_settings_set_native_asc_mode(2)==-EIO);
  assert(settings.native_asc_mode==1);
  assert(user_settings_commit_manual_correction(20)==-EIO && settings.native_asc_mode==1);
  save_error=0;assert(user_settings_commit_manual_correction(20)==0);
  assert(settings.native_asc_mode==0 && user_settings_apply_co2_correction(800)==820);
  return 0;
}
'''
                source.write_text(text)
            text = source.read_text()
            at = text.rfind('  return 0;')
            assert at >= 0
            text = text[:at] + r"""
  save_error=0;image_confirmed=1;
  settings.co2_correction=-345;settings.calib_target=450;
  settings.native_asc_mode=1;settings.auto_calibrate=0;settings.power_mode=2;
  assert(user_settings_reset_preferences()==0);
  assert(settings.co2_correction==-345 && settings.calib_target==450);
  assert(settings.native_asc_mode==1 && settings.power_mode==2);
  user_settings_t preserved=settings;
  save_error=-EIO;assert(user_settings_reset_preferences()==-EIO);
  assert(!memcmp(&preserved,&settings,sizeof(settings)));
  assert(user_settings_start_settling()==-EIO);
  assert(!memcmp(&preserved,&settings,sizeof(settings)));
  save_error=0;assert(user_settings_start_settling()==0);
  assert(settings.native_asc_mode==2 && settings.power_mode==3 && settings.co2_correction==0);
""" + text[at:]
            source.write_text(text)
            if rob:
                text=source.read_text().split('int main(void) {')[0]+r'''
int main(void) {
 assert(settings.native_asc_mode==2);
 settings.native_asc_mode=0;settings.co2_correction=-350;settings.auto_calibrate=1;
 settings.power_mode=2;
 assert(!user_settings_init());
 assert(settings.native_asc_mode==2 && settings.power_mode==2);
 assert(user_settings_apply_co2_correction(800)==800 && !settings.co2_correction);
 assert(!settings.auto_calibrate && user_settings_get_auto_calibrate()==1);
 assert(!user_settings_set_native_asc_mode(1));
 initialized=false;assert(!user_settings_init());
 assert(settings.native_asc_mode==1 && user_settings_apply_co2_correction(800)==800);
 assert(!user_settings_get_auto_calibrate());
 assert(!user_settings_set_native_asc_mode(2));
 assert(user_settings_apply_co2_correction(800)==800);
 return 0;
}
'''
                source.write_text(text)
            if production:
                text=source.read_text().split('int main(void) {')[0]+r'''
int main(void) {
 settings.native_asc_mode=1;settings.power_mode=1;
 assert(!user_settings_init());
 assert(settings.native_asc_mode==2 && user_settings_get_auto_calibrate());
 assert(user_settings_set_native_asc_mode(0)==-ENOTSUP);
 assert(user_settings_set_native_asc_mode(1)==-ENOTSUP);
 assert(user_settings_set_auto_calibrate(0)==-ENOTSUP);
 assert(settings.native_asc_mode==2);
 assert(!user_settings_set_native_asc_mode(2));
 assert(!user_settings_set_power_mode(4));
 user_settings_t restored;uint8_t saved[49];encode_settings(&settings,saved);
 assert(!decode_settings(saved,&restored) && restored.power_mode==4);
 settings=restored;initialized=false;assert(!user_settings_init());
 assert(settings.power_mode==4);
 assert(user_settings_set_power_mode(2)==-EINVAL);
 return 0;
}
'''
                source.write_text(text)
            subprocess.run(['cc', *(['-DCONFIG_AIRSPOT_NATIVE_TEST_MODES=1'] if production else []), *(['-DCONFIG_AIRSPOT_SENSOR_NATIVE_ONLY=1'] if rob else []), *(['-DCONFIG_AIRSPOT_STCC4_INTERNAL_ASC_EXPERIMENT=1'] if experiment else []), '-std=c11', '-fsanitize=address,undefined',
                            '-I', str(root), '-I', str(ROOT / 'application/include'),
                            str(source), '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)

    def test_corruption_and_failed_writes(self):
        self._run_settings()

    def test_native_asc_bypasses_host_offset_without_erasing_saved_settings(self):
        self._run_settings(native=True)

    def test_experiment_overlay_cannot_enable_native_asc_during_legacy_upgrade(self):
        self._run_settings(experiment=True)
        self._run_settings(native=True, experiment=True)

    def test_rob_defaults_bypass_offsets_and_retain_native_off(self):
        self._run_settings(rob=True)

    def test_production_always_enables_native_asc(self):
        self._run_settings(rob=True, production=True)
