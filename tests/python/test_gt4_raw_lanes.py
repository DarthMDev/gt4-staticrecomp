"""Literal byte controls; no game payload or renderer interpretation."""

from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import gt4_raw_lanes as lanes
import gt4_vif_framing as framing


def decode(payload, *, bits=16, components=1, count=4, signed=True, alignment=b""):
    return lanes.decode_integer_lanes(payload, component_bits=bits, components=components,
                                      source_vectors=count, signed=signed, alignment=alignment)


class RawLaneTests(unittest.TestCase):
    def test_literal_signed_and_unsigned_16_bit_extrema(self):
        data = bytes.fromhex("00 80 FF FF 00 00 FF 7F")
        self.assertEqual(decode(data), ((-32768,), (-1,), (0,), (32767,)))
        self.assertEqual(decode(data, signed=False), ((32768,), (65535,), (0,), (32767,)))

    def test_literal_signed_and_unsigned_8_bit_extrema(self):
        data = bytes.fromhex("80 FF 00 7F")
        self.assertEqual(decode(data, bits=8), ((-128,), (-1,), (0,), (127,)))
        self.assertEqual(decode(data, bits=8, signed=False), ((128,), (255,), (0,), (127,)))

    def test_32_bit_patterns_remain_unsigned_integers(self):
        data = bytes.fromhex("00 00 00 80 FF FF FF FF 00 00 80 3F")
        for signed in (True, False):
            result = decode(data, bits=32, count=3, signed=signed)
            self.assertEqual(result, ((0x80000000,), (0xFFFFFFFF,), (0x3F800000,)))
            self.assertIs(type(result[2][0]), int)

    def test_literal_vector_grouping_and_little_endian(self):
        data = bytes.fromhex("01 00 02 00 03 00 FE FF FD FF FC FF")
        self.assertEqual(decode(data, components=3, count=2), ((1, 2, 3), (-2, -3, -4)))
        self.assertEqual(decode(bytes.fromhex("01 02 03 04 05 06 07 08"), bits=8, components=4, count=2),
                         ((1, 2, 3, 4), (5, 6, 7, 8)))

    def test_alignment_marker_never_becomes_a_lane(self):
        data = bytes.fromhex("11 22 33")
        self.assertEqual(decode(data, bits=8, components=3, count=1, alignment=b"\xA5"), ((17, 34, 51),))
        for padding in (b"", b"\xA5\x00"):
            with self.assertRaises(ValueError):
                decode(data, bits=8, components=3, count=1, alignment=padding)
        with self.assertRaises(ValueError):
            decode(data + b"\xA5", bits=8, components=3, count=1, alignment=b"\x00")

    def test_exact_payload_extent(self):
        for size in (7, 9, 12):
            with self.assertRaises(ValueError):
                decode(bytes(size))

    def test_parameter_types_ranges_and_packed_form_reject(self):
        for value in (5, 64, 0, True, "16"):
            with self.assertRaises(ValueError):
                decode(bytes(8), bits=value)
        for value in (0, 5, True, 1.0):
            with self.assertRaises(ValueError):
                decode(bytes(8), components=value)
        for value in (-1, 257, True, 4.0):
            with self.assertRaises(ValueError):
                decode(bytes(8), count=value)
        for value in (0, 1, None):
            with self.assertRaises(ValueError):
                decode(bytes(8), signed=value)

    def test_zero_source_count_requires_empty_extents(self):
        self.assertEqual(decode(b"", count=0), ())
        for data, padding in ((b"\x00", b""), (b"", b"\x00")):
            with self.assertRaises(ValueError):
                decode(data, count=0, alignment=padding)

    def test_immutable_inputs_and_work_caps(self):
        with self.assertRaises(ValueError):
            decode(bytearray(8))
        with self.assertRaises(ValueError):
            decode(bytes(8), alignment=bytearray())
        with patch.object(lanes, "MAX_LANES", 3), self.assertRaisesRegex(ValueError, "cap exceeded"):
            decode(bytes(8))
        with patch.object(lanes, "MAX_DATA_BYTES", 7), self.assertRaisesRegex(ValueError, "cap exceeded"):
            decode(bytes(8))

    def test_framed_fill_payload_is_not_filtered_twice(self):
        # Seven destination rows, CL2/WL3 => five source scalar32 values.
        data = bytes.fromhex("02 03 00 01 00 00 07 60 01 00 00 00 02 00 00 00 03 00 00 00 04 00 00 00 05 00 00 00")
        frame = framing.frame_packet(data, initial_cycle=(1, 1))["commands"][1]
        self.assertEqual((frame["vectors_raw_expanded"], frame["source_vectors"]), (7, 5))
        self.assertEqual(decode(data[8:], bits=32, count=frame["source_vectors"]),
                         ((1,), (2,), (3,), (4,), (5,)))

    def test_expanded_zero_num_and_maximum_lane_count(self):
        # NUM0 normalization belongs to framing; this helper receives256.
        result = decode(bytes.fromhex("FF FF FF FF") * 256, bits=32, count=256)
        self.assertEqual((len(result), result[0], result[-1]), (256, (0xFFFFFFFF,), (0xFFFFFFFF,)))
        result = decode(bytes(4096), bits=32, components=4, count=256)
        self.assertEqual((len(result), len(result[0])), (256, 4))


if __name__ == "__main__":
    unittest.main()
