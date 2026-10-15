"""Pacing launcher mode (slice 153): profile pin, option forwarding, GT4_PACING and
GT4_PRESENT_MODE validation and passthrough, and manifest pins. No game is started."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import play_performance as performance
import play_external_runtime as launcher
import external_runtime_probe as probe

ROOT = Path(__file__).resolve().parents[2]
PACING_RUNTIME = 'private/slice153-pacing-build/build/gt4vulkan.exe'
PACING_MANIFEST = 'slice153-pacing.json'
PACING_SOURCE = 'slice153-pacing-runtime.json'


def pacing_evidence(**overrides):
    evidence = {'profile': 'performance', 'perf_schema_version': 1,
                'keyboard_input_fix': 'digital_only_slice136', 'capture_activation_implemented': False,
                'measurement_mode': 'pacing', 'vu_scope_profile': 'unit_split_run_fast_only',
                'runtime': {'path': PACING_RUNTIME}, 'default_card_seed': {'path': 'private/seed.ps2'},
                'standard_card_seed': {'path': 'private/seed.ps2'}}
    evidence.update(overrides)
    return evidence


class PacingProfileTests(unittest.TestCase):
    def test_profile_pins_the_pacing_binary_and_manifest(self):
        self.assertIn('pacing', probe.PERF_PROFILES)
        manifest, runtime = probe.PERF_PROFILES['pacing']
        self.assertEqual(manifest, PACING_MANIFEST)
        self.assertEqual(runtime, PACING_RUNTIME)
        self.assertIn('pacing', performance.MODES)

    def test_manifest_resolves_to_pacing_runtime_and_mode(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            manifest = root / 'docs/inputs' / PACING_MANIFEST
            self.assertEqual(probe.expected_performance_pin(manifest, root), (PACING_RUNTIME, 'pacing'))

    def test_verify_accepts_only_the_pacing_evidence_contract(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            manifest = root / 'docs/inputs' / PACING_MANIFEST
            probe.verify_performance_profile(manifest, pacing_evidence(), root)
            with self.assertRaisesRegex(ValueError, 'qualified'):
                probe.verify_performance_profile(manifest, pacing_evidence(measurement_mode='gs-async'), root)
            with self.assertRaisesRegex(ValueError, 'qualified'):
                probe.verify_performance_profile(manifest, pacing_evidence(vu_scope_profile='full'), root)
            with self.assertRaisesRegex(ValueError, 'qualified'):
                probe.verify_performance_profile(manifest, pacing_evidence(runtime={'path': 'private/other.exe'}), root)

    def test_pacing_accepts_the_aot_verify_and_interpreter_options(self):
        self.assertIn(PACING_MANIFEST, probe.VU1_AOT_MANIFESTS)


class PacingWrapperTests(unittest.TestCase):
    def run_wrapper(self, arguments, root):
        with patch.object(performance, 'ROOT', root), patch.object(performance, 'play') as play, \
             patch.object(sys, 'argv', ['perf', '--assets-root', str(root)] + arguments):
            code = performance.main()
        return code, play

    def test_pacing_mode_selects_its_manifest(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            code, play = self.run_wrapper(['--mode', 'pacing'], root)
            self.assertEqual(code, 0)
            play.assert_called_once_with(root, root / 'docs/inputs' / PACING_MANIFEST, performance=True)

    def test_verify_and_interpret_only_are_forwarded_to_pacing(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            code, play = self.run_wrapper(['--mode', 'pacing', '--vu1-verify', '50'], root)
            self.assertEqual(code, 0)
            play.assert_called_once_with(root, root / 'docs/inputs' / PACING_MANIFEST,
                                         performance=True, vu1_verify=50)
            code, play = self.run_wrapper(['--mode', 'pacing', '--vu1-interpret-only'], root)
            play.assert_called_once_with(root, root / 'docs/inputs' / PACING_MANIFEST,
                                         performance=True, vu1_interpret_only=True)

    def test_options_remain_rejected_outside_the_aot_modes(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            code, play = self.run_wrapper(['--mode', 'detailed', '--vu1-verify', '5'], root)
            self.assertEqual(code, 1)
            play.assert_not_called()


class PacingEnvironmentTests(unittest.TestCase):
    def test_valid_values_are_recorded(self):
        recorded = probe.pacing_overrides({'GT4_PACING': 'audio', 'GT4_PRESENT_MODE': 'mailbox'})
        self.assertEqual(recorded, {'pacing_environment': {'GT4_PACING': 'audio', 'GT4_PRESENT_MODE': 'mailbox'}})
        self.assertEqual(probe.pacing_overrides({'GT4_PACING': 'paced'}),
                         {'pacing_environment': {'GT4_PACING': 'paced'}})

    def test_unset_names_are_not_recorded(self):
        self.assertEqual(probe.pacing_overrides({'PATH': '/bin'}), {})

    def test_unknown_values_are_refused_before_launch(self):
        for environment in ({'GT4_PACING': 'Audio'}, {'GT4_PACING': ''}, {'GT4_PACING': 'vsync'},
                            {'GT4_PRESENT_MODE': 'vsync'}, {'GT4_PRESENT_MODE': 'FIFO'}):
            with self.assertRaisesRegex(ValueError, 'GT4_'):
                probe.pacing_overrides(environment)

    def test_performance_scrub_keeps_pacing_names(self):
        environment = {'GT4_PACING': 'audio', 'GT4_PRESENT_MODE': 'immediate', 'PS2_SCALE': '4',
                       'GT4_PERF_X': '1', 'GT4_VU1_VERIFY': '50', 'GT4_GS_SYNC': '1', 'PATH': '/bin'}
        probe.scrub_performance_environment(environment)
        self.assertEqual(environment, {'GT4_PACING': 'audio', 'GT4_PRESENT_MODE': 'immediate',
                                       'GT4_GS_SYNC': '1', 'PATH': '/bin'})


class PacingManifestTests(unittest.TestCase):
    def test_launcher_manifest_lists_the_pacing_runtime_and_nine_shaders(self):
        manifest = json.loads((ROOT / 'docs/inputs' / PACING_MANIFEST).read_text(encoding='utf-8'))
        self.assertEqual(manifest['measurement_mode'], 'pacing')
        self.assertEqual(manifest['runtime']['path'], PACING_RUNTIME)
        self.assertEqual(manifest['source_manifest'], PACING_SOURCE)
        self.assertEqual(len(manifest['runtime_dependencies']), 9)
        self.assertEqual(manifest['vu_scope_profile'], 'unit_split_run_fast_only')
        for dependency in manifest['runtime_dependencies']:
            self.assertTrue(dependency['path'].startswith('private/slice153-pacing-shaders/'))

    def test_runtime_pins_match_files_when_the_build_exists(self):
        manifest = json.loads((ROOT / 'docs/inputs' / PACING_MANIFEST).read_text(encoding='utf-8'))
        for entry in [manifest['runtime']] + manifest['runtime_dependencies']:
            path = ROOT / entry['path']
            if not path.exists():
                self.skipTest(f'Local build output not present: {entry["path"]}')
            data = path.read_bytes()
            self.assertEqual(len(data), entry['size_bytes'], entry['path'])
            self.assertEqual(hashlib.sha256(data).hexdigest(), entry['sha256'], entry['path'])

    def test_runtime_source_manifest_pins_the_adapter_and_shaders(self):
        manifest = json.loads((ROOT / 'docs/inputs' / PACING_SOURCE).read_text(encoding='utf-8'))
        self.assertEqual(manifest['slice'], 153)
        self.assertEqual(set(manifest['inputs']['PORT']), {'gpu.cpp', 'gpu.hpp', 'gs.cpp', 'main.cpp'})
        self.assertEqual(len(manifest['inputs']['SHADERS']), 9)
        port = ROOT / 'private/slice153-pacing-port'
        if port.exists():
            for name, expected in manifest['inputs']['PORT'].items():
                self.assertEqual(hashlib.sha256((port / name).read_bytes()).hexdigest(), expected, name)


if __name__ == '__main__':
    unittest.main()
