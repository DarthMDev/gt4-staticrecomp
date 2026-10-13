"""Independent instruction/address controls; no private game assets."""

import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import render_contract_inventory as inventory


class InventoryTests(unittest.TestCase):
    def test_jal_target_uses_pc_region_and_excludes_jump(self):
        target = 0x80400000
        jal = (3 << 26) | ((target >> 2) & 0x03ffffff)
        jump = (2 << 26) | ((target >> 2) & 0x03ffffff)
        segments = [(0x80000000, 0, struct.pack('<III', jal, jump, 0))]
        self.assertEqual(inventory.direct_jal_sites(segments, [target]), {target: [0x80000000]})

    def test_prefix_maps_file_offset_and_stays_in_file_bytes(self):
        segments = [(0x100000, 4096, bytes(range(128)))]
        result = inventory.address_prefix(segments, 0x100004)
        self.assertEqual(result, {'file_offset': 4100, 'size_bytes': 64,
                                 'sha256': hashlib.sha256(bytes(range(4, 68))).hexdigest()})
        for address in (0x100001, 0x100044, 0xffffc):
            with self.subTest(address=address), self.assertRaises(ValueError):
                inventory.address_prefix(segments, address)

    def test_dispatch_requires_registry_array_and_matching_label(self):
        table = '{0x00100000u, 16u, &unit_0000, entries_0000}'
        source = ('const std::uint16_t entries_0000[4] = {0, 7, 0, 0};\n'
                  'case 7u: goto L_00100004;')
        self.assertEqual(inventory.resolve_dispatch(table, 0x100004, {'0000': source}), ('0000', 7))
        self.assertEqual(inventory.resolve_dispatch(table, 0x100000, {'0000': source},
                                                   require_registered=False), ('0000', 0))
        for changed_table, address, changed_source in (
                (table, 0x100000, source), (table, 0x100005, source),
                (table + table, 0x100004, source),
                (table, 0x100004, source.replace('case 7u', 'case 8u')),
                (table, 0x100004, source.replace('[4]', '[5]'))):
            with self.subTest(address=address), self.assertRaises(ValueError):
                inventory.resolve_dispatch(changed_table, address, {'0000': changed_source})

    def test_elf_requires_bounded_executable_segment(self):
        header = (b'\x7fELF\x01\x01\x01' + b'\0' * 9
                  + struct.pack('<HHIIIIIHHHHHH', 2, 8, 1, 0x100000, 52, 0, 0, 52, 32, 1, 0, 0, 0))
        elf = header + struct.pack('<8I', 1, 84, 0x100000, 0x100000, 4, 4, 5, 4) + b'abcd'
        self.assertEqual(inventory.executable_segments(elf), [(0x100000, 84, b'abcd')])
        for data in (elf[:51], elf[:-1], b'broken' + elf[6:]):
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                inventory.executable_segments(data)


if __name__ == '__main__':
    unittest.main()
