"""Candidate object-delta and cross-checkout launch controls; no game execution."""

import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import ee_candidate as candidate
import external_runtime_probe as probe
import play_external_runtime as play
import test_external_runtime_probe as probe_tests


class CandidateControls(unittest.TestCase):
    def test_forged_preparation_cannot_repin_copied_baseline(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            baseline = root / 'qualified'
            (baseline / 'game-build').mkdir(parents=True)
            (baseline / 'game-build/ps2game.lib').write_bytes(b'qualified baseline')
            output = root / 'candidate'
            (output / 'baseline').mkdir(parents=True)
            library = output / 'baseline/ps2game.lib'
            library.write_bytes(b'forged baseline')
            (output / 'candidate-preparation.json').write_text(json.dumps({
                'baseline_producer': str(baseline), 'coverage_producer': str(root / 'coverage'),
                'baseline_library': probe_tests.file_pin(library)}))
            with patch.object(candidate, 'existing_producer', return_value=(output, {}, {})), \
                    patch.object(candidate, 'reviewed_sources', return_value=(baseline, None, None, {})):
                with self.assertRaisesRegex(ValueError, 'baseline pin differs'):
                    candidate.record_libraries(output)
            self.assertFalse((output / 'candidate-libraries.json').exists())

    def test_only_reviewed_units_may_change_in_complete_archive(self):
        original = [{'name': 'table.cpp.obj', 'sha256_without_timestamp': 'a'},
                    {'name': 'unit_00D3.cpp.obj', 'sha256_without_timestamp': 'b'}]
        changed = copy.deepcopy(original)
        changed[1]['sha256_without_timestamp'] = 'c'
        self.assertEqual(candidate.compare_objects(original, changed, ['unit_00D3.cpp']), ['unit_00D3.cpp.obj'])
        for wrong in (original, changed[:1], changed + [{'name': 'unexpected.obj'}]):
            with self.subTest(objects=wrong), self.assertRaises(ValueError):
                candidate.compare_objects(original, wrong, ['unit_00D3.cpp'])
        changed[0]['sha256_without_timestamp'] = 'd'
        with self.assertRaises(ValueError):
            candidate.compare_objects(original, changed, ['unit_00D3.cpp'])

    def test_cross_checkout_uses_pinned_runtime_and_keeps_assets_and_card_local(self):
        with tempfile.TemporaryDirectory() as folder:
            assets = Path(folder) / 'assets'
            runtime = Path(folder) / 'candidate'
            paths = probe_tests.ProbeTests().make_workspace(assets)
            (runtime / 'docs/inputs').mkdir(parents=True)
            (runtime / 'private/build').mkdir(parents=True)
            runner = runtime / 'private/build/candidate.exe'
            runner.write_bytes(b'candidate binary')
            shader = runtime / 'private/build/display.spv'
            shader.write_bytes(b'candidate shader')
            manifest = runtime / 'docs/inputs/candidate.json'
            evidence = {'runtime': dict(probe_tests.file_pin(runner), path='private/build/candidate.exe'),
                        'runtime_dependencies': [dict(probe_tests.file_pin(shader), path='private/build/display.spv')]}
            manifest.write_text(json.dumps(evidence))
            def execute(arguments, **options):
                self.assertEqual(Path(arguments[0]), runner)
                self.assertEqual(Path(arguments[arguments.index('--disc') + 1]), paths['disc'])
                self.assertTrue(Path(arguments[arguments.index('--memcard') + 1]).is_relative_to(assets / 'private'))
                options['stdout'].write(b'[ee] stopped after 100 frames, 1 cycles: frame limit reached\n')
                return subprocess.CompletedProcess(arguments, 0)
            with patch.object(probe.subprocess, 'run', side_effect=execute):
                result = probe.run_probe(assets, assets / 'private/control', 100, 1, [],
                                        runtime_manifest=manifest, runtime_workspace=runtime)
            self.assertTrue(result['completed_requested_frames'])
            evidence['runtime']['path'] = '../../assets/private/donor/ps2run.exe'
            manifest.write_text(json.dumps(evidence))
            with patch.object(probe.subprocess, 'run') as execute:
                with self.assertRaisesRegex(ValueError, 'Runtime executable'):
                    probe.run_probe(assets, assets / 'private/escape', 100, 1, [],
                                    runtime_manifest=manifest, runtime_workspace=runtime)
                execute.assert_not_called()
            self.assertFalse((assets / 'private/escape').exists())

    def test_candidate_manual_launch_uses_owner_card_without_reseeding(self):
        with tempfile.TemporaryDirectory() as folder:
            assets = Path(folder) / 'owner'
            runtime = Path(folder) / 'candidate'
            play_folder = assets / 'private/manual-play'
            play_folder.mkdir(parents=True)
            card = play_folder / 'card.ps2'
            card.write_bytes(b'owner data')
            manifest = runtime / 'docs/inputs/candidate.json'
            manifest.parent.mkdir(parents=True)
            manifest.write_text('{}')
            with patch.object(play, 'ROOT', runtime), patch.object(play, 'run_probe',
                    return_value={'manual_session_closed': True}) as launch:
                play.play('16:9', assets, manifest)
            self.assertEqual(launch.call_args.args[0], assets)
            self.assertIsNone(launch.call_args.args[5])
            self.assertEqual(launch.call_args.kwargs['runtime_workspace'], runtime)
            self.assertEqual(launch.call_args.kwargs['play_card'], card)
            self.assertEqual(card.read_bytes(), b'owner data')


if __name__ == '__main__':
    unittest.main()
