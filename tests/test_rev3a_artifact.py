"""Regression checks using a real build, without touching hardware.

Run with the NCS Python after `airspot build`:
  python3 -m unittest discover -s tests -v
"""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "scripts/rev3a_artifact.py"
spec = importlib.util.spec_from_file_location("rev3a_artifact", TOOL)
artifact = importlib.util.module_from_spec(spec)
spec.loader.exec_module(artifact)


class ArtifactTests(unittest.TestCase):
    def setUp(self):
        source = ROOT / "application/build_rev3a_debug"
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.build = Path(self.tmp.name)
        (self.build / "zephyr").mkdir()
        manifest = json.loads((source / "rev3a-artifact.json").read_text())
        for file in manifest["sha256"]:
            shutil.copyfile(source / file, self.build / file)
        shutil.copyfile(source / "rev3a-artifact.json", self.build / "rev3a-artifact.json")

    def replace(self, file, old, new):
        path = self.build / file
        text = path.read_text()
        self.assertIn(old, text)
        path.write_text(text.replace(old, new))

    def verify_cli(self):
        return subprocess.run([sys.executable, str(TOOL), "verify", str(self.build)],
                              capture_output=True, text=True)

    def test_valid_build(self):
        result = self.verify_cli()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_legacy_mcu_rejected(self):
        self.replace("zephyr/.config", "CONFIG_SOC_NRF54L15_CPUAPP=y",
                     "CONFIG_SOC_NRF54L05_CPUAPP=y")
        with self.assertRaisesRegex(ValueError, "Not an nRF54L15"):
            artifact.validate_build(self.build)

    def test_old_button_pin_rejected(self):
        self.replace("zephyr/zephyr.dts", "gpios = < &gpio0 0x2 0x0 >",
                     "gpios = < &gpio2 0xa 0x0 >")
        with self.assertRaisesRegex(ValueError, "sw0 wiring"):
            artifact.validate_build(self.build)

    def test_flash_wp_cannot_drive_qi_enable(self):
        self.replace("zephyr/zephyr.dts", "gpios = < 0x7 0x0 >", "gpios = < 0xd 0x0 >")
        with self.assertRaisesRegex(ValueError, "flash_wp output"):
            artifact.validate_build(self.build)

    def test_settings_cannot_move_silently(self):
        self.replace("zephyr/zephyr.dts", "reg = < 0x74000 0x9000 >",
                     "reg = < 0x15c000 0x9000 >")
        with self.assertRaisesRegex(ValueError, "Settings location changed"):
            artifact.validate_build(self.build)

    def test_linker_cannot_overlap_settings(self):
        self.replace("zephyr/.config", "CONFIG_FLASH_LOAD_SIZE=0x74000",
                     "CONFIG_FLASH_LOAD_SIZE=0x17d000")
        with self.assertRaisesRegex(ValueError, "application limit"):
            artifact.validate_build(self.build)

    def test_truncated_hex_rejected(self):
        from intelhex import IntelHex
        path = next((self.build / "zephyr").glob("*.hex"))
        image = IntelHex(str(path))
        image = image[:0x10000]
        image.write_hex_file(str(path))
        with self.assertRaisesRegex(ValueError, "HEX and BIN"):
            artifact.validate_build(self.build)

    def test_hex_cannot_write_settings(self):
        from intelhex import IntelHex
        path = next((self.build / "zephyr").glob("*.hex"))
        image = IntelHex(str(path))
        image[artifact.APP_END] = 0
        image.write_hex_file(str(path))
        with self.assertRaisesRegex(ValueError, "outside the application"):
            artifact.validate_build(self.build)

    def test_runner_tampering_rejected(self):
        self.replace("zephyr/runners.yaml", "nRF54L15_M33", "nRF54L05_M33")
        result = self.verify_cli()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Incorrect J-Link target", result.stderr)

    def test_elf_changed_since_build_rejected(self):
        path = next((self.build / "zephyr").glob("*.elf"))
        with path.open("ab") as stream:
            stream.write(b"changed")
        result = self.verify_cli()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Artifacts changed", result.stderr)

    def test_missing_success_manifest_rejected(self):
        (self.build / "rev3a-artifact.json").unlink()
        self.assertNotEqual(self.verify_cli().returncode, 0)


if __name__ == "__main__":
    unittest.main()
