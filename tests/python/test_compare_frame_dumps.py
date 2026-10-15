"""Synthetic controls for the frame-dump comparison used by the internal-resolution checks."""

import math
from pathlib import Path
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import compare_frame_dumps as compare


def write_ppm(path, width, height, pixels):
    path.write_bytes(b'P6\n%d %d\n255\n' % (width, height) + bytes(pixels))


class CompareFrameDumpTests(unittest.TestCase):
    def test_identical_frames_report_infinite_psnr(self):
        self.assertEqual(compare.psnr(b'\x10\x20\x30', b'\x10\x20\x30'), math.inf)

    def test_psnr_matches_mean_squared_error(self):
        # One channel off by 1 over 3 bytes: MSE = 1/3, PSNR = 10 log10(255^2 * 3).
        expected = 10.0 * math.log10(255.0 * 255.0 * 3.0)
        self.assertAlmostEqual(compare.psnr(b'\x00\x00\x00', b'\x01\x00\x00'), expected, places=9)

    def test_box_downsample_averages_each_block_with_rounding(self):
        # 2x2 block of red values 0, 1, 1, 2 (sum 4): average 1.0 exactly; second channel 255 everywhere.
        pixels = bytes([0, 9, 255, 1, 9, 255, 1, 9, 255, 2, 9, 255])
        self.assertEqual(compare.box_downsample(2, 2, pixels, 2), bytes([1, 9, 255]))
        # Sum 3 over 4 samples rounds to 1 (0.75), not 0.
        pixels = bytes([0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0])
        self.assertEqual(compare.box_downsample(2, 2, pixels, 2), bytes([1, 0, 0]))

    def test_native_candidate_compares_at_reference_resolution(self):
        reference = bytes(range(12))
        native = bytearray()
        # A 4x4 native image whose 2x2 blocks average back to the 2x2 reference.
        for y in range(4):
            for x in range(4):
                native += bytes([x * 10, y * 10, 7])
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            write_ppm(root / 'ref.ppm', 2, 2, [0] * 12)
            write_ppm(root / 'native.ppm', 4, 4, native)
            value, downsampled = compare.compare_frame(root / 'ref.ppm', root / 'native.ppm', 2)
            self.assertTrue(downsampled)
            expected = compare.box_downsample(4, 4, bytes(native), 2)
            self.assertEqual(value, compare.psnr(bytes([0] * 12), expected))
            write_ppm(root / 'same.ppm', 2, 2, reference)
            value, downsampled = compare.compare_frame(root / 'same.ppm', root / 'same.ppm', 2)
            self.assertEqual((value, downsampled), (math.inf, False))

    def test_size_mismatch_and_bad_magic_are_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            write_ppm(root / 'ref.ppm', 2, 2, [0] * 12)
            write_ppm(root / 'odd.ppm', 3, 3, [0] * 27)
            with self.assertRaises(ValueError):
                compare.compare_frame(root / 'ref.ppm', root / 'odd.ppm', 2)
            (root / 'bad.ppm').write_bytes(b'P5\n2 2\n255\n' + bytes(4))
            with self.assertRaises(ValueError):
                compare.read_ppm(root / 'bad.ppm')

    def test_png_writer_round_trips_pixels(self):
        pixels = bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 9, 9, 9])
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'out.png'
            compare.write_png(path, 2, 2, pixels)
            data = path.read_bytes()
            self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
            index = data.index(b'IDAT')
            length = int.from_bytes(data[index - 4:index], 'big')
            raw = zlib.decompress(data[index + 4:index + 4 + length])
            self.assertEqual(raw, b'\x00' + pixels[0:6] + b'\x00' + pixels[6:12])


if __name__ == '__main__':
    unittest.main()
