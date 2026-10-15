"""HWR launcher-side option checks (TASK-027 M0, chapter 12.8 and 12.2).

Written from the TASK-026 specification (docs/hw-renderer-spec), clean room per decision 0069.

The C++ parser is the single source of truth; these tests drive the built
hw_gs_smoke executable with environment variants, the way the launch scripts
will pass GT4_RENDERER and GT4_HW_* explicitly.
"""
import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get('GT4_NATIVE_BUILD', ROOT / 'private/slice173-hw-m0-build/build'))
SMOKE = BUILD / 'hw_gs_smoke.exe'

# Every key the launch scripts may pass, scrubbed from the inherited environment.
HW_KEYS = [name for name in os.environ if name.startswith('GT4_HW_') or name == 'GT4_RENDERER']


def run_smoke(arguments, extra=None):
    environment = {name: value for name, value in os.environ.items() if name not in HW_KEYS}
    environment.update(extra or {})
    return subprocess.run([str(SMOKE), *arguments], env=environment, capture_output=True,
                          text=True, timeout=60)


@unittest.skipUnless(SMOKE.exists(), 'hw_gs_smoke is built only with GT4_HW_GS=ON')
class HwLauncherOptionTests(unittest.TestCase):
    def test_unset_environment_reports_donor_and_defaults(self):
        result = run_smoke(['--print-options'])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('GT4_RENDERER=donor', result.stdout)
        self.assertIn('GT4_HW_SCALE=1', result.stdout)
        self.assertIn('GT4_HW_BLEND=high', result.stdout)
        self.assertIn('"GT4_HW_GLM": "1"', result.stdout)

    def test_valid_values_are_echoed_in_text_and_json(self):
        result = run_smoke(['--print-options'], {
            'GT4_HW_SCALE': '4',
            'GT4_HW_DEINTERLACE': 'bob_bff',
            'GT4_HW_CROP': '0,8,0,8',
            'GT4_HW_SPRITE_FIX': 'round,merge',
        })
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('GT4_HW_SCALE=4', result.stdout)
        self.assertIn('GT4_HW_DEINTERLACE=bob_bff', result.stdout)
        self.assertIn('GT4_HW_CROP=0,8,0,8', result.stdout)
        self.assertIn('GT4_HW_SPRITE_FIX=round,merge', result.stdout)
        self.assertIn('"GT4_HW_SCALE": "4"', result.stdout)

    def test_invalid_hw_value_aborts_with_the_variable_name(self):
        for name, value in [('GT4_HW_SCALE', '9'), ('GT4_HW_BLEND', 'exact'), ('GT4_HW_GLM', 'yes'),
                            ('GT4_HW_DEINTERLACE', 'off_bff'), ('GT4_HW_SHADEBOOST', '1,2,3')]:
            with self.subTest(variable=name):
                result = run_smoke(['--print-options'], {name: value})
                self.assertEqual(result.returncode, 2)
                self.assertIn(name, result.stderr)

    def test_unknown_renderer_aborts(self):
        result = run_smoke(['--print-options'], {'GT4_RENDERER': 'vulkan'})
        self.assertEqual(result.returncode, 2)
        self.assertIn('GT4_RENDERER', result.stderr)

    def test_donor_selection_runs_nothing(self):
        result = run_smoke(['--frames', '5'])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('HWR not selected', result.stdout)

    def test_parallel_is_refused_until_built_in(self):
        result = run_smoke(['--frames', '1'], {'GT4_RENDERER': 'parallel'})
        self.assertEqual(result.returncode, 1)
        self.assertIn('paraLLEl-GS', result.stderr)


if __name__ == '__main__':
    unittest.main()
