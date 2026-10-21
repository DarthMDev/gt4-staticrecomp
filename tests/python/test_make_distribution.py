"""Distribution folder assembly (TASK-059, scripts/make_distribution.py).

Needs GT4_NATIVE_BUILD (the build of the runtime pinned in docs/inputs/slice218-pcsx2.json, mode pcsx2) and GT4_ASSETS_ROOT (the
checkout that holds the pinned shaders and the donor licence; default: the main checkout). Skipped when absent.
The distribution is written under the assets checkout's private/ folder, as the script requires, and removed afterwards.
"""

import importlib.util
import json
import os
import shutil
import tempfile
import unittest
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = os.environ.get('GT4_NATIVE_BUILD')
ASSETS = Path(os.environ.get('GT4_ASSETS_ROOT', 'C:/Antigravity/gt4-staticrecomp')).resolve()
PIN = ROOT / 'docs/inputs/slice218-pcsx2.json'

spec = importlib.util.spec_from_file_location('make_distribution', ROOT / 'scripts/make_distribution.py')
dist = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dist)


def executable():
    if not BUILD:
        return None
    candidate = Path(BUILD) / 'gt4vulkan.exe'
    return candidate if candidate.exists() else None


@unittest.skipUnless(executable() and PIN.exists() and (ASSETS / 'private/references/ps2-web-recomp/LICENSE').exists(),
                     'GT4_NATIVE_BUILD, the slice218 pin or the donor licence is not available')
class MakeDistributionTest(unittest.TestCase):
    def setUp(self):
        self.output = ASSETS / 'private' / f'slice218-dist-test-{uuid.uuid4().hex[:8]}'
        self.scratch = Path(tempfile.mkdtemp(prefix='gt4-dist-test-'))

    def tearDown(self):
        shutil.rmtree(self.output, ignore_errors=True)
        shutil.rmtree(self.scratch, ignore_errors=True)

    def assemble(self, exe, output=None):
        return dist.assemble(output or self.output, ASSETS, exe, PIN, ASSETS / 'private/slice153-pacing-shaders',
                             None, allow_missing_crt=True)

    def test_folder_holds_the_executable_shaders_and_licences_only(self):
        manifest = self.assemble(executable())
        names = sorted(item['path'] for item in manifest['files'])
        self.assertIn('gt4vulkan.exe', names)
        for shader in dist.SHADER_NAMES:
            self.assertIn(f'shaders/{shader}.spv', names)
        self.assertIn('licenses/LICENSE-GT4Recomp.txt', names)
        self.assertIn('licenses/LICENSE-ps2-web-recomp.txt', names)
        self.assertIn('licenses/THIRD_PARTY.md', names)
        # TASK-075: SDL3.dll and its licence (zlib) ship next to the executable.
        self.assertIn('SDL3.dll', names)
        self.assertIn('licenses/LICENSE-SDL3.txt', names)
        self.assertTrue((self.output / 'MANIFEST.json').exists())
        # No gt4-gs package, no game data and no memory card in the folder.
        self.assertFalse((self.output / 'gs').exists())
        for path in self.output.rglob('*'):
            self.assertNotIn(path.suffix.lower(), ('.iso', '.elf', '.ps2', '.lib', '.gt4', '.gscap', '.bin'), path)
        recorded = json.loads((self.output / 'MANIFEST.json').read_text(encoding='utf-8'))
        self.assertEqual(recorded['executable_sha256'], manifest['executable_sha256'])
        for item in recorded['files']:
            self.assertEqual(dist.sha256_of(self.output / item['path']), item['sha256'], item['path'])

    def test_a_changed_executable_fails_its_pin(self):
        changed = self.scratch / 'gt4vulkan.exe'
        data = bytearray(executable().read_bytes())
        data[-1] ^= 0xFF
        changed.write_bytes(bytes(data))
        with self.assertRaises(ValueError):
            self.assemble(changed)
        self.assertFalse(self.output.exists() and any(self.output.iterdir()))

    def test_output_outside_private_is_refused(self):
        with self.assertRaises(ValueError):
            self.assemble(executable(), output=self.scratch / 'dist')


if __name__ == '__main__':
    unittest.main()
