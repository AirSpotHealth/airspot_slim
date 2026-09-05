import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('battery_report', Path(__file__).resolve().parents[1] / 'scripts/battery_report.py')
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)

class BatteryReportTests(unittest.TestCase):
    def run_report(self, events):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'events.csv'
            path.write_text('timestamp,event,interval_s,notes\n2026-09-05T00:00:00Z,start,5,test fixture\n' + events)
            return report.summarize(path)

    def test_disconnect_is_not_cutoff(self):
        result = self.run_report('2026-09-06T00:00:00Z,disconnected,,\n')
        self.assertEqual(result['elapsed_days'], 1)
        self.assertEqual(result['result'], 'incomplete')
        self.assertTrue(result['lower_bound_only'])

    def test_observed_cutoff(self):
        result = self.run_report('2026-09-06T12:00:00Z,low_battery_shutdown,,observed lockout\n')
        self.assertEqual(result['elapsed_days'], 1.5)
        self.assertEqual(result['result'], 'measured_cutoff')

    def test_reject_mode_change(self):
        with self.assertRaises(ValueError):
            self.run_report('2026-09-06T12:00:00Z,sample,60,\n')

    def test_crash_not_battery_runtime(self):
        self.assertEqual(self.run_report('2026-09-06T00:00:00Z,crash,,fault\n')['result'], 'failed_crash')
