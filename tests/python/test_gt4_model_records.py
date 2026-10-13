"""Hand-authored fixed-record and sentinel controls, independent of game assets."""

from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import gt4_model_records as records


def fixture(*, nested=False):
    data = bytearray(0x500)
    data[:4] = b"MDLS"
    struct.pack_into("<I", data, 0x10, len(data))
    struct.pack_into("<5H", data, 0x16, 0, 1, 1, 1, 1)
    struct.pack_into("<4I", data, 0x38, 0, 0x80, 0x240, 0x84)
    struct.pack_into("<3I", data, 0x80, 0x120, 0x88, 0x300)
    struct.pack_into("<I", data, 0x124, 0xFFFFFFFF)  # Opaque, not a size.
    struct.pack_into("<BBHHH", data, 0x128, 4, 9, 2, 6, 2)
    struct.pack_into("<IHH", data, 0x130, 0x40, 2, 1 | (1 << 9))
    struct.pack_into("<IHH", data, 0x138, 0x60, 1, 511 | (127 << 9))
    data[0x300:0x304] = b"Tex1"
    struct.pack_into("<I", data, 0x30C, 0x200)
    struct.pack_into("<HHIII", data, 0x314, 1, 1, 0x30, 0x58, 0)
    struct.pack_into("<IHBBHH", data, 0x358, 0x80, 3, 1, 0, 4, 2)
    if not nested:
        return bytes(data)
    car = bytearray(0x60)
    car[:4] = b"CAR4"
    struct.pack_into("<I", car, 0x18, len(car))
    return bytes(car + data)


class ModelRecordTests(unittest.TestCase):
    def test_three_relative_bases_and_fixed_record_counts(self):
        for nested in (False, True):
            result = records.inventory_asset(fixture(nested=nested))
            self.assertEqual(result["main_model_offset"], 0x60 if nested else 0)
            self.assertEqual(result["total_descriptors"], 2)
            self.assertEqual(result["material_record_count"], 1)
            shape = result["shapes"][0]
            self.assertEqual(shape["word_04_raw"], 0xFFFFFFFF)
            self.assertEqual([part["model_relative_offset"] for part in shape["descriptors"]], [0x160, 0x180])
            transfer = result["texture_sets"][0]["transfers"][0]
            self.assertEqual((transfer["data_offset_raw"], transfer["width_raw"], transfer["height_raw"]), (0x80, 4, 2))
            self.assertFalse(transfer["pixels_decoded"])

    def test_texture_and_material_sentinels(self):
        for raw, kind, index in ((0, "none", None), (1, "local", 0),
                                 (510, "local", 509), (511, "external", None)):
            result = records.flags_metadata(raw | (127 << 9))
            self.assertEqual((result["texture_kind"], result["texture_index"]), (kind, index))
            self.assertEqual(result["material_index"], 126)
        self.assertIsNone(records.flags_metadata(0)["material_index"])
        self.assertEqual(records.flags_metadata(1 << 9)["material_index"], 0)

    def test_shape_prefix_and_descriptor_array_bounds(self):
        for offset, count in ((0x4F1, 0), (0x4F0, 1), (0x4E8, 2)):
            data = bytearray(fixture())
            struct.pack_into("<I", data, 0x80, offset)
            if offset + 16 <= len(data):
                struct.pack_into("<H", data, offset + 10, count)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                records.inventory_asset(bytes(data))

    def test_packet_bounds_exact_end_and_one_past(self):
        data = bytearray(fixture())
        struct.pack_into("<IHH", data, 0x130, 0x3D0, 1, 0)
        records.inventory_asset(bytes(data))  # 0x120 + 0x3D0 + 16 == 0x500.
        for offset, count in ((0x3D1, 1), (0xFFFFFFFF, 1), (0x40, 0xFFFF)):
            struct.pack_into("<IHH", data, 0x130, offset, count, 0)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                records.inventory_asset(bytes(data))

    def test_packet_cannot_point_into_own_fixed_records(self):
        for offset in (0, 0x10, 0x1F):
            data = bytearray(fixture())
            struct.pack_into("<I", data, 0x130, offset)
            with self.assertRaisesRegex(ValueError, "overlaps"):
                records.inventory_asset(bytes(data))

    def test_zero_quadwords_are_opaque_no_read(self):
        data = bytearray(fixture())
        struct.pack_into("<IHH", data, 0x130, 0xFFFFFFFF, 0, 0)
        part = records.inventory_asset(bytes(data))["shapes"][0]["descriptors"][0]
        self.assertEqual(part["shape_relative_offset"], 0xFFFFFFFF)
        self.assertIsNone(part["model_relative_offset"])
        self.assertEqual(part["packet_bytes"], 0)

    def test_packet_aliases_and_unaligned_starts_reported_without_decoding(self):
        data = bytearray(fixture())
        for position in (0x130, 0x138):
            struct.pack_into("<IHH", data, position, 0x41, 1, 0)
        parts = records.inventory_asset(bytes(data))["shapes"][0]["descriptors"]
        self.assertEqual(parts[0]["model_relative_offset"], parts[1]["model_relative_offset"])
        self.assertFalse(parts[0]["absolute_alignment_16"])

    def test_descriptor_work_caps(self):
        data = bytearray(fixture())
        struct.pack_into("<H", data, 0x12A, 4097)
        with self.assertRaisesRegex(ValueError, "count cap"):
            records.inventory_asset(bytes(data))
        with patch.object(records, "MAX_TOTAL_DESCRIPTORS", 1), self.assertRaisesRegex(ValueError, "aggregate descriptor"):
            records.inventory_asset(fixture())

    def test_material_and_texture_table_full_extents(self):
        for field, value in ((0x40, 0x4B1), (0x318, 0x1D9), (0x31C, 0x1F5)):
            data = bytearray(fixture())
            struct.pack_into("<I", data, field, value)
            with self.subTest(field=field), self.assertRaises(ValueError):
                records.inventory_asset(bytes(data))

    def test_transfer_start_only_and_unknown_format_preserved(self):
        data = bytearray(fixture())
        struct.pack_into("<IHBBHH", data, 0x358, 0x1FF, 0, 0, 0xFF, 0xFFFF, 0xFFFF)
        transfer = records.inventory_asset(bytes(data))["texture_sets"][0]["transfers"][0]
        self.assertEqual(transfer["pixel_format_raw"], 0xFF)
        self.assertEqual(transfer["data_extent"], "Unknown; start only checked")
        for offset in (0, 0x2F, 0x200, 0xFFFFFFFF):
            struct.pack_into("<I", data, 0x358, offset)
            with self.assertRaises(ValueError):
                records.inventory_asset(bytes(data))

    def test_texture_work_cap_and_zero_size_slot(self):
        with patch.object(records, "MAX_TOTAL_TEXTURE_RECORDS", 1), self.assertRaisesRegex(ValueError, "aggregate texture"):
            records.inventory_asset(fixture())
        data = bytearray(fixture())
        struct.pack_into("<I", data, 0x30C, 0)
        data[0x314:0x32C] = bytes(0x18)
        texture = records.inventory_asset(bytes(data))["texture_sets"][0]
        self.assertEqual(texture["textures"], [])
        self.assertEqual(texture["transfers"], [])


if __name__ == "__main__":
    unittest.main()
