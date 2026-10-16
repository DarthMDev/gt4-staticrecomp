"""TASK-047 (slice196): 480i deinterlacing in the PCSX2 GS mode. Launcher pins of slice196 and the protocol 1.1
statements the game and gt4-gs 0.3.2 rely on (SYNCV from SetGsCrt, VSYNC field = display field).
"""
import hashlib
import json
import os
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import external_runtime_probe as probe  # noqa: E402
import play_performance as performance  # noqa: E402

ASSETS = Path(os.environ.get('GT4_ASSETS_ROOT', ROOT))
MANIFEST = 'slice196-pcsx2.json'
EXECUTABLE = 'private/slice196-gs-deinterlace-build/build/gt4vulkan.exe'
PACKAGE = 'private/slice196-gt4gs-release-pkg'
GS_PROCESS = PACKAGE + '/gt4-gs.exe'


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class Slice196PinTests(unittest.TestCase):
    def manifest(self):
        return json.loads((ROOT / 'docs/inputs' / MANIFEST).read_text(encoding='utf-8'))

    def test_mode_pcsx2_slice196_stays_selectable(self):
        # Since slice199 (TASK-041 on gt4-gs 0.4.0) --mode pcsx2 points at slice199; slice196 stays selectable.
        self.assertEqual(probe.PERF_PROFILES['pcsx2-slice196'], (MANIFEST, EXECUTABLE))
        self.assertIn('pcsx2-slice196', performance.MODES)
        self.assertEqual(probe.PERF_PROFILES['pcsx2-slice195'],
                         ('slice195-pcsx2.json', 'private/slice195-gs-window-build/build/gt4vulkan.exe'))
        self.assertIn(MANIFEST, probe.VU1_AOT_MANIFESTS)
        self.assertIn(MANIFEST, probe.VU0_AOT_MANIFESTS)
        self.assertIn('pcsx2', performance.MODES)
        self.assertIn('pcsx2-slice195', performance.MODES)

    def test_launcher_pin_describes_the_slice196_build(self):
        manifest = self.manifest()
        self.assertEqual(manifest['measurement_mode'], 'pcsx2')
        self.assertEqual(manifest['slice'], 196)
        self.assertEqual(manifest['runtime']['path'], EXECUTABLE)
        stream = manifest['gs_stream']
        self.assertEqual(stream['renderer'], 'pcsx2')
        self.assertEqual(stream['gs_process'], GS_PROCESS)
        self.assertIn('version 1.1', stream['protocol'])
        package = stream['gs_package']
        self.assertEqual(package['directory'], PACKAGE)
        self.assertIs(package['data_root'], True)
        self.assertEqual(package['zip']['name'], 'gt4-gs-0.3.2-windows-x64.zip')
        self.assertTrue(package['release'].endswith('/releases/tag/gt4gs-v0.3.2'))
        paths = [item['path'] for item in manifest['runtime_dependencies']]
        self.assertIn(GS_PROCESS, paths)
        self.assertEqual(len([path for path in paths if path.startswith('private/slice153-pacing-shaders/')]), 9)
        self.assertEqual(manifest['source_capsule'], 'private/slice191-gs-stream-runtime')
        self.assertEqual(manifest['port_capsule'], 'private/slice191-gs-stream-port')

    def test_package_matches_its_pin_when_present(self):
        manifest = self.manifest()
        package = ASSETS / manifest['gs_stream']['gs_package']['directory']
        if not package.is_dir():
            self.skipTest('slice196 gt4-gs package not present')
        digest, files = probe.package_tree_digest(package)
        self.assertEqual((digest, files), (manifest['gs_stream']['gs_package']['tree_sha256'],
                                           manifest['gs_stream']['gs_package']['files']))
        self.assertIn('Source ref: gt4gs-v0.3.2', (package / 'SOURCE.txt').read_text(encoding='utf-8'))

    def test_executable_pins_match_the_build_under_test(self):
        build = os.environ.get('GT4_NATIVE_BUILD')
        if not build or Path(build).resolve() != (ASSETS / 'private/slice196-gs-deinterlace-build/build').resolve():
            self.skipTest('GT4_NATIVE_BUILD is not the slice196 build')
        manifest = self.manifest()
        for entry in [manifest['runtime']] + manifest['runtime_dependencies']:
            path = ASSETS / entry['path']
            self.assertEqual((path.stat().st_size, sha256(path)), (entry['size_bytes'], entry['sha256']), entry['path'])


class ProtocolStatementTests(unittest.TestCase):
    def test_header_is_version_1_1_with_the_display_field(self):
        header = (ROOT / 'native/gs_stream/gs_stream_protocol.h').read_text(encoding='utf-8')
        self.assertRegex(header, r'kVersionMinor = 1;')
        self.assertIn('display', header[header.index('--- VSYNC'):header.index('struct VsyncPayload')])

    def test_set_gs_crt_posts_syncv_for_the_analog_modes(self):
        source = (ROOT / 'native/gs_stream/gs_stream_client.cpp').read_text(encoding='utf-8')
        body = source[source.index('void on_set_gs_crt('):]
        body = body[:body.index('\n}') + 2]
        # SMODE1, then SYNCV (when the mode has one), then SMODE2.
        order = [m.start() for m in re.finditer(r'0x120000(10|60|20)u', body)]
        self.assertEqual(len(order), 3)
        self.assertEqual([body[i:i + 10] for i in sorted(order)], ['0x12000010', '0x12000060', '0x12000020'])

    def test_protocol_document_matches(self):
        text = (ROOT / 'docs/GS-STREAM-PROTOCOL.md').read_text(encoding='utf-8')
        self.assertTrue(text.startswith('# GS stream protocol, version 1.1'))
        self.assertIn('display field', text)
        self.assertIn('0x00C7800601A01801', text)


if __name__ == '__main__':
    unittest.main()
