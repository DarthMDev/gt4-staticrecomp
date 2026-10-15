"""Slice158 CRT table: the qualified guest bytes, the boot hook derivation and the slice158 pins.

The ELF checks read private/reconstructed/SCUS_973.28.elf when it exists (the pinned game image,
not committed) and are skipped otherwise. Native lookups and the rewrite are in the display_table_fixture.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import prepare_perf_runtime as producer

ELF = ROOT / 'private/reconstructed/SCUS_973.28.elf'
TABLE_ADDRESS = 0x618648
ELF_SEGMENT_OFFSET = 0xFF000  # file offset = vaddr - 0xFF000 for the PT_LOAD segments
# The rows read from the pinned ELF (id, interlace, mode, field). Row 8 is the fallback that
# func_00106058 returns when no row matches the id.
ELF_ROWS = [(1, 1, 0x2, 1), (2, 1, 0x2, 0), (3, 1, 0x3, 1), (4, 1, 0x3, 0), (6, 0, 0x50, 1),
            (8, 0, 0x52, 1), (7, 1, 0x51, 1), (5, 0, 0, 1), (0, 1, 0x2, 1)]


class ElfTableTests(unittest.TestCase):
    @unittest.skipUnless(ELF.is_file(), 'pinned SCUS_973.28.elf not present on this machine')
    def test_pinned_elf_rows_match_the_qualified_values(self):
        data = ELF.read_bytes()
        offset = TABLE_ADDRESS - ELF_SEGMENT_OFFSET
        rows = [struct.unpack_from('<4I', data, offset + 16 * index) for index in range(9)]
        self.assertEqual(rows, ELF_ROWS)

    def test_lookup_model_matches_the_qualified_rows(self):
        # Mirrors func_001060A0 (values -> id, first 8 rows, 0 when absent) and func_00106058
        # (id -> row, fallback row 8). The native fixture checks the same behaviour after the rewrite.
        def lookup_id(rows, interlace, mode, field):
            for row_id, row_interlace, row_mode, row_field in rows[:8]:
                if (row_interlace, row_mode, row_field) == (interlace, mode, field):
                    return row_id
            return 0

        def fetch(rows, row_id):
            for row in rows[:8]:
                if row[0] == row_id:
                    return row
            return rows[8]

        self.assertEqual(lookup_id(ELF_ROWS, 1, 2, 1), 1)
        self.assertEqual(fetch(ELF_ROWS, 1), ELF_ROWS[0])
        self.assertEqual(fetch(ELF_ROWS, 6), ELF_ROWS[4])


class BootHookDerivationTests(unittest.TestCase):
    def donor_system(self):
        return ('// Measurement-only MIT donor derivation; see native/THIRD_PARTY.md.\n'
                '#include "perf_probe.hpp"\n'
                '#include "ps2/system.hpp"\n'
                'void boot(std::uint32_t entry, const std::vector<std::string> &args) {\n'
                '    hw::reset();\n'
                '    kernel::start_main_thread(entry, 0, static_cast<std::uint32_t>(guest_args.size()), argv);\n'
                '    schedule(hw::kEventVblank, frame_cycles(), vblank);\n'
                '}\n')

    def test_hook_is_placed_before_the_main_thread_and_reverses_exactly(self):
        derived = producer.PREFIX + self.donor_system()[len(producer.PREFIX):]
        hooked = producer.derive_table(derived)
        self.assertIn('#include "display_table.hpp"\n', hooked)
        self.assertEqual(hooked.count('apply_force_480p_table(g_ram, kRamSize)'), 1)
        self.assertLess(hooked.index('apply_force_480p_table'), hooked.index('kernel::start_main_thread'))
        restored = hooked.replace(producer.TABLE_HOOK, '').replace(producer.TABLE_INCLUDE, '')
        self.assertEqual(restored, derived)

    def test_missing_or_duplicate_anchor_is_rejected(self):
        derived = producer.PREFIX + self.donor_system()[len(producer.PREFIX):]
        with self.assertRaisesRegex(ValueError, 'qualified boot anchor'):
            producer.derive_table(derived.replace(producer.TABLE_ANCHOR, ''))
        with self.assertRaisesRegex(ValueError, 'qualified boot anchor'):
            producer.derive_table(derived + producer.TABLE_ANCHOR)
        with self.assertRaisesRegex(ValueError, 'prefix'):
            producer.derive_table('void boot() {}\n' + producer.TABLE_ANCHOR)

    def test_slice158_capsule_receipt_records_the_table_override(self):
        with patch.object(producer, 'PINS', {'system.cpp': hashlib.sha256(self.donor_system().encode()).hexdigest()}):
            import tempfile
            with tempfile.TemporaryDirectory() as folder:
                root = Path(folder).resolve()
                donor = root / 'private/references/ps2-web-recomp/runtime/src'
                donor.mkdir(parents=True)
                (donor / 'system.cpp').write_bytes(self.donor_system().encode())
                with patch.object(producer, 'ROOT', root):
                    # Only system.cpp is pinned here, so the overlay sources are not required.
                    with patch.object(producer, 'derive', side_effect=lambda name, text, **kw: producer.PREFIX + text):
                        receipt = producer.prepare(root / 'private/capsule', root, display_table_override=True)
                self.assertEqual(receipt['display_table_override_profile'], 'overlay-v1')
                self.assertNotIn('gs_crt_override_profile', receipt)


class Slice158PinTests(unittest.TestCase):
    def load(self, name):
        return json.loads((ROOT / 'docs/inputs' / name).read_text(encoding='utf-8'))

    def test_capsule_and_launcher_pins_name_slice158(self):
        capsule = self.load('slice158-quickfix2-runtime.json')
        launcher = self.load('slice158-quickfix2.json')
        self.assertEqual(capsule['slice'], 158)
        self.assertEqual(launcher['measurement_mode'], 'quickfix2')
        self.assertEqual(launcher['source_capsule'], 'private/slice158-quickfix2-runtime')
        self.assertEqual(launcher['runtime']['path'], 'private/slice158-quickfix2-build/build/gt4vulkan.exe')
        self.assertEqual(launcher['vu_scope_profile'], 'unit_split_run_fast_only')

    def test_pinned_receipt_and_runtime_match_files_when_present(self):
        capsule = self.load('slice158-quickfix2-runtime.json')
        receipt = ROOT / 'private/slice158-quickfix2-runtime/runtime.json'
        if receipt.is_file():
            self.assertEqual(hashlib.sha256(receipt.read_bytes()).hexdigest(), capsule['receipt_sha256'])
        launcher = self.load('slice158-quickfix2.json')
        runtime = ROOT / launcher['runtime']['path']
        if runtime.is_file():
            self.assertEqual(runtime.stat().st_size, launcher['runtime']['size_bytes'])
            self.assertEqual(hashlib.sha256(runtime.read_bytes()).hexdigest(), launcher['runtime']['sha256'])


if __name__ == '__main__':
    unittest.main()
