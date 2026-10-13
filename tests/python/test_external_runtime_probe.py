"""Synthetic controls for the external pilot's input and completion gates."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import external_runtime_probe as probe


def file_pin(path):
    data = path.read_bytes()
    return {'size_bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


class ProbeTests(unittest.TestCase):
    def test_interactive_mode_preserves_completion_gate_and_sets_capture_environment(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.make_workspace(root)
            def execute(arguments, **options):
                self.assertIn('--window', arguments)
                self.assertNotIn('--trace-syscalls', arguments)
                self.assertNotIn('--frames-dir', arguments)
                self.assertEqual(options['env']['PS2_DUMP_AUDIO'], str(root / 'private/play/audio.wav'))
                options['stdout'].write(b'[ee] stopped after 77 frames, 99 cycles: window closed\n')
                return subprocess.CompletedProcess(arguments, 0)
            with patch.object(probe.subprocess, 'run', side_effect=execute):
                result = probe.run_probe(root, root / 'private/play', 0, 1, [],
                                         window=True, capture_audio=True, interactive=True)
            self.assertTrue(result['manual_session_closed'])
            self.assertFalse(result['completed_requested_frames'])
            for options in ({'window': False}, {'window': True, 'presses': ['1:start']}):
                with self.assertRaisesRegex(ValueError, 'Interactive play requires'):
                    probe.run_probe(root, root / 'private/reject', 0, 1,
                                    options.get('presses', []), window=options['window'], interactive=True)

    def test_inherited_audio_capture_cannot_overwrite_external_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.make_workspace(root)
            def execute(arguments, **options):
                self.assertNotIn('PS2_DUMP_AUDIO', options['env'])
                options['stdout'].write(b'[ee] stopped after 100 frames, 9 cycles: frame limit reached\n')
                return subprocess.CompletedProcess(arguments, 0)
            with patch.dict(probe.os.environ, {'PS2_DUMP_AUDIO': 'protected.wav'}), \
                 patch.object(probe.subprocess, 'run', side_effect=execute):
                probe.run_probe(root, root / 'private/audio', 100, 1, [])

    def test_persistent_manual_card_is_backed_up_and_never_reseeded(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.make_workspace(root)
            (root / 'docs/inputs/slice110-external-menu-pilot.json').write_text(json.dumps({'artifacts': []}))
            card = root / 'private/manual/card.ps2'
            card.parent.mkdir()
            original = b'a' * 8650752
            card.write_bytes(original)
            def execute(arguments, **options):
                self.assertEqual(arguments[arguments.index('--memcard') + 1], str(card))
                card.write_bytes(b'b' * 8650752)
                options['stdout'].write(b'[ee] stopped after 77 frames, 99 cycles: window closed\n')
                return subprocess.CompletedProcess(arguments, 0)
            with patch.object(probe.subprocess, 'run', side_effect=execute):
                result = probe.run_probe(root, root / 'private/play', 0, 1, [],
                                         window=True, interactive=True, play_card=card)
            self.assertEqual((root / 'private/play/card-before.ps2').read_bytes(), original)
            self.assertNotEqual(result['play_card_before'], result['play_card_after'])
            self.assertEqual(card.read_bytes(), b'b' * 8650752)
            evidence = root / 'docs/inputs/slice110-external-menu-pilot.json'
            evidence.write_text(json.dumps({'artifacts': [{'path': card.relative_to(root).as_posix()}]}))
            with self.assertRaisesRegex(ValueError, 'pinned evidence artifact'):
                probe.run_probe(root, root / 'private/protected', 0, 1, [], window=True,
                                interactive=True, play_card=card)
            self.assertFalse((root / 'private/protected').exists())
            self.assertEqual(card.read_bytes(), b'b' * 8650752)
            card.write_bytes(b'bad size')
            with self.assertRaisesRegex(ValueError, 'unexpected size'):
                probe.run_probe(root, root / 'private/reject', 0, 1, [], window=True, interactive=True, play_card=card)

    def make_workspace(self, root):
        inputs = root / 'docs/inputs'
        inputs.mkdir(parents=True)
        paths = {
            'runner': 'private/donor/ps2run.exe',
            'core': 'private/fingerprint-check/CORE.GT4',
            'elf': 'private/reconstructed/SCUS_973.28.elf',
            'disc': 'Gran Turismo 4 (USA) (v2.00).iso',
        }
        for name, relative in paths.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(('synthetic ' + name).encode())
            paths[name] = path
        executable = dict(file_pin(paths['runner']), path='private/donor/ps2run.exe')
        (inputs / 'external-recomp-references.json').write_text(json.dumps({'artifacts': [executable]}))
        elf = file_pin(paths['elf'])
        (inputs / 'usa-v2.00-native.json').write_text(json.dumps({
            'native_elf_size': elf['size_bytes'], 'native_elf_sha256': elf['sha256']}))
        (inputs / 'usa-v2.00.json').write_text(json.dumps({
            'disc': file_pin(paths['disc']), 'files': {'/CORE.GT4;1': file_pin(paths['core'])}}))
        return paths

    def test_changed_inputs_are_rejected_before_launch_or_output(self):
        for changed in ('runner', 'core', 'elf', 'disc'):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                paths = self.make_workspace(root)
                paths[changed].write_bytes(b'changed')
                output = root / 'private/new-run'
                with patch.object(probe.subprocess, 'run') as execute:
                    with self.assertRaisesRegex(ValueError, 'pinned size/hash'):
                        probe.run_probe(root, output, 100, 2, [])
                    execute.assert_not_called()
                self.assertFalse(output.exists())

    def test_existing_output_and_outside_private_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.make_workspace(root)
            existing = root / 'private/existing'
            existing.mkdir()
            (existing / 'evidence.txt').write_text('keep')
            for output in (existing, root / 'public-run', root / 'private'):
                with self.subTest(output=output), patch.object(probe.subprocess, 'run') as execute:
                    with self.assertRaises(ValueError):
                        probe.run_probe(root, output, 100, 2, [])
                    execute.assert_not_called()
            self.assertEqual((existing / 'evidence.txt').read_text(), 'keep')

    def test_zero_exit_is_not_completion_when_guest_stopped_early(self):
        stopped = '[ee] stopped after 12 frames, 55 cycles: missing code at 0x00102000\n'
        self.assertFalse(probe.summarize_exit(stopped, 100, 0, False)['completed_requested_frames'])
        self.assertFalse(probe.summarize_exit('', 100, 0, False)['completed_requested_frames'])
        complete = '[ee] stopped after 100 frames, 5500 cycles: frame limit reached\n'
        self.assertTrue(probe.summarize_exit(complete, 100, 0, False)['completed_requested_frames'])
        self.assertFalse(probe.summarize_exit(complete, 100, 0, True)['completed_requested_frames'])
        self.assertFalse(probe.summarize_exit(complete, 100, 9, False)['completed_requested_frames'])

    def test_arguments_keep_disc_path_and_presses_as_literal_values(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            paths = self.make_workspace(root)
            output = root / 'private/new-run'

            def execute(arguments, **options):
                self.assertEqual(arguments[arguments.index('--disc') + 1], str(paths['disc']))
                self.assertEqual(arguments[-2:], ['--press', '30:left+cross:6'])
                self.assertEqual(Path(arguments[1]).name, 'SCUS_973.28')
                self.assertNotIn('shell', options)
                self.assertFalse((output / 'card.ps2').exists())
                options['stdout'].write(b'[ee] stopped after 100 frames, 5500 cycles: frame limit reached\n')
                return subprocess.CompletedProcess(arguments, 0)

            with patch.object(probe.subprocess, 'run', side_effect=execute):
                result = probe.run_probe(root, output, 100, 2, ['30:left+cross:6'])
            self.assertTrue(result['completed_requested_frames'])
            self.assertEqual((output / 'SCUS_973.28').read_bytes(), paths['elf'].read_bytes())
            self.assertEqual(json.loads((output / 'result.json').read_text())['reported_frames'], 100)

    def test_corrupted_elf_copy_is_rejected_before_launch(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.make_workspace(root)
            output = root / 'private/new-run'

            def corrupt_copy(source, destination):
                destination.write_bytes(b'corrupt copy')

            with patch.object(probe.shutil, 'copyfile', side_effect=corrupt_copy):
                with patch.object(probe.subprocess, 'run') as execute:
                    with self.assertRaisesRegex(ValueError, 'pinned size/hash'):
                        probe.run_probe(root, output, 100, 2, [])
                    execute.assert_not_called()

    def test_pinned_card_seed_is_copied_and_original_is_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.make_workspace(root)
            seed = root / 'private/seed.ps2'
            seed.write_bytes(b'synthetic saved profile')
            pin = dict(file_pin(seed), path='private/seed.ps2')
            (root / 'docs/inputs/slice110-external-menu-pilot.json').write_text(
                json.dumps({'artifacts': [pin]}))

            def execute(arguments, **options):
                card = Path(arguments[arguments.index('--memcard') + 1])
                self.assertEqual(card.read_bytes(), seed.read_bytes())
                card.write_bytes(b'guest writes copied card')
                options['stdout'].write(b'[ee] stopped after 100 frames, 5500 cycles: frame limit reached\n')
                return subprocess.CompletedProcess(arguments, 0)

            with patch.object(probe.subprocess, 'run', side_effect=execute):
                result = probe.run_probe(root, root / 'private/reboot', 100, 2, [], seed)
            self.assertTrue(result['completed_requested_frames'])
            self.assertEqual(result['card_seed'], pin)
            self.assertEqual(seed.read_bytes(), b'synthetic saved profile')
            seed.write_bytes(b'changed seed')
            with patch.object(probe.subprocess, 'run') as execute:
                with self.assertRaisesRegex(ValueError, 'pinned size/hash'):
                    probe.run_probe(root, root / 'private/changed-seed', 100, 2, [], seed)
                execute.assert_not_called()
            self.assertFalse((root / 'private/changed-seed').exists())
            for invalid_seed, message in ((root / 'outside.ps2', 'inside private'),
                                          (root / 'private/unpinned.ps2', 'no pinned artifact')):
                with self.subTest(seed=invalid_seed), patch.object(probe.subprocess, 'run') as execute:
                    with self.assertRaisesRegex(ValueError, message):
                        probe.run_probe(root, root / 'private/invalid-seed', 100, 2, [], invalid_seed)
                    execute.assert_not_called()
                self.assertFalse((root / 'private/invalid-seed').exists())

    def test_timeout_preserves_log_and_reports_incomplete_run(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.make_workspace(root)
            output = root / 'private/new-run'

            def execute(arguments, **options):
                options['stdout'].write(b'partial guest log\n')
                raise subprocess.TimeoutExpired(arguments, options['timeout'])

            with patch.object(probe.subprocess, 'run', side_effect=execute):
                result = probe.run_probe(root, output, 100, 2, [])
            self.assertTrue(result['timed_out'])
            self.assertFalse(result['completed_requested_frames'])
            self.assertEqual((output / 'probe.log').read_text(), 'partial guest log\n')


if __name__ == '__main__':
    unittest.main()
