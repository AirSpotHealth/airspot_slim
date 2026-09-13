"""Probe selection must never silently choose among multiple devices."""
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import probe


class ProbeTests(unittest.TestCase):
    def select(self, serials, explicit=None, interactive=False, answer="", env=None):
        devices = [{"serialNumber": sn, "traits": {"jlink": True},
                    "usb": {"product": "J-Link"}} for sn in serials]
        result = subprocess.CompletedProcess([], 0, json.dumps(
            {"type": "info", "data": {"devices": devices}}))
        stdin = io.StringIO(answer)
        with patch.dict(os.environ, env or {}, clear=True), \
             patch.object(probe.subprocess, "run", return_value=result), \
             patch.object(probe.sys, "stdin", stdin), \
             patch.object(stdin, "isatty", return_value=interactive), \
             patch.object(probe.sys, "stderr", io.StringIO()):
            return probe.select_probe(explicit)

    def test_single_probe_without_export(self):
        self.assertEqual(self.select(["000123"]), "000123")

    def test_multiple_probes_prompt_and_retry(self):
        self.assertEqual(self.select(["123", "456"], interactive=True,
                                     answer="9\n2\n"), "456")

    def test_multiple_probes_require_selection_without_terminal(self):
        with self.assertRaisesRegex(ValueError, "--serial"):
            self.select(["123", "456"])

    def test_explicit_serial_overrides_environment(self):
        self.assertEqual(self.select(["123", "456"], "000456",
                         env={"AIRSPOT_JLINK_SERIAL": "123"}), "456")

    def test_missing_probe_and_cancel(self):
        for serials, explicit, interactive, answer in [
            ([], None, False, ""), (["123"], "456", False, ""),
            (["123", "456"], None, True, "q\n")]:
            with self.assertRaises(ValueError):
                self.select(serials, explicit, interactive, answer)

    def test_streams_firmware_without_logger_chatter_and_returns_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            logger = root / "JLinkRTTLogger"
            logger.write_text('#!/bin/sh\necho "transfer chatter"\n'
                              'for output do :; done\nprintf "sensor reading\\n" > "$output"\nexit 7\n')
            logger.chmod(0o755)
            log = root / "rtt.log"
            log.write_text("old session\n")
            result = subprocess.run(
                [sys.executable, probe.__file__, "logs", str(log)],
                env={**os.environ, "PATH": str(root) + os.pathsep + os.environ["PATH"]},
                capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 7)
            self.assertIn("sensor reading", result.stdout)
            self.assertNotIn("old session", result.stdout)
            self.assertNotIn("transfer chatter", result.stdout)
            self.assertIn("RTT logger failed", result.stderr)
            self.assertIn("transfer chatter", log.with_suffix(".jlink.log").read_text())


if __name__ == "__main__":
    unittest.main()
