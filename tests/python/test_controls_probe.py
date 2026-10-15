"""GT4_CONTROLS_FILE pass-through: launcher checks, child environment and run evidence."""

import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import external_runtime_probe as probe

EXAMPLES = ROOT / 'docs/controls'


class ControlsPassThroughTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.directory = Path(directory.name)

    def write(self, text, name='controls.toml'):
        path = self.directory / name
        path.write_text(text, encoding='utf-8')
        return path

    def test_example_files_pass_the_launcher_check(self):
        for name in ('default.toml', 'keyboard-only.toml', 'triggers-as-pedals.toml'):
            with self.subTest(name=name):
                self.assertEqual(probe.resolve_controls_file(EXAMPLES / name), (EXAMPLES / name).resolve())

    def test_bad_files_are_refused_before_launch(self):
        cases = {
            'schema missing': ('[bindings]\ncross = ["key:Z"]\n', 'needs schema'),
            'schema two': ('schema = 2\n', 'needs schema'),
            'schema as string': ('schema = "1"\n', 'needs schema'),
            'unknown table': ('schema = 1\n[pad]\ncross = ["key:Z"]\n', 'unknown keys'),
            'unknown input': ('schema = 1\n[bindings]\ntriangel = ["key:S"]\n', 'unknown input'),
            'number instead of sources': ('schema = 1\n[bindings]\ncross = 3\n', 'string or a list'),
            'non-string item': ('schema = 1\n[bindings]\ncross = [1]\n', 'string or a list'),
            'unknown source kind': ('schema = 1\n[bindings]\ncross = ["mouse:left"]\n', 'unknown source'),
            'not TOML': ('schema = \n', 'not valid TOML'),
        }
        for label, (text, fragment) in cases.items():
            with self.subTest(label=label):
                with self.assertRaisesRegex(ValueError, fragment):
                    probe.resolve_controls_file(self.write(text))

    def test_missing_file_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'not found'):
            probe.resolve_controls_file(self.directory / 'absent.toml')

    def test_run_probe_refuses_a_bad_file_before_creating_output(self):
        bad = self.write('schema = 1\n[bindings]\ntriangel = ["key:S"]\n')
        run_directory = ROOT / 'private' / 'controls-refusal-test-run'
        self.assertFalse(run_directory.exists())
        with self.assertRaisesRegex(ValueError, 'unknown input'):
            probe.run_probe(ROOT, run_directory, 10, 10, [], controls_file=bad)
        self.assertFalse(run_directory.exists())

    def test_no_file_gives_defaults_and_clears_an_inherited_value(self):
        environment = {'GT4_CONTROLS_FILE': '/stale/controls.toml', 'PATH': '/bin'}
        path = probe.resolve_controls_file(None)
        probe.apply_controls_environment(environment, path)
        self.assertNotIn('GT4_CONTROLS_FILE', environment)
        self.assertEqual(environment['PATH'], '/bin')
        self.assertEqual(probe.controls_evidence(path), {'source': 'default'})

    def test_file_is_passed_to_the_game_and_recorded_by_hash(self):
        path = probe.resolve_controls_file(EXAMPLES / 'triggers-as-pedals.toml')
        environment = {'GT4_CONTROLS_FILE': 'stale'}
        probe.apply_controls_environment(environment, path)
        self.assertEqual(environment['GT4_CONTROLS_FILE'], str(path))
        data = path.read_bytes()
        self.assertEqual(probe.controls_evidence(path), {
            'source': 'file', 'path': str(path), 'size_bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest()})


if __name__ == '__main__':
    unittest.main()
