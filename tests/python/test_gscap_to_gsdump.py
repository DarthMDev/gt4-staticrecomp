"""TASK-034: .gscap -> .gs converter (tools/gscap_to_gsdump). Pure checks on synthetic captures.

The converter writes the dump layout described in its module docstring. These tests parse the output
with an independent reader written from the same description, so a layout mistake shows up here.
"""
import io
import os
import struct
import sys
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "gscap_to_gsdump"))
import gscap_to_gsdump as converter  # noqa: E402

PRIV = 0x12000000
CSR = PRIV + 0x1000
FIELD = 0x2000
REGS_SIZE = 0x2000
SYNTHETIC = Path(os.environ.get(
    "GT4_GSCAP_SYNTHETIC",
    "C:/Antigravity/gt4-staticrecomp/private/slice173-synthetic/synthetic_menu.gscap"))


def gif(path, data):
    return b"G" + struct.pack("<BI", path, len(data)) + data


def priv(address, value):
    return b"P" + struct.pack("<IQ", address, value)


def fifo(qwords):
    return b"R" + struct.pack("<I", qwords)


def vsync(frame):
    return b"V" + struct.pack("<Q", frame)


def capture_bytes(*records):
    return b"".join(records)


def convert_bytes(capture, **kwargs):
    source = io.BytesIO(capture)
    destination = io.BytesIO()
    stats = converter.convert(source, destination, **kwargs)
    return destination.getvalue(), stats


def parse_dump(blob):
    """Independent reader for the dump layout: returns the header fields and the packet list."""
    marker, header_size = struct.unpack_from("<II", blob, 0)
    assert marker == 0xFFFFFFFF
    offset = 8
    header = struct.unpack_from("<9I", blob, offset)
    version, state_size, serial_offset, serial_size, crc = header[:5]
    block_start = 8
    serial = blob[block_start + serial_offset:block_start + serial_offset + serial_size]
    assert 8 + header_size == 8 + 36 + serial_size  # no screenshot data
    offset = 8 + header_size
    state = blob[offset:offset + state_size]
    offset += state_size
    initial_regs = blob[offset:offset + REGS_SIZE]
    offset += REGS_SIZE
    packets = []
    while offset < len(blob):
        kind = blob[offset]
        offset += 1
        if kind == 0x00:
            path, size = blob[offset], struct.unpack_from("<I", blob, offset + 1)[0]
            offset += 5
            packets.append(("transfer", path, blob[offset:offset + size]))
            offset += size
        elif kind == 0x02:
            (qwords,) = struct.unpack_from("<I", blob, offset)
            offset += 4
            packets.append(("fifo", qwords))
        elif kind == 0x03:
            packets.append(("regs", blob[offset:offset + REGS_SIZE]))
            offset += REGS_SIZE
        elif kind == 0x01:
            packets.append(("vsync", blob[offset]))
            offset += 1
        else:
            raise AssertionError(f"unknown packet {kind} at {offset - 1}")
    return {"version": version, "state_size": state_size, "crc": crc, "serial": serial,
            "state": state, "initial_regs": initial_regs, "packets": packets}


def csr_word(regs):
    return struct.unpack_from("<I", regs, 0x1000)[0]


class ReadCaptureTests(unittest.TestCase):
    def test_reads_all_four_record_kinds_in_order(self):
        data = capture_bytes(gif(1, b"\x11" * 16), priv(CSR, 0x8), fifo(4), vsync(7))
        records = list(converter.read_capture(io.BytesIO(data)))
        self.assertEqual([r.kind for r in records], ["G", "P", "R", "V"])
        self.assertEqual(records[0].path, 1)
        self.assertEqual(records[0].data, b"\x11" * 16)
        self.assertEqual((records[1].address, records[1].value), (CSR, 8))
        self.assertEqual(records[2].qwords, 4)
        self.assertEqual(records[3].frame, 7)

    def test_unknown_tag_is_refused(self):
        with self.assertRaises(converter.CaptureError):
            list(converter.read_capture(io.BytesIO(b"X" + b"\x00" * 8)))

    def test_truncated_record_is_refused(self):
        with self.assertRaises(converter.CaptureError):
            list(converter.read_capture(io.BytesIO(gif(1, b"\x00" * 16)[:-3])))


