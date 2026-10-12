"""Exercise include normalization without requiring a compiler or game input."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

CMAKE = shutil.which('cmake')
LAUNCHER = Path(__file__).resolve().parents[2] / 'tools/build/normalize_msvc_includes.cmake'


@unittest.skipUnless(CMAKE, 'CMake is unavailable')
class MsvcIncludeLauncherTests(unittest.TestCase):
    def run_launcher(self, fixture, prefix='Observação: incluindo arquivo: ', extra=()):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'compiler.cmake'
            source.write_text(fixture, encoding='utf-8')
            return subprocess.run([CMAKE, f'-DINCLUDE_PREFIX={prefix}', '-P', str(LAUNCHER),
                                   '--', CMAKE, '-P', str(source), *extra],
                                  capture_output=True, text=True, encoding='utf-8')

    def test_normalizes_metadata_and_preserves_pdb_path_argument(self):
        result = self.run_launcher(
            'message("Observação: incluindo arquivo: C:/source/header.hpp")\n'
            'if(NOT CMAKE_ARGV3 STREQUAL "/FdC:/folder with space/")\n'
            '  message(FATAL_ERROR "PDB argument changed or merged")\n'
            'endif()\n'
            'if(NOT CMAKE_ARGV4 STREQUAL "/FS")\n'
            '  message(FATAL_ERROR "following argument lost")\n'
            'endif()\n', extra=('/FdC:\\folder with space\\', '/FS'))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stderr, r'Note: including file: +C:/source/header.hpp')
        self.assertNotIn('Observação', result.stderr)

    def test_unrecognized_metadata_fails_closed(self):
        result = self.run_launcher('message("Observação: incluindo arquivo: C:/source/header.hpp")\n',
                                   prefix='Note: including file: ')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Unrecognized MSVC include metadata', result.stderr)

    def test_compiler_failure_is_not_success(self):
        result = self.run_launcher('message(FATAL_ERROR "compiler refused input")\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('compiler refused input', result.stderr)
        self.assertIn('MSVC compiler failed', result.stderr)

    def test_prefix_inside_diagnostic_is_unchanged(self):
        diagnostic = 'warning: example Observação: incluindo arquivo: C:/source/header.hpp'
        result = self.run_launcher(f'message("{diagnostic}")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(diagnostic, result.stderr)

    def test_unrecognized_unc_metadata_fails_closed(self):
        result = self.run_launcher(
            r'message([=[Observação: incluindo arquivo: \\server\share\header.hpp]=])' + '\n',
            prefix='Note: including file: ')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Unrecognized MSVC include metadata', result.stderr)

    def test_semicolon_argument_is_rejected_before_compiler_runs(self):
        result = self.run_launcher('message("compiler must not run")\n', extra=('foo;bar',))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Unsupported semicolon', result.stderr)
        self.assertNotIn('compiler must not run', result.stderr)
