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
from external_runtime_probe import PERF_MANIFEST, PERF_RUNTIME, PERF_PROFILES, UPSCALE_MANIFEST, VU1_RECORD_MANIFEST, VU1_RECORD_RUNTIME


class PerformanceLauncherTests(unittest.TestCase):
    def test_wrapper_selects_qualified_profile(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for mode in PERF_PROFILES:
                arguments = ['perf', '--assets-root', str(root)]
                if mode != 'detailed': arguments.extend(['--mode', mode])
                with patch.object(performance, 'ROOT', root), patch.object(performance, 'play') as play, \
                     patch.object(sys, 'argv', arguments):
                    self.assertEqual(performance.main(), 0)
                play.assert_called_once_with(root, root / 'docs/inputs' / PERF_PROFILES[mode][0], performance=True)

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
                            launcher.play(root, manifest, performance=True)
                    finally:
                        lock.seek(0); msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
                execute.assert_not_called()
                manifest.write_text(json.dumps(dict(evidence, profile='old')))
                with self.assertRaisesRegex(ValueError, 'qualified'):
                    launcher.play(root, manifest, performance=True)
            self.assertEqual(card.read_bytes(), b'c' * 8650752)
            self.assertFalse(any(directory.glob('session-*')))

    def test_upscale_mode_passes_scale_and_is_not_a_performance_launch(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with patch.object(performance, 'ROOT', root), patch.object(performance, 'play') as play,                  patch.object(sys, 'argv', ['perf', '--assets-root', str(root), '--mode', 'upscale', '--scale', '3', '--force-480p']):
                self.assertEqual(performance.main(), 0)
            play.assert_called_once_with(root, root / 'docs/inputs' / UPSCALE_MANIFEST,
                                         performance=False, force_480p=True, scale=3, coregs11=None)
            # The default is the qualified scale 1, and upscale is not a performance profile.
            with patch.object(performance, 'ROOT', root), patch.object(performance, 'play') as play,                  patch.object(sys, 'argv', ['perf', '--assets-root', str(root), '--mode', 'upscale']):
                self.assertEqual(performance.main(), 0)
            self.assertEqual(play.call_args.kwargs, {'performance': False, 'force_480p': False, 'scale': 1, 'coregs11': None})

    def test_coregs11_options_reach_the_upscale_launch_only(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            arguments = ['perf', '--assets-root', str(root), '--mode', 'upscale', '--scale', '2', '--deinterlace', 'bob',
                         '--aa', 'fxaa', '--present-filter', 'fsr', '--fsr-sharpness', '0.5']
            with patch.object(performance, 'ROOT', root), patch.object(performance, 'play') as play,                  patch.object(sys, 'argv', arguments):
                self.assertEqual(performance.main(), 0)
            self.assertEqual(play.call_args.kwargs['coregs11'],
                             {'GT4_DEINTERLACE': 'bob', 'GT4_AA': 'fxaa', 'GT4_PRESENT_FILTER': 'fsr', 'GT4_FSR_SHARPNESS': '0.5'})
            # Defaults are not sent: the game keeps automatic deinterlace, AA off and the scale-based filter.
            with patch.object(performance, 'ROOT', root), patch.object(performance, 'play') as play,                  patch.object(sys, 'argv', ['perf', '--assets-root', str(root), '--mode', 'upscale']):
                self.assertEqual(performance.main(), 0)
            self.assertIsNone(play.call_args.kwargs['coregs11'])
            with patch.object(performance, 'ROOT', root), patch.object(performance, 'play') as play,                  patch.object(sys, 'argv', ['perf', '--assets-root', str(root), '--mode', 'main', '--aa', 'fxaa']):
                self.assertEqual(performance.main(), 1)
            play.assert_not_called()
            with patch.object(performance, 'ROOT', root), patch.object(performance, 'play') as play,                  patch.object(sys, 'argv', ['perf', '--assets-root', str(root), '--mode', 'upscale', '--present-filter', 'bicubic']):
                with self.assertRaises(SystemExit):
                    performance.main()
            play.assert_not_called()

    def test_scale_is_refused_outside_upscale_and_out_of_range(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for arguments in (['--mode', 'main', '--scale', '2'], ['--mode', 'upscale', '--scale', '5'],
                              ['--mode', 'upscale', '--scale', '0'], ['--scale', '2']):
                with patch.object(performance, 'ROOT', root), patch.object(performance, 'play') as play,                      patch.object(sys, 'argv', ['perf', '--assets-root', str(root)] + arguments):
                    self.assertEqual(performance.main(), 1, arguments)
                play.assert_not_called()

    def test_vu1_record_mode_selects_only_its_pinned_manifest(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with patch.object(performance, 'ROOT', root), patch.object(performance, 'play') as play, \
                 patch.object(sys, 'argv', ['perf', '--assets-root', str(root), '--mode', 'vu1-record']):
                self.assertEqual(performance.main(), 0)
            play.assert_called_once_with(root, root / 'docs/inputs' / VU1_RECORD_MANIFEST,
                                         performance=True, vu1_record=True)
            with patch.object(performance, 'ROOT', root), patch.object(performance, 'play') as play, \
                 patch.object(sys, 'argv', ['perf', '--assets-root', str(root)]):
                self.assertEqual(performance.main(), 0)
            self.assertEqual(play.call_args.kwargs, {'performance': True})

    def test_vu1_record_launch_passes_flag_and_protects_card(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            inputs = root / 'docs/inputs'
            inputs.mkdir(parents=True)
            manifest = inputs / VU1_RECORD_MANIFEST
            evidence = {'profile': 'performance', 'perf_schema_version': 1,
                        'keyboard_input_fix': 'digital_only_slice136', 'capture_activation_implemented': False,
                        'measurement_mode': 'vu-split', 'vu_scope_profile': 'unit_split_run_fast_only',
                        'runtime': {'path': VU1_RECORD_RUNTIME}, 'default_card_seed': {'path': 'private/seed.ps2'}}
            manifest.write_text(json.dumps(evidence))
            directory = root / 'private/manual-play'
            directory.mkdir(parents=True)
            card = directory / 'card.ps2'
            card.write_bytes(b'c' * 8650752)
            with patch.object(launcher, 'ROOT', root), patch.object(launcher, 'run_probe') as execute:
                self.assertEqual(launcher.play(root, manifest, performance=True, vu1_record=True), None)
                self.assertTrue(execute.call_args.kwargs['vu1_record'])
                self.assertTrue(execute.call_args.kwargs['performance'])
                execute.reset_mock()
                launcher.play(root, manifest, performance=True)
                self.assertFalse(execute.call_args.kwargs['vu1_record'])
            self.assertEqual(card.read_bytes(), b'c' * 8650752)
