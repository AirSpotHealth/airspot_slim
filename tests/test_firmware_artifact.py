"""Exercise real generated artifacts and migration safety without touching hardware."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import firmware_artifact as artifact
import provision


class FirmwareArtifactTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = Path(os.environ.get('AIRSPOT_TEST_BUILD_DIR', ROOT / 'application/build_production'))
        if not (cls.source / 'firmware-artifact.json').exists():
            raise RuntimeError('Build production firmware before running artifact tests')

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.build = Path(self.temp.name)
        manifest = json.loads((self.source / 'firmware-artifact.json').read_text())
        for name in manifest['sha256']:
            target = self.build / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(self.source / name, target)

    def tearDown(self):
        self.temp.cleanup()

    def edit(self, name, before, after):
        path = self.build / name
        text = path.read_text()
        self.assertIn(before, text)
        path.write_text(text.replace(before, after))

    def test_reject_bulk_slot_preparation(self):
        self.edit("application/zephyr/.config", "CONFIG_IMG_ERASE_PROGRESSIVELY=y", "# CONFIG_IMG_ERASE_PROGRESSIVELY is not set")
        with self.assertRaisesRegex(ValueError, "bulk slot preparation"):
            artifact.validate_build(self.build)

    def test_valid_signed_build(self):
        version = dict(line.split('=', 1) for line in
                       (ROOT / 'application/VERSION').read_text().splitlines() if '=' in line)
        version = {key.strip(): value.strip() for key, value in version.items()}
        expected = [int(version[key]) for key in
                    ('VERSION_MAJOR', 'VERSION_MINOR', 'PATCHLEVEL', 'VERSION_TWEAK')]
        self.assertEqual(list(artifact.validate_build(self.build)['version']), expected)

    def test_reject_wrong_mcu(self):
        self.edit('application/zephyr/.config', 'CONFIG_SOC_NRF54L15_CPUAPP=y', '# CONFIG_SOC_NRF54L15_CPUAPP is not set')
        with self.assertRaisesRegex(ValueError, 'nRF54L15'): artifact.validate_build(self.build)

    def test_reject_bootloader_in_application_slot(self):
        self.edit('mcuboot/zephyr/.config', 'CONFIG_FLASH_LOAD_OFFSET=0x0', 'CONFIG_FLASH_LOAD_OFFSET=0x10000')
        with self.assertRaisesRegex(ValueError, 'linker start'): artifact.validate_build(self.build)

    def test_reject_missing_rollback(self):
        self.edit('mcuboot/zephyr/.config', 'CONFIG_BOOT_SWAP_USING_MOVE=y', '# CONFIG_BOOT_SWAP_USING_MOVE is not set')
        with self.assertRaisesRegex(ValueError, 'BOOT_SWAP'): artifact.validate_build(self.build)

    def test_reject_client_bypass_of_trial_boot(self):
        self.edit('application/zephyr/.config', '# CONFIG_MCUMGR_GRP_IMG_ALLOW_CONFIRM_NON_ACTIVE_SLOT is not set', 'CONFIG_MCUMGR_GRP_IMG_ALLOW_CONFIRM_NON_ACTIVE_SLOT=y')
        with self.assertRaisesRegex(ValueError, 'bypass'): artifact.validate_build(self.build)

    def test_reject_stale_rtt_metadata(self):
        self.edit('application/zephyr/.config', 'CONFIG_SEGGER_RTT_INIT_MODE_ALWAYS=y', '# CONFIG_SEGGER_RTT_INIT_MODE_ALWAYS is not set')
        with self.assertRaisesRegex(ValueError, 'RTT must'): artifact.validate_build(self.build)

    def test_reject_tampered_ota(self):
        path = self.build / 'application/zephyr/airspot_slim.signed.bin'
        data = bytearray(path.read_bytes()); data[4096] ^= 1; path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'signature'): artifact.validate_build(self.build)

    def test_reject_different_boot_key(self):
        path = self.build / 'mcuboot/zephyr/autogen-pubkey.c'
        path.write_text('unsigned char key[] = {0x00};')
        with self.assertRaisesRegex(ValueError, 'product key'): artifact.validate_build(self.build)

    def test_reject_unsigned_hex_corruption(self):
        path = self.build / 'application/zephyr/airspot_slim.hex'
        image = artifact.IntelHex(str(path)); image[0x20000] ^= 1; image.write_hex_file(str(path))
        with self.assertRaisesRegex(ValueError, 'HEX/BIN'): artifact.validate_build(self.build)

    def test_reject_hex_outside_slot(self):
        path = self.build / 'application/zephyr/airspot_slim.signed.hex'
        image = artifact.IntelHex(str(path)); image[0x15c000] = 0; image.write_hex_file(str(path))
        with self.assertRaises(ValueError): artifact.validate_build(self.build)


class ProvisionTests(unittest.TestCase):
    def test_any_installed_firmware_preserves_current_settings(self):
        settings = bytes(range(256)) * (provision.SETTINGS_SIZE // 256)
        for prefix in (b'old boot', b'unknown!', b'\x00' * 8, b'\xff' * 8):
            before = bytearray(b'\xff' * provision.RRAM_SIZE)
            before[:len(prefix)] = prefix
            before[provision.SETTINGS_START:provision.APP_MEMORY_END] = settings
            image, source = provision.prepare_image(bytes(before), b'new boot', b'app')
            self.assertEqual(source, provision.SETTINGS_START)
            self.assertEqual(image[:8], b'new boot')
            self.assertEqual(image[0x10000:0x10003], b'app')
            self.assertEqual(image[provision.SETTINGS_START:], settings)
            self.assertEqual(image[0xb6000:0x15c000], b'\xff' * 0xa6000)

    def test_explicit_legacy_migration(self):
        before = bytearray(b'\xff' * provision.RRAM_SIZE)
        settings = b'\x42' * provision.SETTINGS_SIZE
        before[0x74000:0x74000 + provision.SETTINGS_SIZE] = settings
        image, source = provision.prepare_image(bytes(before), b'boot', b'app', 0x74000)
        self.assertEqual(source, 0x74000)
        self.assertEqual(image[provision.SETTINGS_START:], settings)

    def test_reset_settings(self):
        image, source = provision.prepare_image(b'\x42' * provision.RRAM_SIZE,
                                                b'boot', b'app', reset_settings=True)
        self.assertIsNone(source)
        self.assertEqual(image[provision.SETTINGS_START:], b'\xff' * provision.SETTINGS_SIZE)

    def test_blank_device(self):
        image, source = provision.prepare_image(b'\xff' * provision.RRAM_SIZE, b'boot', b'app')
        self.assertEqual(source, provision.SETTINGS_START)
        self.assertEqual(image[:4], b'boot')

    def test_incomplete_backup_and_invalid_ranges_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            provision.prepare_image(b'bad', b'boot', b'app')
        for source in (-1, provision.APP_MEMORY_END):
            with self.assertRaisesRegex(ValueError, 'Settings source'):
                provision.prepare_image(b'\xff' * provision.RRAM_SIZE, b'boot', b'app', source)
        for boot, app in ((b'x' * 0x10001, b'app'), (b'boot', b'x' * 0xa6001)):
            with self.assertRaisesRegex(ValueError, 'partition'):
                provision.prepare_image(b'\xff' * provision.RRAM_SIZE, boot, app)

    def test_full_install_unknown_firmware_without_history(self):
        import os
        import struct
        from unittest.mock import patch
        for reset in (False, True):
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                build = root / 'build'
                for name, data in [('mcuboot/zephyr/zephyr.bin', b'new boot'),
                                   ('application/zephyr/airspot_slim.signed.bin', b'app'),
                                   ('firmware-artifact.json', b'{"sha256": {}}')]:
                    path = build / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)
                before = b'\x42' * provision.RRAM_SIZE
                calls = []
                def device(commands, serial, backup):
                    calls.append(commands)
                    if any('ficr.bin' in command for command in commands):
                        (backup / 'ficr.bin').write_bytes(struct.pack('<5I', 0x54b15, 0, 0, 256, 1524))
                        (backup / 'before_rram.bin').write_bytes(before)
                    if any('after_rram.bin' in command for command in commands):
                        (backup / 'after_rram.bin').write_bytes(
                            (backup / 'install.bin').read_bytes() + before[provision.APP_MEMORY_END:])
                argv = ['provision.py', str(build)] + (['--reset-settings'] if reset else [])
                with patch.object(provision, 'ROOT', root), \
                     patch.object(provision, 'validate_build', return_value={'sha256': {}}), \
                     patch.object(provision, 'jlink', side_effect=device), \
                     patch.dict(os.environ, {'AIRSPOT_JLINK_SERIAL': '123'}), \
                     patch.object(sys, 'argv', argv):
                    provision.main()
                backup = next((root / 'local_backups').glob('production-*'))
                self.assertEqual((backup / 'before_rram.bin').read_bytes(), before)
                self.assertEqual((backup / 'settings.bin').read_bytes(), b'\x42' * provision.SETTINGS_SIZE)
                metadata = json.loads((backup / 'backup.json').read_text())
                self.assertEqual(metadata['settings_reset'], reset)
                image = (backup / 'install.bin').read_bytes()
                self.assertEqual(image[provision.SETTINGS_START:],
                                 (b'\xff' if reset else b'\x42') * provision.SETTINGS_SIZE)
                self.assertEqual(calls[-1], ['r', 'g'])


if __name__ == '__main__': unittest.main()
