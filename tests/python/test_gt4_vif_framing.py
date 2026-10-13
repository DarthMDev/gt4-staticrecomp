"""Hand-authored VIF boundaries, cycle filling and zero-count controls."""

from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import gt4_vif_framing as framing


def code(opcode, number=0, immediate=0):
    return struct.pack("<HBB", immediate, number, opcode)


def scan(data, cycle=(1, 1)):
    return framing.frame_packet(data, initial_cycle=cycle)


class VifFramingTests(unittest.TestCase):
    def test_no_payload_irq_and_padding_nops(self):
        result = scan(code(0x94, 0, 0x123) + bytes(12))
        self.assertEqual([entry["name"] for entry in result["commands"]], ["MSCAL", "NOP", "NOP", "NOP"])
        self.assertTrue(result["commands"][0]["irq"])

    def test_mask_row_column_fixed_lengths(self):
        data = code(0x20) + bytes(4) + code(0x30) + bytes(16) + code(0x31) + bytes(16)
        self.assertEqual([entry["byte_offset"] for entry in scan(data)["commands"]], [0, 8, 28])
        for opcode, length in ((0x20, 4), (0x30, 16), (0x31, 16)):
            with self.assertRaisesRegex(ValueError, "exceeds packet"):
                scan(code(opcode) + bytes(length - 4))

    def test_unpack_widths_components_and_alignment(self):
        for opcode, number, raw_length in ((0x60, 2, 8), (0x65, 3, 12),
                                           (0x6A, 1, 3), (0x7E, 2, 8)):
            aligned = (raw_length + 3) & ~3
            result = scan(code(opcode, number) + bytes(aligned) + code(0))
            self.assertEqual(result["commands"][0]["payload_bytes"], raw_length)
            self.assertEqual(result["commands"][1]["byte_offset"], 4 + aligned)

    def test_zero_num_unpack_is_256(self):
        entry = scan(code(0x62) + bytes(256))["commands"][0]
        self.assertEqual((entry["vectors_raw_expanded"], entry["payload_bytes"]), (256, 256))
        with self.assertRaisesRegex(ValueError, "exceeds packet"):
            scan(code(0x62))

    def test_fill_cycle_reduces_input_not_destination_count(self):
        # CL=2, WL=3, 7 destination rows consume rows 0,1,3,4,6: five inputs.
        result = scan(code(1, immediate=0x0302) + code(0x60, 7) + bytes(20))
        unpack = result["commands"][1]
        self.assertEqual((unpack["vectors_raw_expanded"], unpack["source_vectors"], unpack["payload_bytes"]), (7, 5, 20))
        self.assertEqual(result["final_cycle_cl_wl"], [2, 3])

    def test_skip_cycle_consumes_all_input_and_explicit_initial_fill(self):
        self.assertEqual(scan(code(0x60, 7) + bytes(28), (3, 2))["commands"][0]["source_vectors"], 7)
        self.assertEqual(scan(code(0x60, 4) + bytes(8), (1, 3))["commands"][0]["source_vectors"], 2)

    def test_gt4_marker_stays_ordinary_v3_32_framing(self):
        # 0x6801C0C0 has a standard 12-byte UNPACK payload. Its possible GIF
        # meaning is not a second payload rule in this conditional scanner.
        entry = scan(code(0x68, 1, 0xC0C0) + bytes(12))["commands"][0]
        self.assertEqual((entry["name"], entry["payload_bytes"]), ("UNPACK", 12))

    def test_microprogram_and_direct_count_units(self):
        self.assertEqual(scan(code(0x4A, 2) + bytes(16))["commands"][0]["payload_bytes"], 16)
        self.assertEqual(scan(code(0x4A) + bytes(2048))["commands"][0]["payload_bytes"], 2048)
        for opcode in (0x50, 0x51):
            self.assertEqual(scan(code(opcode, immediate=2) + bytes(32))["commands"][0]["payload_bytes"], 32)
            with self.assertRaisesRegex(ValueError, "exceeds packet"):
                scan(code(opcode))  # Zero means 65,536 QW, never empty.

    def test_cycle_must_be_explicit_nonzero_supported_fields(self):
        for cycle in (None, [1, 1], (0, 1), (1, 0), (256, 1), (True, 1)):
            with self.assertRaises(ValueError):
                scan(bytes(4), cycle)
        for immediate in (0, 0x0100, 0x0001):
            with self.assertRaisesRegex(ValueError, "zero-field"):
                scan(code(1, immediate=immediate))

    def test_reserved_and_unqualified_forms_reject(self):
        for opcode in (0x08, 0x12, 0x32, 0x40, 0x63, 0x67, 0x6B, 0x6F, 0x7F):
            with self.subTest(opcode=opcode), self.assertRaises(ValueError):
                scan(code(opcode, 1) + bytes(16))

    def test_short_oversized_and_unaligned_packets(self):
        for data in (b"", bytes(3), bytes(5), bytes(framing.MAX_PACKET_BYTES + 4)):
            with self.assertRaises(ValueError):
                scan(data)
        with patch.object(framing, "MAX_COMMANDS", 1), self.assertRaisesRegex(ValueError, "count cap"):
            scan(bytes(8))


if __name__ == "__main__":
    unittest.main()
