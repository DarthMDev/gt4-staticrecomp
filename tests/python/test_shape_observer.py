"""Run the actual bounded C++ observer controls through native test discovery."""
import os
from pathlib import Path
import subprocess
import unittest


class ShapeObserverTests(unittest.TestCase):
    def test_native_observer_contract(self):
        root = Path(__file__).resolve().parents[2]
        build = Path(os.environ.get('GT4_NATIVE_BUILD', root / 'private/slice114-native-base/build'))
        executable = build / 'shape_observer_fixture.exe'
        if not executable.exists() and 'GT4_NATIVE_BUILD' not in os.environ:
            self.skipTest('Build the native observer fixture or set GT4_NATIVE_BUILD')
        result = subprocess.run([str(executable)], capture_output=True, text=True,
                                check=True, timeout=30)
        self.assertIn('PASS: shape observer memory, frontier, ownership and bounds controls', result.stdout)

    def test_actual_runtime_adapter_matches_unoverlaid_guest_control(self):
        if os.environ.get('GT4_SHAPE_RUNTIME_TESTS') != '1':
            self.skipTest('Experimental runtime overlay fixture is not configured')
        build = Path(os.environ['GT4_NATIVE_BUILD'])
        results = [subprocess.run([str(build / name)], capture_output=True, check=True, timeout=30)
                   for name in ('shape_runtime_control.exe', 'shape_runtime_fixture.exe')]
        self.assertEqual(results[0].stdout, results[1].stdout)
        self.assertIn(b'PASS: synthetic runtime adapter/lifecycle controls', results[1].stderr)
        self.assertIn(b'[probe] 0x00707070', results[1].stderr)


if __name__ == '__main__':
    unittest.main()
