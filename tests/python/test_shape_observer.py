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


if __name__ == '__main__':
    unittest.main()
