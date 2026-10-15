"""paraLLEl-GS launcher mode (slice 168, TASK-012): profile pin, --renderer and --ssaa forwarding and
refusals, the renderer environment given to the binary, pinned artifacts resolved in the assets checkout,
and the launcher manifest. No game is started."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import play_performance as performance
import external_runtime_probe as probe
import test_external_runtime_probe as probe_tests

ROOT = Path(__file__).resolve().parents[2]
# Builds live in the main checkout's private/ also when the tests run in a worktree.
MAIN_CHECKOUT = Path('C:/Antigravity/gt4-staticrecomp')
# Slice 169: the parallel mode points at the high-resolution build; slice168 pins stay as history.
PARALLEL_RUNTIME = 'private/slice169-parallel-build/build/gt4vulkan.exe'
PARALLEL_MANIFEST = 'slice169-parallel.json'
PARALLEL_SOURCE = 'slice169-parallel-runtime.json'


class ParallelProfileTests(unittest.TestCase):
    def test_profile_pins_the_parallel_binary_and_manifest(self):
        self.assertEqual(probe.PERF_PROFILES['parallel'], (PARALLEL_MANIFEST, PARALLEL_RUNTIME))
        self.assertIn('parallel', performance.MODES)
        self.assertIn(PARALLEL_MANIFEST, probe.VU1_AOT_MANIFESTS)
        self.assertIn(PARALLEL_MANIFEST, probe.VU0_AOT_MANIFESTS)
        self.assertEqual(probe.RENDERER_MANIFESTS, ('slice168-parallel.json', PARALLEL_MANIFEST))
        self.assertEqual(probe.HIGH_RESOLUTION_MANIFESTS, (PARALLEL_MANIFEST,))

    def test_scrub_removes_inherited_renderer_choices(self):
        environment = {'GT4_RENDERER': 'donor', 'GT4_PGS_SSAA': '16', 'GT4_PGS_CAPTURE': 'x', 'PATH': '/bin'}
        probe.scrub_performance_environment(environment)
        self.assertEqual(environment, {'PATH': '/bin'})


class ParallelWrapperTests(unittest.TestCase):
    def run_wrapper(self, arguments, root):
        with patch.object(performance, 'ROOT', root), patch.object(performance, 'play') as play, \
             patch.object(sys, 'argv', ['perf', '--assets-root', str(root)] + arguments):
            code = performance.main()
        return code, play

    def test_parallel_mode_defaults_to_the_parallel_renderer(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            code, play = self.run_wrapper(['--mode', 'parallel'], root)
            self.assertEqual(code, 0)
            # The probe defaults the parallel manifest to GT4_RENDERER=parallel (ParallelProbeTests).
            play.assert_called_once_with(root, root / 'docs/inputs' / PARALLEL_MANIFEST, performance=True)

    def test_donor_renderer_and_main_options_are_forwarded(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            code, play = self.run_wrapper(['--mode', 'parallel', '--renderer', 'donor', '--force-480p',
                                           '--vu0-verify', '50', '--vu1-verify', '50'], root)
            self.assertEqual(code, 0)
            play.assert_called_once_with(root, root / 'docs/inputs' / PARALLEL_MANIFEST, performance=True,
                                         renderer='donor', force_480p=True, vu0_verify=50, vu1_verify=50)

    def test_super_sampling_is_forwarded_with_the_parallel_renderer(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            code, play = self.run_wrapper(['--mode', 'parallel', '--ssaa', '4'], root)
            self.assertEqual(code, 0)
            play.assert_called_once_with(root, root / 'docs/inputs' / PARALLEL_MANIFEST, performance=True,
                                         super_sampling=4)

    def test_high_resolution_is_forwarded_with_4x_8x_16x(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for factor in (4, 8, 16):
                code, play = self.run_wrapper(['--mode', 'parallel', '--ssaa', str(factor), '--hires'], root)
                self.assertEqual(code, 0)
                play.assert_called_once_with(root, root / 'docs/inputs' / PARALLEL_MANIFEST, performance=True,
                                             super_sampling=factor, high_resolution=True)

    def test_high_resolution_is_refused_without_enough_super_sampling(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for arguments in (['--mode', 'parallel', '--hires'], ['--mode', 'parallel', '--ssaa', '2', '--hires'],
                              ['--mode', 'parallel', '--renderer', 'donor', '--hires'],
                              ['--mode', 'main', '--hires']):
                with self.subTest(arguments=arguments):
                    code, play = self.run_wrapper(arguments, root)
                    self.assertEqual(code, 1)
                    play.assert_not_called()

    def test_renderer_options_are_refused_before_play(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for arguments in (['--mode', 'main', '--renderer', 'parallel'], ['--mode', 'main', '--ssaa', '4'],
                              ['--mode', 'parallel', '--renderer', 'donor', '--ssaa', '4'],
                              ['--mode', 'parallel', '--ssaa', '3'], ['--mode', 'parallel', '--ssaa', '0']):
                with self.subTest(arguments=arguments):
                    code, play = self.run_wrapper(arguments, root)
                    self.assertEqual(code, 1)
                    play.assert_not_called()


class ParallelProbeTests(unittest.TestCase):
    """Runs the probe on a synthetic workspace with the parallel manifest and checks the environment."""
    make_workspace = probe_tests.ProbeTests.make_workspace
    performance_manifest = probe_tests.ProbeTests.performance_manifest

    def launch(self, root, **options):
        self.make_workspace(root)
        manifest, _ = self.performance_manifest(root, 'parallel')
        seen = {}

        def execute(arguments, **kwargs):
            seen['arguments'] = arguments
            seen['environment'] = kwargs['env']
            kwargs['stdout'].write(b'[ee] stopped after 5 frames, 2 cycles: window closed\n')
            return subprocess.CompletedProcess(arguments, 0)
        with patch.object(probe.subprocess, 'run', side_effect=execute), \
             patch.dict(os.environ, {'GT4_RENDERER': 'donor', 'GT4_PGS_SSAA': '16', 'GT4_PGS_CAPTURE': 'x'}):
            result = probe.run_probe(root, root / 'private/run', 0, 1, [], runtime_manifest=manifest,
                                     window=True, interactive=True, performance=True, **options)
        return result, seen

    def test_default_run_selects_parallel_and_drops_inherited_choices(self):
        with tempfile.TemporaryDirectory() as folder:
            result, seen = self.launch(Path(folder))
            self.assertEqual(seen['environment']['GT4_RENDERER'], 'parallel')
            self.assertNotIn('GT4_PGS_SSAA', seen['environment'])
            self.assertNotIn('GT4_PGS_CAPTURE', seen['environment'])
            self.assertEqual(result['renderer_environment'], {'GT4_RENDERER': 'parallel'})

    def test_donor_and_super_sampling_choices_reach_the_binary(self):
        with tempfile.TemporaryDirectory() as folder:
            result, seen = self.launch(Path(folder), renderer='donor')
            self.assertEqual(seen['environment']['GT4_RENDERER'], 'donor')
            self.assertEqual(result['renderer_environment'], {'GT4_RENDERER': 'donor'})
        with tempfile.TemporaryDirectory() as folder:
            result, seen = self.launch(Path(folder), super_sampling=8)
            self.assertEqual(seen['environment']['GT4_PGS_SSAA'], '8')
            self.assertEqual(result['renderer_environment'], {'GT4_RENDERER': 'parallel', 'GT4_PGS_SSAA': '8'})
        with tempfile.TemporaryDirectory() as folder:
            result, seen = self.launch(Path(folder), super_sampling=16, high_resolution=True)
            self.assertEqual(seen['environment']['GT4_PGS_HIRES'], '1')
            self.assertEqual(result['renderer_environment'],
                             {'GT4_RENDERER': 'parallel', 'GT4_PGS_SSAA': '16', 'GT4_PGS_HIRES': '1'})

    def test_invalid_renderer_choices_are_refused_before_output(self):
        for options in ({'renderer': 'software'}, {'renderer': 'donor', 'super_sampling': 4}, {'super_sampling': 3},
                        {'high_resolution': True}, {'super_sampling': 2, 'high_resolution': True}):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                with self.assertRaisesRegex(ValueError, 'renderer|ssaa|hires'):
                    self.launch(root, **options)
                self.assertFalse((root / 'private/run').exists())

    def test_renderer_is_refused_for_single_renderer_manifests(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.make_workspace(root)
            manifest, _ = self.performance_manifest(root, 'main')
            with patch.object(probe.subprocess, 'run') as execute, self.assertRaisesRegex(ValueError, 'renderer'):
                probe.run_probe(root, root / 'private/run', 0, 1, [], runtime_manifest=manifest, window=True,
                                interactive=True, performance=True, renderer='parallel')
            execute.assert_not_called()

    def test_artifacts_resolve_in_the_artifact_workspace(self):
        # A launcher in a git worktree (runtime_workspace) starts the binary pinned in the assets checkout.
        with tempfile.TemporaryDirectory() as assets_folder, tempfile.TemporaryDirectory() as tree_folder:
            assets, tree = Path(assets_folder), Path(tree_folder)
            self.make_workspace(assets)
            manifest, evidence = self.performance_manifest(assets, 'parallel')
            (tree / 'docs/inputs').mkdir(parents=True)
            tree_manifest = tree / 'docs/inputs' / manifest.name
            tree_manifest.write_text(manifest.read_text())
            seen = {}

            def execute(arguments, **kwargs):
                seen['executable'] = Path(arguments[0])
                kwargs['stdout'].write(b'[ee] stopped after 5 frames, 2 cycles: window closed\n')
                return subprocess.CompletedProcess(arguments, 0)
            with patch.object(probe.subprocess, 'run', side_effect=execute):
                probe.run_probe(assets, assets / 'private/run', 0, 1, [], runtime_manifest=tree_manifest,
                                window=True, interactive=True, performance=True, runtime_workspace=tree,
                                artifact_workspace=assets)
            self.assertEqual(seen['executable'], assets / evidence['runtime']['path'])


class ParallelManifestTests(unittest.TestCase):
    def test_launcher_manifest_pins_the_parallel_runtime_and_shaders(self):
        manifest = json.loads((ROOT / 'docs/inputs' / PARALLEL_MANIFEST).read_text(encoding='utf-8'))
        self.assertEqual(manifest['measurement_mode'], 'parallel')
        self.assertEqual(manifest['runtime']['path'], PARALLEL_RUNTIME)
        self.assertEqual(manifest['source_manifest'], PARALLEL_SOURCE)
        self.assertEqual(manifest['vu_scope_profile'], 'unit_split_run_fast_only')
        self.assertEqual(len(manifest['runtime_dependencies']), 9)
        for dependency in manifest['runtime_dependencies']:
            self.assertTrue(dependency['path'].startswith('private/slice169-parallel-shaders/'))

    def test_runtime_pins_match_files_when_the_build_exists(self):
        manifest = json.loads((ROOT / 'docs/inputs' / PARALLEL_MANIFEST).read_text(encoding='utf-8'))
        for entry in [manifest['runtime']] + manifest['runtime_dependencies']:
            path = next((root / entry['path'] for root in (ROOT, MAIN_CHECKOUT) if (root / entry['path']).exists()),
                        ROOT / entry['path'])
            if not path.exists():
                self.skipTest(f'Local build output not present: {entry["path"]}')
            data = path.read_bytes()
            self.assertEqual(len(data), entry['size_bytes'], entry['path'])
            self.assertEqual(hashlib.sha256(data).hexdigest(), entry['sha256'], entry['path'])

    def test_runtime_source_manifest_pins_the_parallel_port(self):
        manifest = json.loads((ROOT / 'docs/inputs' / PARALLEL_SOURCE).read_text(encoding='utf-8'))
        self.assertEqual(manifest['slice'], 169)
        self.assertEqual(set(manifest['inputs']['PORT']),
                         {'gif.cpp', 'gpu.cpp', 'gpu.hpp', 'gs.cpp', 'hw.cpp', 'main.cpp'})
        self.assertEqual(len(manifest['inputs']['SHADERS']), 9)
        self.assertEqual(manifest['source_capsule'], 'private/slice160-vu0-runtime')


if __name__ == '__main__':
    unittest.main()
