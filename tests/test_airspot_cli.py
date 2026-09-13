"""Exercise CLI dispatch without performing any hardware operations."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ['airspot-production', 'terminal.sh']:
            shutil.copy(ROOT / 'scripts' / name, self.root / name)
        (self.root / 'VERSION').write_text('VERSION_MAJOR = 1\nVERSION_MINOR = 2\nPATCHLEVEL = 3\n')
        (self.root / 'airspot').write_text('''NCS_TOOLCHAIN_DIR="$script_dir"
AIRSPOT_BUILD_DIR="$script_dir/build"
AIRSPOT_PROJECT_DIR="$script_dir"
AIRSPOT_BOARD=test-board
''')
        (self.root / 'bin').mkdir()
        self.executable('bin/python3', '''#!/bin/sh
case "$1" in
  */probe.py) echo 000123 ;;
  *) echo "python $*" >> "$CALLS" ;;
esac
''')
        self.executable('bin/nrfutil', '#!/bin/sh\necho "$*" >> "$CALLS"\n')
        self.env = {**os.environ, 'PATH': str(self.root / 'bin') + os.pathsep + os.environ['PATH'],
                    'CALLS': str(self.root / 'calls')}

    def executable(self, path, content):
        target = self.root / path
        target.write_text(content)
        target.chmod(0o755)

    def run_cli(self, *args):
        return subprocess.run(['bash', str(self.root / 'airspot-production'), *args],
                              env=self.env, input='', capture_output=True, text=True, timeout=10)

    def test_help_lists_restored_commands_without_ansi_in_pipe(self):
        result = self.run_cli()
        self.assertEqual(result.returncode, 0)
        for command in ['erase', 'reset', 'menu', 'bf / bfl', 'version', 'ports']:
            self.assertIn(command, result.stdout)
        self.assertNotIn('\x1b', result.stdout)

    def test_erase_refuses_unconfirmed_noninteractive_execution(self):
        result = self.run_cli('erase')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('confirmation', result.stderr)
        self.assertFalse((self.root / 'calls').exists())

    def test_erase_explicit_yes_targets_only_selected_probe(self):
        result = self.run_cli('erase', '-y', '--serial', '123')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / 'calls').read_text(),
                         'device erase --all --family nrf54l --serial-number 000123\n')

    def test_reset_targets_selected_probe(self):
        result = self.run_cli('reset')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / 'calls').read_text(),
                         'device reset --family nrf54l --serial-number 000123\n')

    def test_yes_rejected_for_other_commands(self):
        self.assertNotEqual(self.run_cli('flash', '-y').returncode, 0)
        self.assertFalse((self.root / 'calls').exists())

    def test_flash_passes_settings_options(self):
        result = self.run_cli('flash', '--settings-source', '0x74000')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('provision.py', (self.root / 'calls').read_text())
        self.assertIn('--settings-source 0x74000', (self.root / 'calls').read_text())

    def test_settings_options_rejected_for_other_commands(self):
        self.assertNotEqual(self.run_cli('logs', '--reset-settings').returncode, 0)
        self.assertFalse((self.root / 'calls').exists())

    def test_menu_requires_terminal(self):
        result = self.run_cli('menu')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('requires a terminal', result.stderr)


if __name__ == '__main__':
    unittest.main()
