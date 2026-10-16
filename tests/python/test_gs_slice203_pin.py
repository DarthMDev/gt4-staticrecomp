"""TASK-044 (slice203): gt4-gs 1.0.1 (no writes under Documents\\PCSX2) pinned with the slice202 game executable.

Checks the pin against slice202 (same game runtime, same hash), the published stable gt4-gs 1.0.1 release values, the
launcher mapping (--mode pcsx2 = slice203, pcsx2-slice202 kept), and, when the files are present, the hashes on disk.
"""
import hashlib
import json
import os
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'tests/python'))
import external_runtime_probe as probe  # noqa: E402
import play_performance as performance  # noqa: E402

ASSETS = Path(os.environ.get('GT4_ASSETS_ROOT', ROOT))
PACKAGE = 'private/slice203-gt4gs-release-pkg'
EXECUTABLE = 'private/slice203-build/build/gt4vulkan.exe'
EXE_SHA = 'd4cb6de66f9dcc2d4522af6d1628387f7b7087b5844f21a765b5313f05b912b9'
GS_SHA = 'bda98d498fd25215f379977438d693390bbba30746927243d65359dd8a5e806b'
ZIP_SHA = '58f7cfef4842e781a62778a26276434510c6476545b6c6200269c2f1dd7244ca'
TREE_SHA = '55939b2be4cd55dbd24a6987702e8a71205fdb32067dad56f8986e171399a167'


def manifest(name):
    return json.loads((ROOT / 'docs/inputs' / name).read_text(encoding='utf-8'))


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class Slice203PinTests(unittest.TestCase):
    def test_mode_pcsx2_selects_slice203_and_slice202_stays(self):
        self.assertEqual(probe.PERF_PROFILES['pcsx2'], ('slice203-pcsx2.json', EXECUTABLE))
        self.assertEqual(probe.PERF_PROFILES['pcsx2-slice202'],
                         ('slice202-pcsx2.json', 'private/slice202-build/build/gt4vulkan.exe'))
        self.assertIn('pcsx2-slice202', performance.MODES)
        self.assertIn('pcsx2', performance.MODES)
        self.assertIn('slice203-pcsx2.json', probe.VU1_AOT_MANIFESTS)
        self.assertIn('slice203-pcsx2.json', probe.VU0_AOT_MANIFESTS)

    def test_slice203_is_the_slice202_game_with_the_1_0_1_package(self):
        pin, previous = manifest('slice203-pcsx2.json'), manifest('slice202-pcsx2.json')
        self.assertEqual(pin['slice'], 203)
        self.assertEqual(pin['runtime'], {'path': EXECUTABLE, 'size_bytes': 95685120, 'sha256': EXE_SHA})
        self.assertEqual(previous['runtime']['sha256'], EXE_SHA)
        for key in ('source_capsule', 'port_capsule', 'shader_capsule', 'vu1_aot', 'vu0_aot', 'card_seeds',
                    'frozen_game_library', 'environment', 'toolchain', 'memcard_capsule', 'memcard_capsule_sha256'):
            self.assertEqual(pin[key], previous[key], key)
        stream, old_stream = pin['gs_stream'], previous['gs_stream']
        self.assertEqual(stream['gs_process'], PACKAGE + '/gt4-gs.exe')
        self.assertEqual(stream['protocol'], old_stream['protocol'])
        package = stream['gs_package']
        self.assertEqual(package['directory'], PACKAGE)
        self.assertEqual(package['files'], 140)
        self.assertEqual(package['tree_sha256'], TREE_SHA)
        self.assertEqual(package['zip'], {'name': 'gt4-gs-1.0.1-windows-x64.zip', 'size_bytes': 13022346, 'sha256': ZIP_SHA})
        self.assertEqual(package['release'], 'https://github.com/moesuito/gt4recomp-gs/releases/tag/gt4gs-v1.0.1')
        self.assertIn('stable release gt4gs-v1.0.1', package['source'])
        gs_pin = [item for item in pin['runtime_dependencies'] if item['path'].endswith('gt4-gs.exe')]
        self.assertEqual(gs_pin, [{'path': PACKAGE + '/gt4-gs.exe', 'size_bytes': 8955904, 'sha256': GS_SHA}])
        others = [item for item in pin['runtime_dependencies'] if not item['path'].endswith('gt4-gs.exe')]
        self.assertEqual(others, [item for item in previous['runtime_dependencies'] if not item['path'].endswith('gt4-gs.exe')])

    def test_pinned_files_match_when_present(self):
        pin = manifest('slice203-pcsx2.json')
        if not (ASSETS / pin['runtime']['path']).exists():
            self.skipTest('slice203 build not present')
        for entry in [pin['runtime']] + pin['runtime_dependencies']:
            path = ASSETS / entry['path']
            self.assertEqual((path.stat().st_size, sha256(path)), (entry['size_bytes'], entry['sha256']), entry['path'])
        package = pin['gs_stream']['gs_package']
        self.assertEqual(probe.package_tree_digest(ASSETS / package['directory']), (package['tree_sha256'], package['files']))


if __name__ == '__main__':
    unittest.main()
