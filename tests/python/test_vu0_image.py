"""VU0 static image and VCALLMS entry list (slice 160). Synthetic ELF bytes only; no game data.

Controls that read the private ELF and image skip when those files are absent.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import vu0_image as image_tool

ROOT = Path(__file__).resolve().parents[2]
PRIVATE_ELF = ROOT.parent / 'gt4-staticrecomp/private/reconstructed/SCUS_973.28.elf'
PRIVATE_IMAGE = ROOT.parent / 'gt4-staticrecomp/private/slice160-vu0-image/vu0_image.bin'


def synthetic_elf(upload, *, vaddr=image_tool.ELF_VADDR, file_offset=image_tool.ELF_OFFSET):
    """A minimal ELF32 LE file with one PT_LOAD segment that maps the upload at vaddr."""
    header_size, phentsize = 0x34, 0x20
    body = bytearray(max(file_offset + len(upload) + 64, 0x1000))
    body[0:4] = b'\x7fELF'
    body[4], body[5], body[6] = 1, 1, 1
    struct.pack_into('<HHI', body, 0x10, 2, 8, 1)  # ET_EXEC, EM_MIPS, version
    struct.pack_into('<I', body, 0x1C, header_size)  # e_phoff
    struct.pack_into('<HH', body, 0x2A, phentsize, 1)  # e_phentsize, e_phnum
    struct.pack_into('<IIIIIIII', body, header_size, 1, file_offset, vaddr, vaddr, len(upload), len(upload), 5, 0x10)
    body[file_offset:file_offset + len(upload)] = upload
    return bytes(body)


class ImageTests(unittest.TestCase):
    def test_upload_is_read_from_the_mapped_segment(self):
        upload = bytes((index * 7 + 3) & 0xFF for index in range(image_tool.UPLOAD_BYTES))
        self.assertEqual(image_tool.upload_bytes(synthetic_elf(upload)), upload)

    def test_refuses_a_segment_that_maps_elsewhere(self):
        upload = bytes(image_tool.UPLOAD_BYTES)
        with self.assertRaisesRegex(ValueError, 'does not map'):
            image_tool.upload_bytes(synthetic_elf(upload, vaddr=0x6D3000))

    def test_refuses_missing_segment(self):
        with self.assertRaisesRegex(ValueError, 'no PT_LOAD'):
            image_tool.upload_bytes(synthetic_elf(bytes(image_tool.UPLOAD_BYTES), file_offset=0x200))

    def test_padding_is_zero_and_size_is_4k(self):
        image = image_tool.pad_image(bytes([0xAB]) * image_tool.UPLOAD_BYTES)
        self.assertEqual(len(image), image_tool.IMAGE_BYTES)
        self.assertEqual(image[image_tool.UPLOAD_BYTES:], bytes(image_tool.IMAGE_BYTES - image_tool.UPLOAD_BYTES))

    def test_rejects_short_upload(self):
        with self.assertRaises(ValueError):
            image_tool.pad_image(bytes(10))


class EntryTests(unittest.TestCase):
    def test_entries_are_fourteen_aligned_addresses_inside_the_upload(self):
        entries = image_tool.VCALLMS_ENTRIES
        self.assertEqual(len(entries), 14)
        self.assertEqual(len(set(entries)), 14)
        self.assertTrue(all(address % 8 == 0 and address < image_tool.UPLOAD_BYTES for address in entries))

    def test_entries_command_writes_list_and_sidecar_once(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / 'vu0_image.bin'
            image.write_bytes(bytes(image_tool.IMAGE_BYTES))
            out = Path(directory) / 'entries.txt'
            self.assertEqual(image_tool.main(['entries', '--image', str(image), '--out', str(out)]), 0)
            lines = out.read_text(encoding='ascii').split()
            self.assertEqual(lines[:3], ['0058', '0078', '00B8'])
            self.assertEqual(len(lines), 14)
            sidecar = json.loads(Path(str(out) + '.json').read_text(encoding='utf-8'))
            self.assertEqual(sidecar['entries'], 14)
            self.assertEqual(image_tool.main(['entries', '--image', str(image), '--out', str(out)]), 1)

    def test_build_refuses_non_fresh_output(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / 'stale.txt').write_text('x', encoding='utf-8')
            self.assertEqual(image_tool.main(['build', '--elf', str(Path(directory) / 'missing.elf'), '--out', directory]), 1)


class ControlTests(unittest.TestCase):
    @unittest.skipUnless(PRIVATE_IMAGE.exists(), 'private slice160 VU0 image not present')
    def test_private_image_matches_the_pin(self):
        self.assertEqual(image_tool.main(['check', '--image', str(PRIVATE_IMAGE)]), 0)
        digest = hashlib.sha256(PRIVATE_IMAGE.read_bytes()).hexdigest()
        self.assertEqual(digest, json.loads((ROOT / 'docs/inputs/slice160-vu0-image.json').read_text(encoding='utf-8'))['image']['sha256'])

    @unittest.skipUnless(PRIVATE_ELF.exists(), 'pinned reconstructed ELF not present')
    def test_private_elf_upload_matches_the_doc_hash(self):
        upload = image_tool.upload_bytes(PRIVATE_ELF.read_bytes())
        self.assertEqual(hashlib.sha256(upload).hexdigest(), '6ee4bdde81d98d0cf0cfdf65f5bb64697b4fd4bfee408d4d6125e39d8d602690')


if __name__ == '__main__':
    unittest.main()
