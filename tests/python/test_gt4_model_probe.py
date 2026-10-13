"""Hand-packed nested models exercise bases, declared extents and indirection."""

from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import gt4_model_probe as probe


def model_fixture(*, nested=False):
    # MDLS with one model, one shape, two colors x two texture LODs.
    data = bytearray(0x240)
    data[:4] = b"MDLS"
    struct.pack_into("<I", data, 0x10, len(data))
    struct.pack_into("<5H", data, 0x16, 1, 1, 0, 2, 2)
    struct.pack_into("<4I", data, 0x38, 0x80, 0xA8, 0, 0xAC)
    struct.pack_into("<I", data, 0xA8, 0xF0)
    struct.pack_into("<2I", data, 0xAC, 0xB4, 0xBC)
    struct.pack_into("<2I", data, 0xB4, 0x100, 0x140)
    struct.pack_into("<2I", data, 0xBC, 0x180, 0x1C0)
    for offset in (0x100, 0x140, 0x180, 0x1C0):
        data[offset:offset + 4] = b"Tex1"
        struct.pack_into("<I", data, offset + 0x0C, 0x30)
    if not nested:
        return bytes(data)
    car = bytearray(0x60)
    car[:4] = b"CAR4"
    struct.pack_into("<I", car, 0x18, len(car))
    return bytes(car + data)


