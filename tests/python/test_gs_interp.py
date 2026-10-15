"""TASK-031 Prototype A: parser, encoder, primitive matching and interpolation (tools/gs_interp).

The tests build small captures in memory. One test decodes the first frames of the menu
capture when it is present under private/ (skipped otherwise).
"""
from pathlib import Path
import math
import os
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from gs_interp import capture as capture_io  # noqa: E402
from gs_interp import gif  # noqa: E402
from gs_interp import metrics  # noqa: E402
from gs_interp import match as matcher  # noqa: E402
from gs_interp import prims  # noqa: E402
from gs_interp import synth  # noqa: E402

# The capture is owner data under private/ (main checkout); point GT4_INTERP_MENU_CAPTURE at it.
MENU_CAPTURE = Path(os.environ["GT4_INTERP_MENU_CAPTURE"]) if os.environ.get("GT4_INTERP_MENU_CAPTURE")     else ROOT / "private/slice175-capture/menu-0-1100.gscap"


def float_bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def xyz(x, y, z):
    return x | (y << 16) | (z << 32)


def decode_all(data: bytes):
    stream = gif.GifStream()
    events = []
    for record in capture_io.iter_records(data):
        if record.tag == "G":
            events.extend(stream.feed(record.path, record.payload))
        elif record.tag == "P":
            events.append(("P", record.address, record.value))
        elif record.tag == "R":
            events.append(("R", record.value))
    return events


def triangle_events(path, prim, vertices, colour=0x40):
    """Events for one triangle: PRIM, then per vertex RGBAQ, ST and an XYZ2 kick."""
    events = [("op", path, gif.REG_PRIM, prim)]
    for (x, y, z), (s, t, q) in vertices:
        events.append(("op", path, gif.REG_RGBAQ, colour | (float_bits(q) << 32)))
        events.append(("op", path, gif.REG_ST, float_bits(s) | (float_bits(t) << 32)))
        events.append(("kick", path, gif.REG_XYZ2, xyz(x, y, z), True))
    return events


class TagAndCodecTests(unittest.TestCase):
    def test_nreg_zero_means_sixteen(self):
        lo = (2 << 58) | (0 << 60)  # image mode, NREG field 0
        tag = gif.parse_tag(struct.pack("<QQ", lo | 4, 0))
        self.assertEqual(tag.nreg, 16)
        self.assertEqual(tag.data_bytes, 4 * 16)

    def test_encode_then_decode_keeps_ops_kicks_and_drawing_flag(self):
        events = [("op", 1, gif.REG_PRIM, 3), ("op", 1, gif.REG_TEX0_1, 0x1234)]
        events.append(("kick", 1, gif.REG_XYZ2, xyz(10, 20, 30), True))
        events.append(("kick", 1, gif.REG_XYZ2, xyz(11, 21, 31), False))
        events.append(("kick", 1, gif.REG_XYZF3, xyz(12, 22, 32) | (7 << 56), False))
        events.append(("op", 1, gif.REG_ZBUF_1, 0x99))
        records = gif.encode_events(events)
        data = b"".join(capture_io.serialize_record(record) for record in records)
        self.assertEqual(decode_all(data), events)

    def test_packed_quads_follow_the_descriptor_rules(self):
        # Packed tag, two descriptors per loop: ST then RGBAQ, one loop.
        lo = 1 | (2 << 60) | (0 << 58)
        hi = 0x2 | (0x1 << 4)
        st_quad = struct.pack("<IIII", float_bits(0.5), float_bits(0.25), float_bits(2.0), 0)
        rgbaq_quad = struct.pack("<IIII", 10, 20, 30, 40)
        stream = gif.GifStream()
        events = stream.feed(1, struct.pack("<QQ", lo, hi) + st_quad + rgbaq_quad)
        self.assertEqual(events[0], ("op", 1, gif.REG_ST, float_bits(0.5) | (float_bits(0.25) << 32)))
        self.assertEqual(events[1], ("op", 1, gif.REG_RGBAQ,
                                     10 | (20 << 8) | (30 << 16) | (40 << 24) | (float_bits(2.0) << 32)))

    def test_packed_xyz_adc_bit_marks_a_non_drawing_kick(self):
        lo = 1 | (1 << 60)
        hi = 0x5
        drawing_quad = struct.pack("<IIII", 5, 6, 7, 0)
        adc_quad = struct.pack("<IIII", 5, 6, 7, 1 << 15)
        stream = gif.GifStream()
        events = stream.feed(0, struct.pack("<QQ", lo, hi) + drawing_quad)
        events += stream.feed(0, struct.pack("<QQ", lo, hi) + adc_quad)
        self.assertEqual([event[4] for event in events], [True, False])

    def test_tag_data_may_continue_into_the_next_packet_of_the_same_path(self):
        image_tag = struct.pack("<QQ", 2 | (2 << 58), 0)  # two image qwords
        payload = image_tag + b"\x11" * 16 + b"\x22" * 16
        stream = gif.GifStream()
        first = stream.feed(3, payload[:20])
        second = stream.feed(3, payload[20:])
        self.assertEqual(first, [])
        self.assertEqual(len(second), 1)
        self.assertEqual(second[0][3], b"\x11" * 16 + b"\x22" * 16)


