"""Native pilot trust boundaries and optional real GPU fixture."""
import json
import ctypes
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import prepare_external_vulkan as prepare
import external_runtime_probe as probe
import play_external_runtime as play
import test_external_runtime_probe as probe_tests


def native_executable(name):
    build = Path(os.environ.get('GT4_NATIVE_BUILD', prepare.ROOT / 'private/slice114-native-base/build'))
    return build / name


class NativeVulkanTests(unittest.TestCase):
    def test_first_manual_card_uses_default_seed_and_preserves_existing_card(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            inputs = root / 'docs/inputs'
            inputs.mkdir(parents=True)
            (inputs / 'slice114-native-base.json').write_text(json.dumps({
                'default_card_seed': {'path': 'private/wide.ps2'},
                'standard_card_seed': {'path': 'private/standard.ps2'}}))
            with patch.object(play, 'ROOT', root), patch.object(play, 'run_probe',
                 return_value={'manual_session_closed': True}) as launch:
                play.play()
            self.assertEqual(launch.call_args.args[5], root / 'private/wide.ps2')
            self.assertNotIn('aspect', launch.call_args.kwargs)
            card = root / 'private/manual-play/card.ps2'
            card.write_bytes(b'existing owner card')
            with patch.object(play, 'ROOT', root), patch.object(play, 'run_probe',
                 return_value={'manual_session_closed': True}) as launch:
                play.play()
            self.assertIsNone(launch.call_args.args[5])
            self.assertEqual(card.read_bytes(), b'existing owner card')

    def test_runtime_seed_is_verified_and_cannot_be_used_as_mutable_card(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            paths = probe_tests.ProbeTests().make_workspace(root)
            seed = root / 'private/wide-seed.ps2'
            seed.write_bytes(b'w' * 8650752)
            seed_pin = dict(probe_tests.file_pin(seed), path='private/wide-seed.ps2')
            (root / 'docs/inputs/slice110-external-menu-pilot.json').write_text(json.dumps({'artifacts': []}))
            manifest = root / 'docs/inputs/native.json'
            manifest.write_text(json.dumps({'runtime': dict(probe_tests.file_pin(paths['runner']),
                                           path='private/donor/ps2run.exe'), 'card_seeds': [seed_pin]}))
            def execute(arguments, **options):
                copy = Path(arguments[arguments.index('--memcard') + 1])
                self.assertNotEqual(copy, seed)
                self.assertEqual(probe_tests.file_pin(copy), probe_tests.file_pin(seed))
                options['stdout'].write(b'[ee] stopped after 100 frames, 1 cycles: frame limit reached\n')
                return subprocess.CompletedProcess(arguments, 0)
            with patch.object(probe.subprocess, 'run', side_effect=execute):
                probe.run_probe(root, root / 'private/wide', 100, 1, [], seed, manifest)
            with self.assertRaisesRegex(ValueError, 'pinned evidence artifact'):
                probe.run_probe(root, root / 'private/overwrite', 0, 1, [], seed, manifest,
                                window=True, interactive=True, play_card=seed)
            seed.write_bytes(b'corrupt')
            with patch.object(probe.subprocess, 'run') as execute:
                with self.assertRaisesRegex(ValueError, 'pinned size/hash'):
                    probe.run_probe(root, root / 'private/corrupt', 100, 1, [], seed, manifest)
                execute.assert_not_called()
            self.assertFalse((root / 'private/corrupt').exists())

    def test_presentation_is_fixed_16_9_and_aspect_selection_is_removed(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            probe_tests.ProbeTests().make_workspace(root)
            def execute(arguments, **options):
                self.assertNotIn('GT4_ASPECT', options['env'])
                options['stdout'].write(b'[ee] stopped after 100 frames, 1 cycles: frame limit reached\n')
                return subprocess.CompletedProcess(arguments, 0)
            with patch.dict(probe.os.environ, {'GT4_ASPECT': 'inherited value is ignored'}), \
                 patch.object(probe.subprocess, 'run', side_effect=execute):
                result = probe.run_probe(root, root / 'private/wide', 100, 1, [])
            self.assertEqual(result['presentation_aspect'], '16:9')
            with patch.object(probe.subprocess, 'run') as execute:
                with self.assertRaises(TypeError):
                    probe.run_probe(root, root / 'private/rejected', 100, 1, [], aspect='4:3')
                execute.assert_not_called()
            self.assertFalse((root / 'private/rejected').exists())
            with patch.object(sys, 'argv', ['probe', str(root / 'private/cli'), '--aspect', '4:3']):
                with self.assertRaises(SystemExit) as refused:
                    probe.main()
            self.assertEqual(refused.exception.code, 2)
            self.assertFalse((root / 'private/cli').exists())

    def test_force_480p_is_off_by_default_and_refuses_unknown_values(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            probe_tests.ProbeTests().make_workspace(root)
            def execute(arguments, **options):
                options['stdout'].write(b'[ee] stopped after 100 frames, 1 cycles: frame limit reached\n')
                return subprocess.CompletedProcess(arguments, 0)
            with patch.dict(probe.os.environ, {}, clear=True), patch.object(probe.subprocess, 'run', side_effect=execute):
                result = probe.run_probe(root, root / 'private/off', 100, 1, [])
            self.assertNotIn('force_480p_environment', result)
            with patch.dict(probe.os.environ, {}, clear=True), patch.object(probe.subprocess, 'run', side_effect=execute) as run:
                result = probe.run_probe(root, root / 'private/forced', 100, 1, [], force_480p=True)
            self.assertEqual(result['force_480p_environment'], {'GT4_FORCE_480P': '1'})
            self.assertEqual(run.call_args.kwargs['env']['GT4_FORCE_480P'], '1')
            with patch.dict(probe.os.environ, {'GT4_FORCE_480P': '480p'}), patch.object(probe.subprocess, 'run') as run:
                with self.assertRaisesRegex(ValueError, 'GT4_FORCE_480P must be 0 or 1'):
                    probe.run_probe(root, root / 'private/unknown', 100, 1, [])
                run.assert_not_called()
            self.assertFalse((root / 'private/unknown').exists())

    def test_changed_donor_is_rejected_without_output(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            donor = root / 'private/references/ps2-web-recomp'
            source = donor / 'runtime/src/gs/gpu/gpu.cpp'
            source.parent.mkdir(parents=True)
            source.write_bytes(b'changed')
            with patch.object(prepare, 'ROOT', root):
                with self.assertRaisesRegex(ValueError, 'qualified version'):
                    prepare.prepare(root / 'private/derived')
            self.assertFalse((root / 'private/derived').exists())

    def test_public_output_and_private_root_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with patch.object(prepare, 'ROOT', root):
                for output in (root / 'public', root / 'private',
                               root / 'private/references/ps2-web-recomp'):
                    with self.assertRaises(ValueError):
                        prepare.prepare(output)

    def test_native_runtime_pin_and_window_arguments(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            paths = probe_tests.ProbeTests().make_workspace(root)
            manifest = root / 'docs/inputs/native.json'
            manifest.write_text(json.dumps({'runtime': dict(probe_tests.file_pin(paths['runner']),
                                        path='private/donor/ps2run.exe')}))
            def execute(arguments, **options):
                self.assertEqual(arguments[-1], '--window')
                options['stdout'].write(b'[ee] stopped after 100 frames, 1 cycles: frame limit reached\n')
                return subprocess.CompletedProcess(arguments, 0)
            with patch.object(probe.subprocess, 'run', side_effect=execute):
                self.assertTrue(probe.run_probe(root, root / 'private/gpu', 100, 1, [],
                                runtime_manifest=manifest, window=True)['completed_requested_frames'])
            paths['runner'].write_bytes(b'changed executable')
            with patch.object(probe.subprocess, 'run') as execute:
                with self.assertRaisesRegex(ValueError, 'pinned size/hash'):
                    probe.run_probe(root, root / 'private/reject', 100, 1, [], runtime_manifest=manifest)
                execute.assert_not_called()

    def test_changed_shader_dependency_is_rejected_before_launch(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            paths = probe_tests.ProbeTests().make_workspace(root)
            shader = root / 'private/compute.spv'
            shader.write_bytes(b'pinned shader')
            manifest = root / 'docs/inputs/native.json'
            manifest.write_text(json.dumps({
                'runtime': dict(probe_tests.file_pin(paths['runner']), path='private/donor/ps2run.exe'),
                'runtime_dependencies': [dict(probe_tests.file_pin(shader), path='private/compute.spv')]}))
            shader.write_bytes(b'changed shader')
            with patch.object(probe.subprocess, 'run') as execute:
                with self.assertRaisesRegex(ValueError, 'pinned size/hash'):
                    probe.run_probe(root, root / 'private/reject', 100, 1, [], runtime_manifest=manifest)
                execute.assert_not_called()
            self.assertFalse((root / 'private/reject').exists())

    def test_real_vulkan_fixture_if_qualified_build_exists(self):
        executable = native_executable('vulkan_fixture.exe')
        if not executable.is_file():
            self.skipTest('Private native Vulkan pilot not built on this machine')
        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('fragmented upload PSM 44', result.stderr)
        self.assertIn('PASS: real Vulkan', result.stderr)
        self.assertIn('zero-work dispatch preserves VRAM', result.stderr)
        self.assertIn('fixed 16:9 centered', result.stderr)
        self.assertIn('display 1920x1080 halved=0: all pixels match CPU', result.stderr)
        self.assertIn('display 2048x2048 halved=0: all pixels match CPU', result.stderr)

    def test_real_windows_audio_and_input_fixture_if_built(self):
        executable = native_executable('host_io_fixture.exe')
        if not executable.is_file():
            self.skipTest('Private native audio/input pilot not built on this machine')
        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('PASS: XInput mapping', result.stderr)
        self.assertIn('digital keyboard, focus, rumble', result.stderr)
        self.assertIn('completed=2592 canceled=0', result.stderr)
        self.assertIn('PASS: real Windows audio completion', result.stderr)

    def test_native_card_lock_refuses_overlapping_game_process(self):
        executable = native_executable('gt4vulkan.exe')
        if not executable.is_file():
            self.skipTest('Private native audio/input pilot not built on this machine')
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_ulong, ctypes.c_ulong,
                                      ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p]
        kernel.CreateFileW.restype = ctypes.c_void_p
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        with tempfile.TemporaryDirectory() as folder:
            card = Path(folder) / 'card.ps2'
            handle = kernel.CreateFileW(str(card) + '.lock', 0xc0000000, 0, None, 4, 128, None)
            self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
            try:
                result = subprocess.run([str(executable), 'missing.elf', '--window', '--memcard', str(card)],
                                        capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, 1)
                self.assertIn('exclusively lock game memory card: 32', result.stderr)
                self.assertFalse(card.exists())
            finally:
                kernel.CloseHandle(handle)


if __name__ == '__main__':
    unittest.main()