class ModelProbeTests(unittest.TestCase):
    def test_nested_and_standalone_bases_and_texture_buckets(self):
        for nested in (False, True):
            result = probe.probe_asset(model_fixture(nested=nested))
            model = result["main_model"]
            base = 0x60 if nested else 0
            self.assertEqual(model["file_offset"], base)
            self.assertEqual((model["model_count"], model["shape_count"]), (1, 1))
            self.assertEqual([(item["color"], item["lod"], item["file_offset"])
                              for item in model["textures"]],
                             [(0, 0, base + 0x100), (0, 1, base + 0x140),
                              (1, 0, base + 0x180), (1, 1, base + 0x1C0)])
            self.assertFalse(model["relocation_applied"])

    def test_declared_mdls_extent_precedes_parent_end(self):
        data = bytearray(model_fixture())
        struct.pack_into("<I", data, 0x10, 0xB0)
        with self.assertRaises(ValueError):
            probe.probe_asset(bytes(data))

    def test_header_truncations_and_signatures(self):
        for data in (b"", b"CAR4" + bytes(0x37), b"MDLS" + bytes(0x7B),
                     b"PRM0" + bytes(128)):
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                probe.probe_asset(data)

    def test_main_offset_and_nested_magic(self):
        for offset in (0, 0x30, 0xFFFFFFFF, len(model_fixture(nested=True)) - 0x7F):
            data = bytearray(model_fixture(nested=True))
            struct.pack_into("<I", data, 0x18, offset)
            with self.assertRaises(ValueError):
                probe.probe_asset(bytes(data))
        data = bytearray(model_fixture(nested=True))
        data[0x60] = 0
        with self.assertRaises(ValueError):
            probe.probe_asset(bytes(data))

    def test_mdls_declared_sizes(self):
        for size in (0, 0x7F, 0xFFFFFFFF):
            data = bytearray(model_fixture())
            struct.pack_into("<I", data, 0x10, size)
            with self.assertRaises(ValueError):
                probe.probe_asset(bytes(data))

    def test_mdls_count_caps(self):
        for offset, count in ((0x16, 4097), (0x18, 8193), (0x1A, 4097),
                              (0x1C, 33), (0x1E, 257)):
            data = bytearray(model_fixture())
            struct.pack_into("<H", data, offset, count)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                probe.probe_asset(bytes(data))

    def test_texture_product_cap(self):
        data = bytearray(model_fixture())
        struct.pack_into("<HH", data, 0x1C, 32, 256)
        with self.assertRaisesRegex(ValueError, "texture count cap"):
            probe.probe_asset(bytes(data))

    def test_table_pointer_bounds(self):
        for field in (0x38, 0x3C, 0x44, 0xAC, 0xB4, 0xA8):
            for value in (0, 0xFFFFFFFF, 0x240):
                data = bytearray(model_fixture())
                struct.pack_into("<I", data, field, value)
                with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                    probe.probe_asset(bytes(data))

    def test_model_bounds_and_command_pointers(self):
        data = bytearray(model_fixture())
        data[0x81] = 1
        struct.pack_into("<I", data, 0x84, 0x234)
        self.assertEqual(probe.probe_asset(bytes(data))["main_model"]["models"][0]["bounds_count"], 1)
        struct.pack_into("<I", data, 0x84, 0x235)
        with self.assertRaises(ValueError):
            probe.probe_asset(bytes(data))
        data = bytearray(model_fixture())
        struct.pack_into("<I", data, 0x98, 0xFFFFFFFF)
        with self.assertRaises(ValueError):
            probe.probe_asset(bytes(data))

    def test_vm_words_are_signed_metadata_without_pointer_interpretation(self):
        data = bytearray(model_fixture())
        struct.pack_into("<3i", data, 0x9C, -1, 0, 0x7FFFFFFF)
        record = probe.probe_asset(bytes(data))["main_model"]["models"][0]
        self.assertEqual((record["constructor_vm_raw"], record["update_vm_raw"],
                          record["callback_vm_raw"]), (-1, 0, 0x7FFFFFFF))
        self.assertEqual(record["vm_word_semantics"], "Unknown")

    def test_tex1_signature_and_sizes(self):
        for field, value in ((0x100, 0), (0x10C, 1), (0x10C, 0x2F), (0x10C, 0x141)):
            data = bytearray(model_fixture())
            struct.pack_into("<I", data, field, value)
            with self.assertRaises(ValueError):
                probe.probe_asset(bytes(data))

    def test_zero_size_tex1_is_header_only(self):
        data = bytearray(model_fixture())
        struct.pack_into("<I", data, 0x10C, 0)
        texture = probe.probe_asset(bytes(data))["main_model"]["textures"][0]
        self.assertEqual(texture["declared_size"], 0)
        self.assertEqual(texture["extent_classification"],
                         "Header only; zero-size slot semantics Unknown")

    def test_zero_size_tex1_rejects_any_subrecord_references(self):
        for offset in (0x114, 0x116, 0x118, 0x11C, 0x120, 0x124, 0x128):
            data = bytearray(model_fixture())
            struct.pack_into("<I", data, 0x10C, 0)
            data[offset] = 1
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, "zero-size Tex1"):
                probe.probe_asset(bytes(data))

    def test_tex1_optional_tables(self):
        data = bytearray(model_fixture())
        struct.pack_into("<I", data, 0x10C, 0x40)
        struct.pack_into("<HH", data, 0x114, 1, 1)
        struct.pack_into("<III", data, 0x118, 0x30, 0x34, 0x38)
        texture = probe.probe_asset(bytes(data))["main_model"]["textures"][0]
        self.assertEqual((texture["texture_count"], texture["transfer_count"]), (1, 1))
        for field in (0x118, 0x11C, 0x120):
            bad = bytearray(data)
            struct.pack_into("<I", bad, field, 0x40)
            with self.assertRaises(ValueError):
                probe.probe_asset(bytes(bad))

    def test_texture_counts_and_material_pointer(self):
        for field in (0x114, 0x116):
            data = bytearray(model_fixture())
            struct.pack_into("<H", data, field, 4097)
            with self.assertRaises(ValueError):
                probe.probe_asset(bytes(data))
        data = bytearray(model_fixture())
        struct.pack_into("<H", data, 0x1A, 1)
        with self.assertRaises(ValueError):
            probe.probe_asset(bytes(data))

    def test_untextured_model_and_shared_texture_headers(self):
        data = bytearray(model_fixture())
        struct.pack_into("<H", data, 0x1C, 0)
        self.assertEqual(probe.probe_asset(bytes(data))["main_model"]["textures"], [])
        data = bytearray(model_fixture())
        struct.pack_into("<2I", data, 0xBC, 0x100, 0x140)
        textures = probe.probe_asset(bytes(data))["main_model"]["textures"]
        self.assertEqual(textures[0]["file_offset"], textures[2]["file_offset"])

    def test_asset_size_cap(self):
        with self.assertRaisesRegex(ValueError, "input cap"):
            probe.probe_asset(b"MDLS" + bytes(probe.MAX_FILE_BYTES))


if __name__ == "__main__":
    unittest.main()