class DumpLayoutTests(unittest.TestCase):
    def test_header_serial_and_empty_state(self):
        blob, stats = convert_bytes(capture_bytes(vsync(0)), serial="SCUS-97328", crc=0x1234)
        dump = parse_dump(blob)
        self.assertEqual(dump["version"], converter.GS_STATE_VERSION)
        self.assertEqual(dump["serial"], b"SCUS-97328")
        self.assertEqual(dump["crc"], 0x1234)
        self.assertEqual(dump["state_size"], 0)
        self.assertEqual(dump["initial_regs"], bytes(REGS_SIZE))
        self.assertEqual(stats.frames, 1)

    def test_state_blob_is_copied_when_given(self):
        blob, _ = convert_bytes(capture_bytes(vsync(0)), state=b"STATE-BLOB")
        self.assertEqual(parse_dump(blob)["state"], b"STATE-BLOB")

    def test_packets_are_in_capture_order_with_register_block_before_each_vsync(self):
        capture = capture_bytes(gif(1, b"\xaa" * 32), priv(PRIV + 0x40, 0x1122), fifo(2), vsync(0),
                                gif(3, b"\xbb" * 16), vsync(1))
        dump = parse_dump(convert_bytes(capture)[0])
        kinds = [p[0] for p in dump["packets"]]
        self.assertEqual(kinds, ["transfer", "fifo", "regs", "vsync", "transfer", "regs", "vsync"])
        self.assertEqual(dump["packets"][0][2], b"\xaa" * 32)
        self.assertEqual(dump["packets"][0][1], converter.TRANSFER_PATH_NEW)
        self.assertEqual(dump["packets"][1][1], 2)
        self.assertEqual(dump["packets"][4][2], b"\xbb" * 16)

    def test_registers_hold_the_latest_privileged_write(self):
        capture = capture_bytes(priv(PRIV + 0x40, 0x1122), priv(PRIV + 0x40, 0x3344), vsync(0))
        regs = parse_dump(convert_bytes(capture)[0])["packets"][0][1]
        self.assertEqual(struct.unpack_from("<Q", regs, 0x40)[0], 0x3344)

    def test_out_of_range_privileged_writes_are_counted_not_stored(self):
        capture = capture_bytes(priv(0x10000000, 1), priv(PRIV + REGS_SIZE, 2), vsync(0))
        blob, stats = convert_bytes(capture)
        self.assertEqual(stats.privileged_writes_out_of_range, 2)
        self.assertEqual(stats.privileged_writes, 0)
        self.assertEqual(parse_dump(blob)["packets"][0][1], bytes(REGS_SIZE))

    def test_csr_reset_bit_is_counted(self):
        _, stats = convert_bytes(capture_bytes(priv(CSR, 0x200), vsync(0)))
        self.assertEqual(stats.csr_writes, 1)
        self.assertEqual(stats.csr_reset_writes, 1)


class FieldTests(unittest.TestCase):
    def fields(self, phase, frames):
        capture = capture_bytes(*[vsync(n) for n in range(frames)])
        dump = parse_dump(convert_bytes(capture, field_phase=phase)[0])
        regs = [p[1] for p in dump["packets"] if p[0] == "regs"]
        bytes_out = [p[1] for p in dump["packets"] if p[0] == "vsync"]
        return [bool(csr_word(r) & FIELD) for r in regs], bytes_out

    def test_csr_field_bit_alternates_each_vsync(self):
        bits, _ = self.fields(0, 4)
        self.assertEqual(bits, [False, True, False, True])

    def test_field_phase_one_flips_the_parity(self):
        bits, _ = self.fields(1, 4)
        self.assertEqual(bits, [True, False, True, False])

    def test_vsync_field_byte_is_zero_when_bit_is_set(self):
        bits, field_bytes = self.fields(0, 4)
        self.assertEqual(field_bytes, [0 if bit else 1 for bit in bits])

    def test_game_csr_bits_are_kept_apart_from_field_bit(self):
        capture = capture_bytes(priv(CSR, 0x8), vsync(0), vsync(1))
        dump = parse_dump(convert_bytes(capture)[0])
        first, second = [csr_word(p[1]) for p in dump["packets"] if p[0] == "regs"]
        self.assertEqual(first & ~FIELD, 0x8)
        self.assertEqual(second & ~FIELD, 0x8)
        self.assertNotEqual(first & FIELD, second & FIELD)


class LimitTests(unittest.TestCase):
    def test_max_frames_stops_after_that_many_vsyncs(self):
        capture = capture_bytes(vsync(0), gif(1, b"\x00" * 16), vsync(1), vsync(2))
        blob, stats = convert_bytes(capture, max_frames=2)
        self.assertEqual(stats.frames, 2)
        self.assertEqual(sum(1 for p in parse_dump(blob)["packets"] if p[0] == "vsync"), 2)

    def test_invalid_field_phase_is_refused(self):
        with self.assertRaises(ValueError):
            convert_bytes(capture_bytes(vsync(0)), field_phase=2)


