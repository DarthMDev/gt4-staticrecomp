"""Independent synthetic archive controls, no proprietary object code."""

from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from ee_archive import object_inventory, inventory_digest


def archive(timestamp=1, machine=0x8664):
    body = struct.pack('<HHIIIHH', machine, 1, timestamp, 0, 0, 0, 0) + bytes(40)
    header = (b'table.obj/      '.ljust(16) + b'0'.ljust(12) + b'0'.ljust(6)
              + b'0'.ljust(6) + b'0'.ljust(8) + str(len(body)).encode().ljust(10) + b'`\n')
    return b'!<arch>\n' + header + body


class ArchiveTests(unittest.TestCase):
    def test_long_member_names_and_invalid_offsets(self):
        def member(name, body):
            header = (name.ljust(16) + b'0'.ljust(12) + b'0'.ljust(6) + b'0'.ljust(6)
                      + b'0'.ljust(8) + str(len(body)).encode().ljust(10) + b'`\n')
            return header + body + (b'\n' if len(body) & 1 else b'')
        first, second = b'folder\\table.obj\0', b'another\\unit_0000.obj\0'
        body = archive()[68:]
        data = (b'!<arch>\n' + member(b'//', first + second)
                + member(b'/0', body) + member(('/' + str(len(first))).encode(), body))
        rows = object_inventory(data)
        self.assertEqual([row['name'] for row in rows], ['table.obj', 'unit_0000.obj'])
        for names, reference in ((b'unterminated', b'/0'), (first, b'/999')):
            with self.subTest(reference=reference), self.assertRaises(ValueError):
                object_inventory(b'!<arch>\n' + member(b'//', names) + member(reference, body))

    def test_only_timestamp_is_normalized(self):
        first = object_inventory(archive(1))
        self.assertEqual(first, object_inventory(archive(2)))
        changed = bytearray(archive())
        changed[-1] = 1
        self.assertNotEqual(inventory_digest(first), inventory_digest(object_inventory(bytes(changed))))

    def test_invalid_and_non_amd64_archives_fail(self):
        for data in (b'', b'arbitrary bytes', b'!<arch>\n', archive()[:-1], archive(machine=0x14c)):
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                object_inventory(data)


if __name__ == '__main__':
    unittest.main()
