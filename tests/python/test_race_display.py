"""Slice164 race display index: the pinned ELF chain, the window hook and the launcher mode.

The ELF checks read private/reconstructed/SCUS_973.28.elf when it exists (the pinned game image,
not committed) and are skipped otherwise. The C++ fixture (race_display_fixture) covers the logic.
"""
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
ELF = ROOT / 'private/reconstructed/SCUS_973.28.elf'
TEXT_BASE = 0xFF000


def word(data, vaddr):
    return struct.unpack_from('<I', data, vaddr - TEXT_BASE)[0]


class RaceDisplayElfChainTests(unittest.TestCase):
    """Words of the pinned ELF that the owner-decision chain depends on (Confirmed in slice164)."""

    @classmethod
    def setUpClass(cls):
        if not ELF.is_file():
            raise unittest.SkipTest('pinned ELF not present')
        cls.data = ELF.read_bytes()

    def test_setdefault_clears_the_index_at_0x16b0(self):
        self.assertEqual(word(self.data, 0x436598), 0xAE0016B0)  # sw zero,0x16B0(s0) in func_004363A0

    def test_race_start_reads_the_index_at_0x16b0(self):
        self.assertEqual(word(self.data, 0x43665C), 0x8E2316B0)  # lw v1,0x16B0(s1) in func_00436640

    def test_owner_pointer_is_read_from_0x622f4c_and_offset_0x38cb0(self):
        self.assertEqual(word(self.data, 0x1103F0), 0x8C442F4C)  # lw a0,0x2F4C(v0), v0 = 0x620000
        self.assertEqual(word(self.data, 0x1103F8), 0x3C010003)  # lui at,0x3
        self.assertEqual(word(self.data, 0x1103FC), 0x34218CB0)  # ori at,at,0x8CB0

    def test_index_names_table_is_normal_480p_1080i(self):
        names = []
        for index in range(3):
            pointer = word(self.data, 0x622DF0 + 4 * index)
            start = pointer - TEXT_BASE
            names.append(self.data[start:self.data.index(b'\0', start)].decode('latin1'))
        self.assertEqual(names, ['normal', '480p', '1080i'])


class RaceDisplayWiringTests(unittest.TestCase):
    def test_window_hook_is_called_each_vblank_only_when_forced(self):
        body = (ROOT / 'native/window.cpp').read_text(encoding='utf-8')
        self.assertIn('if (force_480p_enabled)\n    gt4::display::force_race_480p_at_vblank(ps2::g_ram, ps2::kRamSize, force_480p_state);', body)
        self.assertIn('force_480p_enabled = gt4::display::parse_force_480p(std::getenv("GT4_FORCE_480P"));', body)
        self.assertIn('race display index writes', body)

    def test_old_crt_table_and_setgscrt_files_are_gone(self):
        for name in ('display_table.cpp', 'display_table.hpp', 'display_table_fixture.cpp',
                     'display_override.cpp', 'display_override.hpp'):
            self.assertFalse((ROOT / 'native' / name).exists(), name)

    def test_ctest_registers_the_race_display_fixture(self):
        text = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
        self.assertIn('add_test(NAME native_race_display COMMAND $<TARGET_FILE:race_display_fixture>)', text)
        self.assertNotIn('native_display_table', text)

    def test_launcher_quickfix3_selects_slice164(self):
        import external_runtime_probe as probe
        manifest, runtime = probe.PERF_PROFILES['quickfix3']
        self.assertEqual(manifest, 'slice164-quickfix3.json')
        self.assertEqual(runtime, 'private/slice164-quickfix3-build/build/gt4vulkan.exe')
        self.assertIn('slice164-quickfix3.json', probe.VU1_AOT_MANIFESTS)


if __name__ == '__main__':
    unittest.main()
