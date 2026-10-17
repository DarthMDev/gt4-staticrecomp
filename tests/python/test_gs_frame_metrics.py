"""TASK-047: interlace metrics of frame dumps (tools/gs_frame_metrics). Synthetic PNGs only.

Three pictures of the same scene (a soft vertical gradient with a sharp diagonal edge) are written as PNGs:
correctly woven fields, fields woven in the wrong order (every other line from a picture shifted by one line) and one
field line-doubled (no deinterlacing). The comb index must order them line-doubled < woven < wrong order, and the
frame-to-frame flicker of a line-doubled picture that bounces by one line must exceed a steady one.
"""
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "gs_frame_metrics"))
import gs_frame_metrics as metrics  # noqa: E402

WIDTH, HEIGHT = 64, 48


def scene(x, y):
    """Luma of the progressive scene at full resolution (y may be fractional for field sampling)."""
    edge = 230 if x > 1.3 * y else 30
    return int(edge * 0.7 + 0.3 * (y * 255 / HEIGHT))


def png_bytes(rows, channels=3):
    """Encode grey rows as an 8-bit RGB (or RGBA) PNG, cycling the row filters None, Sub, Up, Average, Paeth."""
    stride = WIDTH * channels
    raw = bytearray()
    previous = bytearray(stride)
    for index, row in enumerate(rows):
        line = bytearray()
        for value in row:
            line += bytes([value] * 3) + (b"\xff" if channels == 4 else b"")
        kind = index % 5
        encoded = bytearray(stride)
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = previous[i]
            c = previous[i - channels] if i >= channels else 0
            if kind == 0:
                predictor = 0
            elif kind == 1:
                predictor = a
            elif kind == 2:
                predictor = b
            elif kind == 3:
                predictor = (a + b) >> 1
            else:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                predictor = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
            encoded[i] = (line[i] - predictor) & 0xFF
        raw += bytes([kind]) + encoded
        previous = line

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    color = 2 if channels == 3 else 6
    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, color, 0, 0, 0)
    return (metrics.PNG_SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw)))
            + chunk(b"IEND", b""))


def woven():
    return [[scene(x, y) for x in range(WIDTH)] for y in range(HEIGHT)]


def wrong_order():
    # Odd lines come from the other field at another position (the edge moved by 8 pixels): the comb pattern of
    # fields woven in the wrong order.
    return [[scene(x if y % 2 == 0 else x + 8, y) for x in range(WIDTH)] for y in range(HEIGHT)]


def line_doubled(phase):
    # One field (every other line) stretched to full height, shifted by `phase` lines.
    return [[scene(x, min(HEIGHT - 1, (y // 2) * 2 + phase)) for x in range(WIDTH)] for y in range(HEIGHT)]


class FrameMetricsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def write(self, name, rows, channels=3):
        path = self.dir / name
        path.write_bytes(png_bytes(rows, channels))
        return path

    def test_decoder_reads_all_filters_and_rgba(self):
        rows = woven()
        for channels in (3, 4):
            path = self.write(f"w{channels}.png", rows, channels)
            width, height, decoded = metrics.read_png_luma(path)
            self.assertEqual((width, height), (WIDTH, HEIGHT))
            self.assertEqual(decoded, rows)  # grey pixels: luma equals the grey value

    def test_comb_orders_the_three_cases(self):
        _, _, good = metrics.read_png_luma(self.write("good.png", woven()))
        _, _, bad = metrics.read_png_luma(self.write("bad.png", wrong_order()))
        _, _, doubled = metrics.read_png_luma(self.write("doubled.png", line_doubled(0)))
        comb_good = metrics.frame_metrics(good)["comb"]
        comb_bad = metrics.frame_metrics(bad)["comb"]
        comb_doubled = metrics.frame_metrics(doubled)["comb"]
        self.assertLess(comb_doubled, comb_good)
        self.assertLess(comb_good, comb_bad)
        self.assertGreater(comb_bad, 1.0)

    def test_flicker_of_bouncing_fields(self):
        for number in range(4):
            self.write(f"bounce_frame{number:05d}.png", line_doubled(number % 2))
            self.write(f"steady_frame{number:05d}.png", woven())
        bounce = metrics.range_metrics(self.dir, "bounce_frame{:05d}.png", 0, 3)["summary"]
        steady = metrics.range_metrics(self.dir, "steady_frame{:05d}.png", 0, 3)["summary"]
        self.assertEqual(steady["flicker"], 0.0)
        self.assertGreater(bounce["flicker"], 1.0)
        self.assertEqual(bounce["frames"], 4)

    def test_rejects_unsupported_png(self):
        path = self.dir / "x.png"
        path.write_bytes(b"not a png")
        with self.assertRaises(metrics.PngError):
            metrics.read_png_luma(path)

    def test_command_line_writes_json(self):
        for number in range(2):
            self.write(f"c_frame{number:05d}.png", woven())
        out = self.dir / "m.json"
        self.assertEqual(metrics.main([str(self.dir), "--pattern", "c_frame{:05d}.png", "--first", "0", "--last", "1",
                                       "--json", str(out)]), 0)
        self.assertIn("summary", out.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