class SyntheticCaptureTests(unittest.TestCase):
    @unittest.skipUnless(SYNTHETIC.exists(), "synthetic capture not present in this checkout")
    def test_synthetic_capture_converts_with_matching_vsync_count(self):
        with open(SYNTHETIC, "rb") as handle:
            records = list(converter.read_capture(handle))
        vsync_count = sum(1 for r in records if r.kind == "V")
        with open(SYNTHETIC, "rb") as source:
            destination = io.BytesIO()
            stats = converter.convert(source, destination)
        self.assertEqual(stats.frames, vsync_count)
        dump = parse_dump(destination.getvalue())
        self.assertEqual(sum(1 for p in dump["packets"] if p[0] == "vsync"), vsync_count)


class CompareFrameTests(unittest.TestCase):
    """Metrics of compare_frames.py on tiny synthetic images (no game data)."""

    def setUp(self):
        import compare_frames as frames
        self.frames = frames

    def test_identical_images_have_zero_bad_pixels_and_infinite_psnr(self):
        image = (2, 2, bytes(range(12)))
        result = self.frames.compare(image, image)
        self.assertEqual(result["pct_bad"], 0.0)
        self.assertEqual(result["max_delta"], 0)
        self.assertEqual(result["psnr"], "inf")

    def test_difference_of_eight_is_not_bad_but_nine_is(self):
        reference = (1, 1, bytes([10, 10, 10]))
        eight = (1, 1, bytes([18, 10, 10]))
        nine = (1, 1, bytes([19, 10, 10]))
        self.assertEqual(self.frames.compare(reference, eight)["pct_bad"], 0.0)
        result = self.frames.compare(reference, nine)
        self.assertEqual(result["pct_bad"], 100.0)
        self.assertEqual(result["max_delta"], 9)

    def test_pct_bad_counts_pixels_not_channels(self):
        reference = (2, 1, bytes(6))
        candidate = (2, 1, bytes([0, 0, 40, 0, 0, 0]))
        self.assertEqual(self.frames.compare(reference, candidate)["pct_bad"], 50.0)

    def test_size_mismatch_is_reported_not_scored(self):
        result = self.frames.compare((1, 1, bytes(3)), (2, 1, bytes(6)))
        self.assertIn("size_mismatch", result)

    def test_ppm_reader_accepts_p6_and_refuses_other_magic(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            good = Path(directory) / "good.ppm"
            good.write_bytes(b"P6\n2 1\n255\n" + bytes([1, 2, 3, 4, 5, 6]))
            self.assertEqual(self.frames.read_ppm(good), (2, 1, bytes([1, 2, 3, 4, 5, 6])))
            bad = Path(directory) / "bad.ppm"
            bad.write_bytes(b"P3\n1 1\n255\n0 0 0\n")
            with self.assertRaises(ValueError):
                self.frames.read_ppm(bad)

    def test_png_writer_output_decodes_back_with_zlib(self):
        import tempfile
        pixels = bytes(range(18))
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "x.png"
            self.frames.write_png(target, 3, 2, pixels)
            blob = target.read_bytes()
        self.assertEqual(blob[:8], b"\x89PNG\r\n\x1a\n")
        position = 8
        idat = b""
        while position < len(blob):
            (length,) = struct.unpack(">I", blob[position:position + 4])
            kind = blob[position + 4:position + 8]
            if kind == b"IDAT":
                idat += blob[position + 8:position + 8 + length]
            position += 12 + length
        raw = zlib.decompress(idat)
        self.assertEqual(raw, b"\x00" + pixels[0:9] + b"\x00" + pixels[9:18])

    def test_downscale_box_averages_blocks(self):
        image = (2, 2, bytes([0, 0, 0, 4, 4, 4, 8, 8, 8, 12, 12, 12]))
        self.assertEqual(self.frames.downscale_box(image, 2), (1, 1, bytes([6, 6, 6])))


# --- TASK-035: .gscap version 2 (GS stream protocol records). The writer below follows
# docs/GS-STREAM-PROTOCOL.md section 6 independently of the converter's reader.
class V2Writer:
    def __init__(self, serial=b"SCUS-97328", first_frame=0):
        self.blob = bytearray(struct.pack("<8sHHHHII16sQ16x", b"GSSTREAM", 2, 1, 0, 64, 16, 0, serial, first_frame))
        self.sequence = 0

    def record(self, kind, payload, flags=0):
        self.sequence += 1
        self.blob += struct.pack("<HHIQ", kind, flags, len(payload), self.sequence)
        self.blob += payload + bytes((-len(payload)) % 16)
        return self

    def gif(self, path, data, flags=0):
        return self.record(1, struct.pack("<B3xII4x", path, 1, len(data)) + data, flags)

    def priv(self, address, value):
        return self.record(2, struct.pack("<I4xQ", address, value))

    def vsync(self, frame, field_bit):
        return self.record(3, struct.pack("<QB3xI", frame, field_bit, 1))

    def fifo(self, qwords, request):
        return self.record(4, struct.pack("<I4xQ", qwords, request))

    def reset(self, kind, mask):
        return self.record(5, struct.pack("<II8x", kind, mask))

    def control(self, kind):
        return self.record(6, struct.pack("<I4x3Q", kind, 0, 0, 0))


NATIVE_BUILD = Path(os.environ.get("GT4_NATIVE_BUILD", ROOT / "build"))


class Version2CaptureTests(unittest.TestCase):
    def test_version2_converts_like_the_equivalent_version1_capture(self):
        data_a, data_b = bytes(range(32)), bytes(range(48))
        v2 = (V2Writer().reset(1, 0).priv(CSR, 0x200).gif(3, data_a).gif(1, data_b).fifo(2, 1)
              .vsync(0, 1).priv(PRIV + 0x70, 5).vsync(1, 0).control(1))
        v1 = capture_bytes(priv(CSR, 0x200), gif(3, data_a), gif(1, data_b), fifo(2), vsync(0),
                           priv(PRIV + 0x70, 5), vsync(1))
        blob2, stats2 = convert_bytes(bytes(v2.blob))
        blob1, _ = convert_bytes(v1)
        self.assertEqual(blob2, blob1)
        self.assertEqual(stats2.records, {"X": 1, "P": 2, "G": 2, "R": 1, "V": 2, "C": 1})

    def test_more_parts_are_joined_into_one_transfer(self):
        part1, part2 = bytes([1]) * 32, bytes([2]) * 16
        v2 = V2Writer().gif(2, part1, flags=1).gif(2, part2).vsync(0, 0)
        records = list(converter.read_capture(io.BytesIO(bytes(v2.blob))))
        self.assertEqual([r.kind for r in records], ["G", "V"])
        self.assertEqual((records[0].path, records[0].data), (2, part1 + part2))
        self.assertEqual(records[1].field, 0)

    def test_field_from_stream_uses_the_recorded_bit(self):
        v2 = V2Writer().vsync(0, 1).vsync(1, 1)
        blob, _ = convert_bytes(bytes(v2.blob), field_from_stream=True)
        fields = [packet[1] for packet in parse_dump(blob)["packets"] if packet[0] == "vsync"]
        self.assertEqual(fields, [0, 0])  # field bit 1 -> field byte 0, twice
        blob, _ = convert_bytes(bytes(v2.blob))
        fields = [packet[1] for packet in parse_dump(blob)["packets"] if packet[0] == "vsync"]
        self.assertEqual(fields, [1, 0])  # default: frame parity, as for version 1

    def test_malformed_version2_input_is_refused(self):
        good = V2Writer().gif(3, bytes(16))
        cases = {
            "bad version": bytes(good.blob[:8]) + struct.pack("<H", 3) + bytes(good.blob[10:]),
            "truncated": bytes(good.blob[:-8]),
            "dangling more": bytes(V2Writer().gif(3, bytes(16), flags=1).blob),
            "size mismatch": bytes(V2Writer().record(1, struct.pack("<B3xII4x", 3, 0, 32) + bytes(16)).blob),
            "unknown type": bytes(V2Writer().record(99, bytes(16)).blob),
        }
        skipped = V2Writer().gif(3, bytes(16))
        skipped.sequence += 1
        skipped.gif(3, bytes(16))
        cases["sequence gap"] = bytes(skipped.blob)
        for name, blob in cases.items():
            with self.subTest(name):
                with self.assertRaises(converter.CaptureError):
                    list(converter.read_capture(io.BytesIO(blob)))

    def test_native_client_fixture_capture_converts_if_built(self):
        """The C++ writer (CTest native_gs_stream_client) and this reader agree on the format."""
        capture = NATIVE_BUILD / "gs-stream-client-scratch" / "donor-capture.gscap"
        if not capture.exists():
            self.skipTest(f"no native fixture capture at {capture}")
        with open(capture, "rb") as handle:
            records = list(converter.read_capture(handle))
        kinds = [r.kind for r in records]
        self.assertEqual(kinds.count("V"), 10)
        self.assertEqual(kinds.count("X"), 2)
        self.assertEqual(kinds.count("R"), 1)
        self.assertEqual(kinds.count("G"), 31)  # 30 per-frame chunks + one split chunk joined from two parts
        self.assertTrue(any(len(r.data) == (16 << 20) + 48 for r in records if r.kind == "G"))
        self.assertEqual([r.field for r in records if r.kind == "V"], [i & 1 for i in range(10)])


if __name__ == "__main__":
    unittest.main()
