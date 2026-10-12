"""Native pilot trust boundaries and optional real GPU fixture."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import prepare_external_vulkan as prepare
import external_runtime_probe as probe
import test_external_runtime_probe as probe_tests


class NativeVulkanTests(unittest.TestCase):
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
        executable = prepare.ROOT / 'private/slice111-vulkan/build/vulkan_fixture.exe'
        if not executable.is_file():
            self.skipTest('Private native Vulkan pilot not built on this machine')
        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('fragmented upload PSM 44', result.stderr)
        self.assertIn('PASS: real Vulkan', result.stderr)


if __name__ == '__main__':
    unittest.main()