class MatchingTests(unittest.TestCase):
    def make(self, frame, kind, key, count=3):
        vertices = [prims.Vertex(i, gif.REG_XYZ2, xyz(i, i, 0), 0, 0) for i in range(count)]
        return prims.Primitive(frame=frame, kind=kind, key=key, vertices=vertices)

    def test_identical_frames_match_completely(self):
        frame = [self.make(0, prims.PRIM_TRIANGLE, ("k", 1)) for _ in range(4)]
        result = matcher.match_primitives(frame, frame)
        self.assertEqual(result.matched, 4)
        self.assertEqual(result.rate_b, 1.0)

    def test_runs_of_one_context_pair_by_order_within_the_run(self):
        a = [self.make(0, prims.PRIM_TRIANGLE, ("x",)), self.make(0, prims.PRIM_TRIANGLE, ("x",)),
             self.make(0, prims.PRIM_SPRITE, ("y",), count=2)]
        b = [self.make(1, prims.PRIM_TRIANGLE, ("x",)), self.make(1, prims.PRIM_SPRITE, ("y",), count=2)]
        result = matcher.match_primitives(a, b)
        self.assertEqual(result.pairs, {0: 0, 1: 2})
        self.assertEqual(result.matched, 2)
        self.assertEqual(result.rate_a, 2 / 3)

    def test_kind_or_vertex_count_mismatch_is_not_matched(self):
        a = [self.make(0, prims.PRIM_TRIANGLE, ("x",))]
        b = [self.make(1, prims.PRIM_TRIANGLE, ("x",), count=2)]
        self.assertEqual(matcher.match_primitives(a, b).matched, 0)

    def test_strips_are_matched_but_not_substitutable(self):
        a = [self.make(0, prims.PRIM_TRIANGLE_STRIP, ("s",))]
        b = [self.make(1, prims.PRIM_TRIANGLE_STRIP, ("s",))]
        b[0].shared = True
        result = matcher.match_primitives(a, b)
        self.assertEqual(result.matched, 1)
        self.assertEqual(result.substitutable, 0)


