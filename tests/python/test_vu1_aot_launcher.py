"""VU1 AOT launcher modes: profile pin, option validation, and forwarding. No game is started."""
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

AOT_RUNTIME = 'private/slice151-vu1-aot-build/build/gt4vulkan.exe'


def aot_evidence(**overrides):
    evidence = {'profile': 'performance', 'perf_schema_version': 1,
                'keyboard_input_fix': 'digital_only_slice136', 'capture_activation_implemented': False,
                'measurement_mode': 'vu1-aot', 'vu_scope_profile': 'unit_split_run_fast_only',
                'runtime': {'path': AOT_RUNTIME}, 'default_card_seed': {'path': 'private/seed.ps2'},
                'standard_card_seed': {'path': 'private/seed.ps2'}}
    evidence.update(overrides)
    return evidence


class AotProfileTests(unittest.TestCase):
    def test_profile_pins_the_aot_binary_and_manifest(self):
        self.assertIn('vu1-aot', probe.PERF_PROFILES)
        manifest, runtime = probe.PERF_PROFILES['vu1-aot']
        self.assertEqual(manifest, 'slice151-vu1-aot.json')
        self.assertEqual(runtime, AOT_RUNTIME)
        self.assertIn('vu1-aot', performance.MODES)

    def test_manifest_resolves_to_aot_runtime_and_mode(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            manifest = root / 'docs/inputs/slice151-vu1-aot.json'
            self.assertEqual(probe.expected_performance_pin(manifest, root), (AOT_RUNTIME, 'vu1-aot'))

    def test_verify_accepts_only_the_aot_evidence_contract(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            manifest = root / 'docs/inputs/slice151-vu1-aot.json'
            probe.verify_performance_profile(manifest, aot_evidence(), root)
            with self.assertRaisesRegex(ValueError, 'qualified'):
                probe.verify_performance_profile(manifest, aot_evidence(measurement_mode='vu-split'), root)
            with self.assertRaisesRegex(ValueError, 'qualified'):
                probe.verify_performance_profile(manifest, aot_evidence(vu_scope_profile='full'), root)


class AotWrapperTests(unittest.TestCase):
    def run_wrapper(self, arguments, root):
        with patch.object(performance, 'ROOT', root), patch.object(performance, 'play') as play, \
             patch.object(sys, 'argv', ['perf', '--assets-root', str(root)] + arguments):
            code = performance.main()
        return code, play

    def test_aot_mode_selects_its_manifest_without_extra_options(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            code, play = self.run_wrapper(['--mode', 'vu1-aot'], root)
            self.assertEqual(code, 0)
            play.assert_called_once_with(root, root / 'docs/inputs/slice151-vu1-aot.json', performance=True)

    def test_verify_and_interpret_only_are_forwarded(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            code, play = self.run_wrapper(['--mode', 'vu1-aot', '--vu1-verify', '50'], root)
            self.assertEqual(code, 0)
            play.assert_called_once_with(root, root / 'docs/inputs/slice151-vu1-aot.json', performance=True, vu1_verify=50)
            code, play = self.run_wrapper(['--mode', 'vu1-aot', '--vu1-interpret-only'], root)
            play.assert_called_once_with(root, root / 'docs/inputs/slice151-vu1-aot.json', performance=True, vu1_interpret_only=True)

    def test_options_are_rejected_outside_the_aot_mode_and_before_play(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for arguments in (['--mode', 'detailed', '--vu1-verify', '5'],
                              ['--mode', 'vu-split', '--vu1-interpret-only'],
                              ['--mode', 'vu1-aot', '--vu1-verify', '-1'],
                              ['--mode', 'vu1-aot', '--vu1-verify', '5', '--vu1-interpret-only']):
                code, play = self.run_wrapper(arguments, root)
                self.assertEqual(code, 1, arguments)
                play.assert_not_called()


class AotPlayForwardingTests(unittest.TestCase):
    def test_play_passes_verify_period_to_the_probe(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            inputs = root / 'docs/inputs'
            inputs.mkdir(parents=True)
            manifest = inputs / 'slice151-vu1-aot.json'
            manifest.write_text(json.dumps(aot_evidence()))
            with patch.object(launcher, 'ROOT', root), patch.object(launcher, 'run_probe', return_value={'manual_session_closed': True}) as execute, \
                 patch.object(launcher, 'verify_performance_profile'):
                launcher.play(root, manifest, performance=True, vu1_verify=50)
            self.assertEqual(execute.call_args.kwargs['vu1_verify'], 50)
            self.assertFalse(execute.call_args.kwargs['vu1_interpret_only'])


if __name__ == '__main__':
    unittest.main()
