"""TASK-035: GS stream seam derivations, launcher wiring and pins (docs/GS-STREAM-PROTOCOL.md).

Pure checks run everywhere. Checks on the real donor sources and the slice191 capsules use the assets checkout
(GT4_ASSETS_ROOT, default this checkout) and are skipped when it has no private/ inputs. Pin checks against the
built files use GT4_NATIVE_BUILD (the slice191 build directory) and are skipped for other builds.
"""
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import external_runtime_probe as probe  # noqa: E402
import play_performance as performance  # noqa: E402
import prepare_external_vulkan as port  # noqa: E402
import prepare_perf_runtime as runtime  # noqa: E402

ASSETS = Path(os.environ.get('GT4_ASSETS_ROOT', ROOT))
DONOR = ASSETS / 'private/references/ps2-web-recomp'
MANIFEST = 'slice191-pcsx2-stream.json'
RUNTIME_PIN = 'slice191-gs-stream-runtime.json'
EXECUTABLE = 'private/slice191-gs-stream-build/build/gt4vulkan.exe'
RECORDER = 'private/slice191-gs-stream-build/build/gs_stream_record.exe'


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class SeamDerivationTests(unittest.TestCase):
    def test_gs_cpp_hooks_are_unique_and_reversible_on_a_synthetic_source(self):
        source = ''.join(anchor + '// filler\n' for anchor, _ in port.GS_STREAM_HOOKS)
        derived = port.apply_gs_stream_hooks(source)
        self.assertIn('#include "gs_stream_client.hpp"', derived)
        self.assertEqual(derived.count('::gt4::gs_stream::'), sum(r.count('::gt4::gs_stream::') for _, r in port.GS_STREAM_HOOKS))
        with self.assertRaisesRegex(ValueError, 'anchor changed'):
            port.apply_gs_stream_hooks(source + port.GS_STREAM_HOOKS[3][0])  # duplicated anchor

    def test_runtime_hooks_are_unique_and_reversible_on_a_synthetic_source(self):
        for name, sites in runtime.GS_STREAM_PATCHES.items():
            source = ''.join(before + '// filler\n' for before, _ in sites)
            derived = runtime.apply_gs_stream(name, source)
            self.assertIn('gt4::gs_stream::', derived)
            with self.assertRaisesRegex(ValueError, 'GS stream site'):
                runtime.apply_gs_stream(name, source.replace(sites[0][0], ''))

    def test_gs_stream_option_implies_mtvu_and_records_the_profile(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            with patch.object(runtime, 'prepare', wraps=runtime.prepare):
                with self.assertRaises((ValueError, OSError)):
                    # No donor in the temporary root: the derivation stops before any output.
                    runtime.prepare(root / 'private/capsule', root, gs_stream=True)
            self.assertFalse((root / 'private/capsule').exists())

    def test_real_derivations_reproduce_the_slice191_capsules(self):
        port_capsule = ASSETS / 'private/slice191-gs-stream-port'
        runtime_capsule = ASSETS / 'private/slice191-gs-stream-runtime'
        if not DONOR.exists() or not (runtime_capsule / 'runtime.json').exists():
            self.skipTest('donor or slice191 capsules not present')
        graphics = (DONOR / 'runtime/src/gs/gs.cpp').read_text(encoding='utf-8')
        graphics = graphics.replace('"../hw.hpp"', '"hw.hpp"').replace('"gpu/gpu.hpp"', '"gpu.hpp"')
        # The port writer uses text mode (CRLF on Windows); compare the text.
        self.assertEqual(port.apply_gs_stream_hooks(graphics), (port_capsule / 'gs.cpp').read_text(encoding='utf-8'))
        for name in ('gif.cpp', 'hw.cpp'):
            donor_path, _ = runtime.MTVU_RUNTIME_SOURCES[name]
            text = (DONOR / 'runtime/src' / donor_path).read_bytes().decode('utf-8').replace('\r\n', '\n')
            derived = runtime.derive_mtvu_runtime_source(name, text, gs_stream=True)
            self.assertEqual(derived.encode('utf-8'), (runtime_capsule / name).read_bytes(), name)
            # Without the option the derivation is the slice178 main capsule, byte for byte.
            main = ASSETS / 'private/slice178-main-runtime' / name
            if main.exists():
                self.assertEqual(runtime.derive_mtvu_runtime_source(name, text).encode('utf-8'), main.read_bytes(), name)
        receipt = json.loads((runtime_capsule / 'runtime.json').read_text(encoding='utf-8'))
        self.assertEqual(receipt['gs_stream_profile'], 'task035-seam-v1')
        self.assertEqual(receipt['gs_stream_sources'], ['gif.cpp', 'hw.cpp'])


class LauncherTests(unittest.TestCase):
    def evidence(self, **stream):
        block = dict({'renderer': 'pcsx2', 'gs_process': RECORDER}, **stream)
        return {'gs_stream': block, 'runtime_dependencies': [{'path': RECORDER}]}

    def test_mode_selects_the_slice191_pin(self):
        self.assertEqual(probe.PERF_PROFILES['pcsx2-stream'], (MANIFEST, EXECUTABLE))
        self.assertIn(MANIFEST, probe.VU1_AOT_MANIFESTS)
        self.assertIn(MANIFEST, probe.VU0_AOT_MANIFESTS)
        self.assertIn('pcsx2-stream', performance.MODES)

    def test_performance_run_forces_the_manifest_renderer_and_pinned_process(self):
        environment = {'GT4_RENDERER': 'donor', 'GT4_GS_PROCESS': 'C:/other.exe', 'GT4_GS_RECORD_OUTPUT': 'x.gscap'}
        recorded = probe.gs_stream_environment(environment, self.evidence(), Path('C:/assets'), performance=True)
        self.assertEqual(environment['GT4_RENDERER'], 'pcsx2')
        self.assertEqual(Path(environment['GT4_GS_PROCESS']), (Path('C:/assets') / RECORDER).resolve())
        self.assertNotIn('GT4_GS_RECORD_OUTPUT', environment)  # owner runs: null consumer, no file
        self.assertEqual(recorded['gs_stream_environment']['GT4_RENDERER'], 'pcsx2')

    def test_headless_run_keeps_the_caller_renderer(self):
        environment = {}
        self.assertEqual(probe.gs_stream_environment(environment, self.evidence(), Path('C:/assets'), False), {})
        self.assertEqual(environment, {})  # donor by default, nothing recorded
        environment = {'GT4_RENDERER': 'pcsx2', 'GT4_GS_RECORD_OUTPUT': 'C:/run/stream.gscap'}
        recorded = probe.gs_stream_environment(environment, self.evidence(), Path('C:/assets'), False)
        self.assertEqual(Path(environment['GT4_GS_PROCESS']), (Path('C:/assets') / RECORDER).resolve())
        self.assertEqual(recorded['gs_stream_environment']['GT4_GS_RECORD_OUTPUT'], 'C:/run/stream.gscap')

    def test_bad_values_and_unpinned_process_are_refused(self):
        with self.assertRaisesRegex(ValueError, 'donor or pcsx2'):
            probe.gs_stream_environment({'GT4_RENDERER': 'parallel'}, self.evidence(), Path('C:/a'), False)
        with self.assertRaisesRegex(ValueError, '1, 2 or 3'):
            probe.gs_stream_environment({'GT4_GS_FRAMES_IN_FLIGHT': '4'}, self.evidence(), Path('C:/a'), False)
        evidence = self.evidence()
        evidence['runtime_dependencies'] = []
        with self.assertRaisesRegex(ValueError, 'pin its GS process'):
            probe.gs_stream_environment({}, evidence, Path('C:/a'), False)

    def test_other_manifests_never_pass_stream_variables(self):
        environment = {'GT4_RENDERER': 'pcsx2', 'GT4_GS_PROCESS': 'x', 'GT4_GS_CAPTURE': 'y', 'GT4_MTVU': '1'}
        self.assertEqual(probe.gs_stream_environment(environment, {}, Path('C:/a'), False), {})
        self.assertEqual(environment, {'GT4_MTVU': '1'})


class PinTests(unittest.TestCase):
    def test_launcher_pin_describes_the_stream_build(self):
        manifest = json.loads((ROOT / 'docs/inputs' / MANIFEST).read_text(encoding='utf-8'))
        self.assertEqual(manifest['measurement_mode'], 'pcsx2-stream')
        self.assertEqual(manifest['runtime']['path'], EXECUTABLE)
        self.assertEqual(manifest['gs_stream']['gs_process'], RECORDER)
        self.assertEqual(manifest['source_manifest'], RUNTIME_PIN)
        paths = [item['path'] for item in manifest['runtime_dependencies']]
        self.assertIn(RECORDER, paths)
        self.assertEqual(len([path for path in paths if path.startswith('private/slice153-pacing-shaders/')]), 9)
        self.assertEqual(manifest['vu_scope_profile'], 'unit_split_run_fast_only')

    def test_runtime_pin_matches_the_capsules_when_present(self):
        pin = json.loads((ROOT / 'docs/inputs' / RUNTIME_PIN).read_text(encoding='utf-8'))
        capsule = ASSETS / pin['source_capsule']
        if not (capsule / 'runtime.json').exists():
            self.skipTest('slice191 runtime capsule not present')
        self.assertEqual(sha256(capsule / 'runtime.json'), pin['receipt_sha256'])
        for name, digest in pin['source_sha256'].items():
            self.assertEqual(sha256(capsule / name), digest, name)
        for name, digest in pin['inputs']['PORT'].items():
            self.assertEqual(sha256(ASSETS / pin['port_capsule'] / name), digest, name)

    def test_executable_pins_match_the_build_under_test(self):
        build = os.environ.get('GT4_NATIVE_BUILD')
        if not build or Path(build).resolve() != (ASSETS / 'private/slice191-gs-stream-build/build').resolve():
            self.skipTest('GT4_NATIVE_BUILD is not the slice191 build')
        manifest = json.loads((ROOT / 'docs/inputs' / MANIFEST).read_text(encoding='utf-8'))
        for entry in [manifest['runtime']] + manifest['runtime_dependencies']:
            path = ASSETS / entry['path']
            self.assertEqual((path.stat().st_size, sha256(path)), (entry['size_bytes'], entry['sha256']), entry['path'])


if __name__ == '__main__':
    unittest.main()
