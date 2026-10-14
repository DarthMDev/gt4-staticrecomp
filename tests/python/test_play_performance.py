import json
import msvcrt
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import play_performance as performance
import play_external_runtime as launcher
from external_runtime_probe import PERF_MANIFEST, PERF_RUNTIME, PERF_PROFILES


class PerformanceLauncherTests(unittest.TestCase):
    def test_wrapper_selects_qualified_profile(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for mode in PERF_PROFILES:
                arguments = ['perf', '--assets-root', str(root)]
                if mode == 'frame-only': arguments.extend(['--mode', mode])
                with patch.object(performance, 'ROOT', root), patch.object(performance, 'play') as play, \
                     patch.object(sys, 'argv', arguments):
                    self.assertEqual(performance.main(), 0)
                play.assert_called_once_with('16:9', root, root / 'docs/inputs' / PERF_PROFILES[mode][0], performance=True)

    def test_lock_failure_and_profile_rejection_preserve_card(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            inputs = root / 'docs/inputs'
            inputs.mkdir(parents=True)
            manifest = inputs / PERF_MANIFEST
            evidence = {'profile': 'performance', 'perf_schema_version': 1,
                        'keyboard_input_fix': 'digital_only_slice136',
                        'capture_activation_implemented': False, 'runtime': {'path': PERF_RUNTIME}}
            manifest.write_text(json.dumps(evidence))
            directory = root / 'private/manual-play'
            directory.mkdir(parents=True)
            card = directory / 'card.ps2'
            card.write_bytes(b'c' * 8650752)
            with patch.object(launcher, 'ROOT', root), patch.object(launcher, 'run_probe') as execute:
                with (directory / 'session.lock').open('w+b') as lock:
                    lock.write(b'0'); lock.flush(); lock.seek(0)
                    msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
                    try:
                        with self.assertRaisesRegex(ValueError, 'already running'):
                            launcher.play('16:9', root, manifest, performance=True)
                    finally:
                        lock.seek(0); msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
                execute.assert_not_called()
                manifest.write_text(json.dumps(dict(evidence, profile='old')))
                with self.assertRaisesRegex(ValueError, 'qualified'):
                    launcher.play('16:9', root, manifest, performance=True)
            self.assertEqual(card.read_bytes(), b'c' * 8650752)
            self.assertFalse(any(directory.glob('session-*')))