class InterpolationTests(unittest.TestCase):
    def vertex(self, x, y, z, s, t, q):
        xyz_value = xyz(x, y, z)
        rgbaq = 0x40 | (float_bits(q) << 32)
        st = float_bits(s) | (float_bits(t) << 32)
        return prims.Vertex(0, gif.REG_XYZ2, xyz_value, rgbaq, st)

    def test_midpoint_of_positions_and_colours(self):
        a = prims.Vertex(0, gif.REG_XYZ2, xyz(100, 200, 1000), 0x20 | (float_bits(1.0) << 32), 0)
        b = prims.Vertex(0, gif.REG_XYZ2, xyz(120, 180, 2000), 0x60 | (float_bits(1.0) << 32), 0)
        value = synth.lerp_vertex(a, b, gif.REG_XYZ2, 0.5)
        self.assertEqual(value.xyz, xyz(110, 190, 1500))
        self.assertEqual(value.rgbaq & 0xFF, 0x40)

    def test_extrapolation_with_t_two_predicts_the_next_position(self):
        q_one = float_bits(1.0) << 32
        a = prims.Vertex(0, gif.REG_XYZ2, xyz(100, 100, 0), q_one, 0)
        b = prims.Vertex(0, gif.REG_XYZ2, xyz(104, 98, 0), q_one, 0)
        value = synth.lerp_vertex(a, b, gif.REG_XYZ2, 2.0)
        self.assertEqual(value.xyz, xyz(108, 96, 0))

    def test_perspective_correct_st_at_midpoint(self):
        a = self.vertex(0, 0, 0, s=0.0, t=0.0, q=1.0)
        b = self.vertex(0, 0, 0, s=1.0, t=1.0, q=2.0)
        value = synth.lerp_vertex(a, b, gif.REG_XYZ2, 0.5)
        q_new = struct.unpack("<f", struct.pack("<I", value.rgbaq >> 32))[0]
        s_new = struct.unpack("<f", struct.pack("<I", value.st & 0xFFFFFFFF))[0]
        self.assertAlmostEqual(q_new, 4.0 / 3.0, places=5)
        self.assertAlmostEqual(s_new, 1.0 / 3.0, places=5)

    def test_undefined_perspective_is_rejected(self):
        a = self.vertex(0, 0, 0, s=0.0, t=0.0, q=1.0)
        b = self.vertex(0, 0, 0, s=1.0, t=1.0, q=4.0)
        # 1/W = (1 - t) * 1 + t * 0.25 is negative at t = 2: no valid perspective.
        self.assertIsNone(synth.lerp_vertex(a, b, gif.REG_XYZ2, 2.0))

    def test_synthesis_rewrites_kicks_and_restores_the_latch(self):
        events_a = triangle_events(1, 3, [((0, 0, 0), (0, 0, 1)), ((64, 0, 0), (0, 0, 1)),
                                          ((0, 64, 0), (0, 0, 1))])
        events_b = triangle_events(1, 3, [((16, 0, 0), (0, 0, 1)), ((80, 0, 0), (0, 0, 1)),
                                          ((16, 64, 0), (0, 0, 1))])
        frame_a = prims.PrimitiveBuilder().build(0, events_a)
        frame_b = prims.PrimitiveBuilder().build(1, events_b)
        result = synth.synthesize(events_b, frame_a, frame_b, 0.5)
        self.assertEqual(result.substituted, 1)
        kicks = [event for event in result.events if event[0] == "kick"]
        self.assertEqual([event[3] & 0xFFFF for event in kicks], [8, 72, 8])
        self.assertEqual(len(result.events), len(events_b) + 3 * 2 + 2)
        last_kick = max(i for i, event in enumerate(result.events) if event[0] == "kick")
        self.assertEqual(result.events[last_kick + 1][2], gif.REG_RGBAQ)
        self.assertEqual(result.events[last_kick + 2][2], gif.REG_ST)

    def test_non_geometry_events_are_kept_in_order(self):
        events_b = [("P", 0x12000000, 5), ("op", 1, gif.REG_TEX0_1, 9)] + triangle_events(
            1, 3, [((0, 0, 0), (0, 0, 1)), ((8, 0, 0), (0, 0, 1)), ((0, 8, 0), (0, 0, 1))])
        frame_b = prims.PrimitiveBuilder().build(1, events_b)
        result = synth.synthesize(events_b, frame_b, frame_b, 0.5)
        self.assertEqual(result.events[0], ("P", 0x12000000, 5))
        self.assertEqual(result.events[1], ("op", 1, gif.REG_TEX0_1, 9))


class MetricsAndCaptureTests(unittest.TestCase):
    def test_identical_images_have_no_bad_pixels_and_infinite_psnr(self):
        image = (2, 1, bytes([0, 1, 2, 3, 4, 5]))
        result = metrics.compare(image, image)
        self.assertEqual(result["pct_bad"], 0.0)
        self.assertTrue(math.isinf(result["psnr_db"]))

    def test_one_bad_pixel_of_two(self):
        a = (2, 1, bytes([0, 0, 0, 0, 0, 0]))
        b = (2, 1, bytes([9, 0, 0, 0, 0, 0]))
        result = metrics.compare(a, b)
        self.assertEqual(result["pct_bad"], 50.0)

    def test_png_and_ppm_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            ppm = Path(directory) / "frame.ppm"
            ppm.write_bytes(b"P6\n2 1\n255\n" + bytes(range(6)))
            width, height, rgb = metrics.read_ppm(ppm)
            self.assertEqual((width, height, rgb), (2, 1, bytes(range(6))))
            png = Path(directory) / "frame.png"
            metrics.write_png(png, (width, height, rgb))
            self.assertEqual(png.read_bytes()[:8], b"\x89PNG\r\n\x1a\n")

    def test_capture_frames_split_at_v_records(self):
        records = [("G", 1, b""), ("V", 0), ("P", 0x12000000, 7), ("V", 1)]
        data = b"".join(capture_io.serialize_record(record) for record in records)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "two_frames.gscap"
            path.write_bytes(data)
            frames = list(capture_io.decode_frames(str(path)))
        self.assertEqual([frame.number for frame in frames], [0, 1])
        self.assertEqual(frames[1].events, [("P", 0x12000000, 7)])
        self.assertEqual(frames[1].start, frames[0].end)

    @unittest.skipUnless(MENU_CAPTURE.is_file(), "menu capture not found (set GT4_INTERP_MENU_CAPTURE)")
    def test_menu_capture_first_frames_round_trip(self):
        events = []
        for frame in capture_io.decode_frames(str(MENU_CAPTURE), wanted=None, last=20):
            events.extend(frame.events)
        self.assertGreater(len(events), 0)
        records = gif.encode_events(events)
        data = b"".join(capture_io.serialize_record(record) for record in records)
        self.assertEqual(decode_all(data), events)


if __name__ == "__main__":
    unittest.main()
