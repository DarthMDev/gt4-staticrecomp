"""compare_fsv_trace controls: parsing, CRC comparison, zero fill past the end and ISO9660 name mapping."""
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
import zlib

import pycdlib

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import compare_fsv_trace as compare

SECTOR = 2048


def trace_line(lsn, sectors, dst, length, crc):
    return '[fsv] read lsn=%d sectors=%d dst=0x%08X bytes=%d crc32=%08X' % (lsn, sectors, dst, length, crc)


class ParseTests(unittest.TestCase):
    def test_parses_fields_and_ignores_other_lines(self):
        lines = ['[disc] image: 3 files', trace_line(16, 1, 0x00100000, 2048, 0xCBF43926), 'noise [fsv] nope']
        reads = compare.parse_trace(lines)
        self.assertEqual(reads, [{'line': 2, 'lsn': 16, 'sectors': 1, 'dst': 0x00100000, 'bytes': 2048,
                                  'crc32': 0xCBF43926}])

    def test_no_reads_gives_empty_list(self):
        self.assertEqual(compare.parse_trace(['[disc] only']), [])


class CompareTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.image = Path(self.folder.name) / 'disc.bin'
        self.data = bytes(range(256)) * 16  # two sectors of known content
        self.image.write_bytes(self.data)

    def tearDown(self):
        self.folder.cleanup()

    def reads_for(self, lsn, sectors, length, crc):
        return compare.parse_trace([trace_line(lsn, sectors, 0x100000, length, crc)])

    def test_matching_crc_reports_match(self):
        expected = zlib.crc32(self.data[SECTOR:SECTOR + 100]) & 0xFFFFFFFF
        with self.image.open('rb') as stream:
            results = compare.compare(self.reads_for(1, 1, 100, expected), stream)
        self.assertTrue(results[0]['match'])
        self.assertEqual(results[0]['iso_crc32'], expected)

    def test_wrong_crc_reports_mismatch(self):
        with self.image.open('rb') as stream:
            results = compare.compare(self.reads_for(1, 1, 100, 0xDEADBEEF), stream)
        self.assertFalse(results[0]['match'])

    def test_range_past_end_is_zero_filled_like_disc_read(self):
        # LSN 2 starts at the end of the 4096-byte image: the 12 requested bytes are all zero fill.
        expected = zlib.crc32(bytes(12)) & 0xFFFFFFFF
        with self.image.open('rb') as stream:
            results = compare.compare(self.reads_for(2, 1, 12, expected), stream)
        self.assertTrue(results[0]['match'])
        self.assertEqual(results[0]['iso_crc32'], expected)

    def test_table_and_summary(self):
        with self.image.open('rb') as stream:
            results = compare.compare(self.reads_for(0, 1, 16, 0), stream)
        table = compare.format_table(results)
        self.assertIn('reads: 1  matches: 0  mismatches: 1', table)
        self.assertIn('(unmapped)', table)


class MappingTests(unittest.TestCase):
    def test_iso9660_file_extent_maps_to_its_name(self):
        with tempfile.TemporaryDirectory() as folder:
            iso_path = Path(folder) / 'small.iso'
            iso = pycdlib.PyCdlib()
            iso.new()
            payload = b'license-test' * 300
            iso.add_fp(io.BytesIO(payload), len(payload), '/TEST.BIN;1')
            iso.write(str(iso_path))
            iso.close()
            extents = compare.iso9660_extents(iso_path)
            names = [name for _start, _end, name in extents]
            self.assertIn('/TEST.BIN;1', names)
            start, end, _ = next(e for e in extents if e[2] == '/TEST.BIN;1')
            self.assertEqual(compare.names_for(start, 1, extents), ['/TEST.BIN;1'])
            self.assertEqual(compare.names_for(end + 100, 1, extents), [])

    def test_main_without_pinned_disc_still_maps_iso9660_and_reports_table(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            iso_path = folder / 'small.iso'
            iso = pycdlib.PyCdlib()
            iso.new()
            payload = b'x' * 4096
            iso.add_fp(io.BytesIO(payload), len(payload), '/DATA.BIN;1')
            iso.write(str(iso_path))
            iso.close()
            log = folder / 'probe.log'
            start = next(s for s, _e, n in compare.iso9660_extents(iso_path) if n == '/DATA.BIN;1')
            log.write_text('[disc] x\n' + trace_line(start, 1, 0x100000, 2048,
                                                     zlib.crc32(payload[:2048]) & 0xFFFFFFFF) + '\n',
                           encoding='utf-8')
            output = io.StringIO()
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(io.StringIO()):
                status = compare.main([str(log), str(iso_path)])
            self.assertEqual(status, 0)
            self.assertIn('/DATA.BIN;1', output.getvalue())
            self.assertIn('matches: 1  mismatches: 0', output.getvalue())

    def test_main_rejects_trace_without_fsv_lines(self):
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / 'probe.log'
            log.write_text('[disc] nothing here\n', encoding='utf-8')
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(compare.main([str(log), str(log)]), 1)


if __name__ == '__main__':
    unittest.main()
