"""Exercise real generated artifacts and migration safety without touching hardware."""
import hashlib
import json
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
        cls.source = ROOT / 'application/build_production'
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

    def test_valid_signed_build(self):
        self.assertEqual(artifact.validate_build(self.build)['version'][:3], [0, 14, 0])

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
    def test_preserve_settings_and_clear_pending_images(self):
        boot, app = b'bootloader', b'signed app'
        before = bytearray(b'\xff' * provision.RRAM_SIZE)
        before[:len(boot)] = boot
        before[0xb6000:0xb6010] = b'old staged image'
        settings = bytes(range(256)) * (provision.SETTINGS_SIZE // 256)
        before[provision.SETTINGS_START:provision.APP_MEMORY_END] = settings
        image, source = provision.prepare_image(bytes(before), boot, app, True)
        self.assertEqual(source, provision.SETTINGS_START)
        self.assertEqual(image[provision.SETTINGS_START:], settings)
        self.assertEqual(image[0xb6000:0x15c000], b'\xff' * 0xa6000)

    def test_refuse_unknown_existing_firmware(self):
        with self.assertRaisesRegex(ValueError, 'Unknown installed'):
            provision.prepare_image(b'\x00' * provision.RRAM_SIZE, b'boot', b'app', False)

    def test_blank_factory_device(self):
        image, source = provision.prepare_image(b'\xff' * provision.RRAM_SIZE, b'boot', b'app', False)
        self.assertEqual(source, provision.SETTINGS_START)
        self.assertEqual(image[:4], b'boot')

    def test_update_refuses_unprovisioned_device(self):
        with self.assertRaisesRegex(ValueError, 'Unknown installed'):
            provision.prepare_image(b'\xff' * provision.RRAM_SIZE, b'boot', b'app', True)


if __name__ == '__main__': unittest.main()
