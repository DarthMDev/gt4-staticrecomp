"""Synthetic and private controls for the explicit-seed EE producer (slice 161)."""

import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))

import ee_entries
from ee_archive import object_inventory


def synthetic_elf(base=0x00100000, body=bytes(64)):
    """A one-segment MIPS ELF whose executable bytes start at base."""
    elf = bytearray(84 + len(body))
    elf[:16] = b'\x7fELF\x01\x01\x01' + b'\0' * 9
    struct.pack_into('<HHIIIIIHHHHHH', elf, 16, 2, 8, 1, base, 52, 0, 0x20924001, 52, 32, 1, 0, 0, 0)
    struct.pack_into('<8I', elf, 52, 1, 84, base, base, len(body), len(body), 1, 4)
    elf[84:] = body
    return bytes(elf)


def synthetic_table(base, size, suffix, entries):
    table = f'{{0x{base:08X}u, {size}u, &unit_{suffix}, entries_{suffix}}},\n'
    source = (f'const std::uint16_t entries_{suffix}[{len(entries)}] = {{'
              + ', '.join(str(value) for value in entries) + '};\n'
              + ''.join(f'case {value}u: goto L_{base + index * 4:08X};\n'
                        for index, value in enumerate(entries) if value))
    return table, {suffix: source}


class SeedParsing(unittest.TestCase):
    def test_accepts_strict_uppercase_aligned_addresses(self):
        self.assertEqual(ee_entries.parse_seeds('0x0044EA58\n0x004CFD08\n'), [0x0044EA58, 0x004CFD08])

    def test_rejects_malformed_unaligned_duplicate_and_empty_seed_lists(self):
        for text in ('0x0044ea58\n', '44EA58\n', '0x0044EA5\n', '0x0044EA5A\n', '0x0044EA58\n0x0044EA58\n', '\n', ''):
            with self.subTest(text=text), self.assertRaises(ValueError):
                ee_entries.parse_seeds(text)

    def test_committed_referenced_set_matches_its_pin(self):
        path, seeds, pin = ee_entries.seed_set_pin('referenced')
        self.assertEqual(len(seeds), 16)
        self.assertEqual(pin['count'], 16)
        self.assertEqual(pin['expected_translator_entries'], 135155)

    def test_pin_mismatch_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'docs/inputs').mkdir(parents=True)
            (root / 'docs/inputs/seeds.txt').write_text('0x00100000\n', encoding='ascii')
            manifest = {'seed_sets': {'test': {'path': 'docs/inputs/seeds.txt', 'size_bytes': 1,
                                                'sha256': '0' * 64, 'count': 1,
                                                'expected_translator_entries': 1}}}
            with patch.object(ee_entries, 'read_seed_manifest', return_value=manifest):
                with self.assertRaises(ValueError):
                    ee_entries.seed_set_pin('test', root)


class SeedExecutability(unittest.TestCase):
    def test_seed_inside_executable_bytes_passes(self):
        ee_entries.check_seeds_executable(synthetic_elf(), [0x00100000, 0x0010003C])

    def test_bad_seed_is_reported_not_dropped(self):
        for address in (0x00000000, 0x00100040, 0x00200000):
            with self.subTest(address=hex(address)), self.assertRaises(ValueError):
                ee_entries.check_seeds_executable(synthetic_elf(), [address])


class DispatchVerification(unittest.TestCase):
    def test_nonzero_registered_seed_resolves_and_zero_id_seed_is_reported(self):
        table, sources = synthetic_table(0x00100000, 16, '0001', [0, 3, 0, 5])
        resolved, failures = ee_entries.verify_resolution(table, sources, [0x00100004, 0x00100008])
        self.assertEqual(resolved, [{'address': '0x00100004', 'unit': 'unit_0001', 'entry_id': 3}])
        self.assertEqual([row['address'] for row in failures], ['0x00100008'])

    def test_seed_outside_every_registry_row_is_reported(self):
        table, sources = synthetic_table(0x00100000, 8, '0001', [1, 2])
        resolved, failures = ee_entries.verify_resolution(table, sources, [0x00200000])
        self.assertEqual(resolved, [])
        self.assertEqual(len(failures), 1)


class GenerationReceipts(unittest.TestCase):
    def test_stats_line_parses_and_duplicates_are_refused(self):
        line = 'code 1241079 instructions (4847 KiB), functions 14679, entries 135155, rejected seeds 0\n'
        stats = ee_entries.parse_stats(line)
        self.assertEqual(stats, {'instructions': 1241079, 'functions': 14679, 'entries': 135155, 'rejected_seeds': 0})
        with self.assertRaises(ValueError):
            ee_entries.parse_stats(line + line)

    def test_rejected_seed_and_entry_count_drift_are_refused(self):
        pin = {'expected_translator_entries': 135155}
        with self.assertRaises(ValueError):
            ee_entries.validate_stats({'entries': 135155, 'rejected_seeds': 1}, pin)
        with self.assertRaises(ValueError):
            ee_entries.validate_stats({'entries': 135154, 'rejected_seeds': 0}, pin)
        ee_entries.validate_stats({'entries': 135155, 'rejected_seeds': 0}, pin)

    def test_changed_units_lists_differences_and_rejects_set_changes(self):
        baseline = [{'path': 'unit_0001.cpp', 'sha256': 'a'}, {'path': 'unit_0002.cpp', 'sha256': 'b'}]
        candidate = [{'path': 'unit_0001.cpp', 'sha256': 'c'}, {'path': 'unit_0002.cpp', 'sha256': 'b'}]
        self.assertEqual(ee_entries.changed_units(baseline, candidate), ['unit_0001.cpp'])
        with self.assertRaises(ValueError):
            ee_entries.changed_units(baseline, candidate[:1])


class PrivateProducerControls(unittest.TestCase):
    def test_private_generation_receipt_when_present(self):
        receipt = ROOT / 'private/slice161-ee-entries-producer/generation-entries.json'
        if not receipt.exists():
            self.skipTest('Explicit-seed producer is not generated in this checkout')
        import json
        data = json.loads(receipt.read_text(encoding='utf-8'))
        self.assertEqual(len(data['resolved_seeds']), 16)
        self.assertEqual(data['emitted_stop_sites'], 0)
        self.assertEqual(data['stats']['rejected_seeds'], 0)
        self.assertEqual(data['stats']['entries'], 135155)

    def test_private_library_members_when_present(self):
        library = ROOT / 'private/slice161-ee-entries-producer/game-build/ps2game.lib'
        if not library.exists():
            self.skipTest('Explicit-seed library is not built in this checkout')
        self.assertEqual(len(object_inventory(library.read_bytes())), 327)


if __name__ == '__main__':
    unittest.main()
