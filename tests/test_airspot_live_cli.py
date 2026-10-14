"""Verify real frame decoding and safe delegation without touching hardware."""
import asyncio
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / filename)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


live = load("airspot_live_test_module", "airspot_live.py")
cli = load("airspot_router_test_module", "airspot_cli.py")


class LiveCliTests(unittest.TestCase):
    def test_cli_opens_console_without_starting_watch(self):
        with patch.object(cli.sys.stdin, "isatty", return_value=True), \
             patch.object(cli.subprocess, "call", return_value=0) as call:
            self.assertEqual(cli.main(["cli"]), 0)
            self.assertEqual(call.call_args.args[0], [str(cli.LIVE), "cli"])

    def test_live_picker_selection_stays_on_device_as_scan_updates(self):
        sys.modules["airspot_live"] = live
        tui = load("airspot_tui_test_module", "airspot_tui.py")
        picker = tui.Picker()
        a = SimpleNamespace(address="a", name="AirSpot-A")
        b = SimpleNamespace(address="b", name="AirSpot-B")
        advertisement = SimpleNamespace(local_name=None, rssi=-50)
        picker.discovered(a, advertisement)
        self.assertEqual(picker.chosen(), [])
        picker.toggle()
        picker.discovered(b, advertisement)
        picker.discovered(a, advertisement)
        self.assertEqual([item[0].address for item in picker.chosen()], ["a"])
        self.assertEqual(len(picker.devices), 2)
        picker.move(1)
        picker.toggle()
        self.assertEqual(len(picker.chosen()), 2)
        picker.toggle()
        self.assertEqual(len(picker.chosen()), 1)

    def test_actual_screen_and_slim_frames(self):
        self.assertEqual(live.decode(bytes.fromhex("ffaa01026ac43d0c02aacf"))["co2"], 682)
        sample = live.decode(bytes.fromhex("ffaa010a00009fdd033509e914df4d"))
        self.assertEqual(sample["co2"], 821)
        self.assertEqual(sample["temperature"], 25.37)
        self.assertEqual(sample["humidity"], 53.43)

    def test_fault_sentinel_is_not_a_co2_reading(self):
        result = live.decode(bytes.fromhex("ffaa010a44000000ffff00000000f6"))
        self.assertIsNone(result["co2"])
        self.assertEqual(result["fault"], "0x44")

    def test_checksum_failure_is_rejected(self):
        with self.assertRaises(ValueError):
            live.decode(bytes.fromhex("ffaa01026ac43d0c02aace"))
        with self.assertRaises(ValueError):
            live.decode(b"\xff\xaa")

    def test_legacy_battery_and_native_policy(self):
        result = live.decode(bytes.fromhex("ffaa200140000a"))
        self.assertEqual(result["battery"], 64)
        result = live.decode(bytes.fromhex("ffaa3807010101000000846f"))
        self.assertEqual(result["asc"], "OFF")
        self.assertTrue(result["asc_verified"])

    def test_monitor_cannot_write_settings_or_reset(self):
        session = live.Session(None, "AirSpot-test", None)
        for command in (4, 5, 0x24, 0x27, 0x3A, 0xFA):
            with self.assertRaises(ValueError):
                asyncio.run(session.request(command))
        self.assertEqual(live.frame(0x38), bytes.fromhex("ffaa3800e1"))
        with self.assertRaises(ValueError):
            asyncio.run(session.request(0x38, payload=b"\x01"))
        self.assertEqual(live.frame(0x21, b"\x01"), bytes.fromhex("ffaa210101cc"))

    def test_selection_normalizes_ids_and_reports_missing(self):
        found = {"AirSpot-BE56FF": (None, None)}
        chosen, missing = live.select(found, ["be56ff", "123456"])
        self.assertEqual(list(chosen), ["AirSpot-BE56FF"])
        self.assertEqual(missing, ["AirSpot-123456"])

    def test_project_commands_keep_arguments_literal(self):
        argv, cwd = cli.invocation("slim", ["build", "--serial", "123;touch /tmp/no"])
        self.assertEqual(argv[-1], "123;touch /tmp/no")
        self.assertEqual(cwd, ROOT)
        argv, _ = cli.invocation("screen", ["connect", "AF94D2"])
        self.assertEqual(argv[1:], ["watch", "--type", "screen", "AF94D2"])
        argv, _ = cli.invocation("hub-10", ["logs", "/dev/tty;bad"])
        self.assertEqual(argv[-1], "/dev/tty;bad")
        self.assertIn('source "$1"', argv[2])

    def test_web_routes_only_existing_scripts(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d)
            (path / "package.json").write_text(json.dumps({"scripts": {"dev": "next dev"}}))
            previous = cli.PROJECTS.get("example")
            cli.PROJECTS["example"] = (path, "web", None)
            try:
                argv, cwd = cli.invocation("example", ["dev", "--port", "9999"])
                self.assertEqual(argv, ["npm", "run", "dev", "--", "--port", "9999"])
                self.assertEqual(cwd, path)
                with self.assertRaises(ValueError):
                    cli.invocation("example", ["deploy"])
            finally:
                if previous is None:
                    del cli.PROJECTS["example"]
                else:
                    cli.PROJECTS["example"] = previous
