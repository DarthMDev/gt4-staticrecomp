"""TASK-011 step 0 (slice169): MTVU measurement capsule, launcher mode, switch and pins.

Synthetic checks always run. Real-file checks read the donor, the slice160/slice169 capsules and the
built executable from the assets checkout (GT4_ASSETS_ROOT, default this checkout) and are skipped when
those private files are absent. The counting logic itself is covered by CTest native_mtvu_measure.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import external_runtime_probe as probe
import play_performance as performance
import prepare_perf_runtime as producer
import test_external_runtime_probe as probe_tests  # module import: its TestCase is not collected twice

ASSETS = Path(os.environ.get('GT4_ASSETS_ROOT', ROOT))
MANIFEST = 'slice169-mtvu-measure.json'
RUNTIME_MANIFEST = 'slice169-mtvu-measure-runtime.json'


class MtvuMeasureDerivationTests(unittest.TestCase):
    def test_hooks_are_additions_that_reverse_to_the_input(self):
        for name, sites in producer.MTVU_MEASURE_PATCHES.items():
            source = 'head\n' + ''.join(before + 'middle\n' for before, _ in sites) + 'tail\n'
            derived = producer.apply_mtvu_measure(name, source)
            self.assertGreater(derived.count('gt4::mtvu::'), 0, name)
            # Every hook keeps the donor text it is attached to.
            for before, after in sites:
                for line in before.splitlines():
                    self.assertIn(line, after, name)
            with self.assertRaisesRegex(ValueError, 'one qualified MTVU measure site'):
                producer.apply_mtvu_measure(name, 'nothing here\n')

    def test_option_off_leaves_the_derivation_unchanged(self):
        source = '\n'.join(before for before, _ in producer.PATCHES['system.cpp'])
        source += '\nvoid vblank() {\n    ++g_frames;\n    vif::stats();\n'
        plain = producer.derive('system.cpp', source)
        self.assertNotIn('mtvu', plain)
        measured = producer.derive('system.cpp', source, mtvu_measure=True)
        self.assertTrue(measured.startswith(producer.PREFIX + producer.MTVU_MEASURE_PREFIX))
        self.assertIn('gt4::mtvu::on_vblank();', measured)
        self.assertIn('gt4::mtvu::final_report(stderr);', measured)
        hooks_removed = (measured.replace(producer.MTVU_MEASURE_PREFIX, '', 1)
                         .replace('    gt4::mtvu::on_vblank();\n', '')
                         .replace('    gt4::mtvu::final_report(stderr);\n', ''))
        self.assertEqual(hooks_removed, plain)

    def test_capsule_requires_the_assets_private_parent(self):
        with tempfile.TemporaryDirectory() as folder:
            assets = Path(folder).resolve()
            (assets / 'private').mkdir()
            with self.assertRaisesRegex(ValueError, 'immediate private child'):
                producer.prepare(assets / 'elsewhere', assets, mtvu_measure=True)


class MtvuMeasureRealFileTests(unittest.TestCase):
    donor = ASSETS / 'private/references/ps2-web-recomp/runtime/src'

    def setUp(self):
        if not self.donor.is_dir():
            self.skipTest('donor checkout not present')

    def derived(self, **options):
        result = {}
        for name in producer.PINS:
            text = (self.donor / name).read_bytes().decode('utf-8').replace('\r\n', '\n')
            result[name] = producer.derive(name, text, **options).encode('utf-8')
        return result

    def test_without_the_option_the_slice160_capsule_is_reproduced(self):
        capsule = ASSETS / 'private/slice160-vu0-runtime'
        if not (capsule / 'runtime.json').exists():
            self.skipTest('slice160 capsule not present')
        for name, data in self.derived(split_vu=True, vu1_aot=True, vu0_aot=True).items():
            self.assertEqual(data, (capsule / name).read_bytes(), name)

    def test_slice169_capsule_matches_derivation_and_pin(self):
        capsule = ASSETS / 'private/slice169-mtvu-measure-runtime'
        if not (capsule / 'runtime.json').exists():
            self.skipTest('slice169 capsule not present')
        pin = json.loads((ROOT / 'docs/inputs' / RUNTIME_MANIFEST).read_text(encoding='utf-8'))
        self.assertEqual(hashlib.sha256((capsule / 'runtime.json').read_bytes()).hexdigest(), pin['receipt_sha256'])
        derived = self.derived(split_vu=True, vu1_aot=True, vu0_aot=True, mtvu_measure=True)
        for name, (donor_path, expected) in producer.MTVU_MEASURE_SOURCES.items():
            data = (self.donor / donor_path).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), expected, donor_path)
            derived[name] = producer.derive_mtvu_source(
                name, data.decode('utf-8').replace('\r\n', '\n')).encode('utf-8')
        self.assertEqual(set(pin['source_sha256']), set(derived))
        for name, data in derived.items():
            self.assertEqual(data, (capsule / name).read_bytes(), name)
            self.assertEqual(hashlib.sha256(data).hexdigest(), pin['source_sha256'][name], name)


class MtvuMeasureLauncherTests(unittest.TestCase):
    def test_mode_selects_slice169_and_accepts_main_options(self):
        manifest, runtime = probe.PERF_PROFILES['mtvu-measure']
        self.assertEqual(manifest, MANIFEST)
        self.assertEqual(runtime, 'private/slice169-mtvu-measure-build/build/gt4vulkan.exe')
        self.assertIn(MANIFEST, probe.VU1_AOT_MANIFESTS)
        self.assertIn(MANIFEST, probe.VU0_AOT_MANIFESTS)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            arguments = ['perf', '--assets-root', str(root), '--mode', 'mtvu-measure', '--force-480p',
                         '--vu1-verify', '50', '--vu0-verify', '50']
            with patch.object(performance, 'play') as play, patch.object(sys, 'argv', arguments):
                self.assertEqual(performance.main(), 0)
            options = play.call_args.kwargs
            self.assertEqual((options['force_480p'], options['vu1_verify'], options['vu0_verify']), (True, 50, 50))

    def test_pinned_manifest_is_the_qualified_performance_profile(self):
        evidence = json.loads((ROOT / 'docs/inputs' / MANIFEST).read_text(encoding='utf-8'))
        self.assertEqual(evidence['measurement_mode'], 'mtvu-measure')
        self.assertEqual(evidence['runtime']['path'], probe.PERF_PROFILES['mtvu-measure'][1])
        probe.verify_performance_profile((ROOT / 'docs/inputs' / MANIFEST).resolve(), evidence, ROOT)
        executable = ASSETS / evidence['runtime']['path']
        if executable.exists():
            self.assertEqual(executable.stat().st_size, evidence['runtime']['size_bytes'])
            self.assertEqual(hashlib.sha256(executable.read_bytes()).hexdigest(), evidence['runtime']['sha256'])

    def test_switch_values_are_refused_before_output_and_recorded(self):
        helper = probe_tests.ProbeTests()
        for value in ('2', 'on', ''):
            with self.subTest(value=value), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                helper.make_workspace(root)
                with patch.dict(os.environ, {'GT4_MTVU_MEASURE': value}), \
                        patch.object(probe.subprocess, 'run') as execute:
                    with self.assertRaisesRegex(ValueError, 'GT4_MTVU_MEASURE'):
                        probe.run_probe(root, root / 'private/run', 10, 1, [])
                    execute.assert_not_called()
                self.assertFalse((root / 'private/run').exists())
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            helper.make_workspace(root)
            def execute(arguments, **options):
                self.assertEqual(options['env']['GT4_MTVU_MEASURE'], '0')
                options['stdout'].write(b'[ee] stopped after 10 frames, 1 cycles: frame limit reached\n')
                return subprocess.CompletedProcess(arguments, 0)
            with patch.dict(os.environ, {'GT4_MTVU_MEASURE': '0', 'GT4_MTVU_MEASURE_EVERY': '600'}), \
                    patch.object(probe.subprocess, 'run', side_effect=execute):
                result = probe.run_probe(root, root / 'private/run', 10, 1, [])
            self.assertEqual(result['mtvu_measure_environment'],
                             {'GT4_MTVU_MEASURE': '0', 'GT4_MTVU_MEASURE_EVERY': '600'})

    def test_worktree_without_private_takes_binaries_from_the_assets_checkout(self):
        with tempfile.TemporaryDirectory() as folder:
            worktree, assets = Path(folder) / 'worktree', Path(folder) / 'assets'
            worktree.mkdir()
            (assets / 'private').mkdir(parents=True)
            self.assertEqual(probe.binary_workspace_for(worktree, assets), assets.resolve())
            (worktree / 'private').mkdir()
            self.assertEqual(probe.binary_workspace_for(worktree, assets), worktree.resolve())

    def test_ctest_and_window_wiring(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
        self.assertIn('add_test(NAME native_mtvu_measure COMMAND $<TARGET_FILE:mtvu_measure_fixture>)', cmake)
        window = (ROOT / 'native/window.cpp').read_text(encoding='utf-8')
        self.assertIn('gt4::mtvu::report(output, "interval");', window)
        self.assertIn('gt4::mtvu::request_interval_reset();', window)


if __name__ == '__main__':
    unittest.main()
