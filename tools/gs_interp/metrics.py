"""Image comparison for replayed frames: PPM reading, pct_bad, PSNR, PNG writing (stdlib only).

pct_bad is the percentage of pixels where some channel differs by more than 8 (the 14.4
threshold of docs/hw-renderer-spec). PSNR is over all 8-bit channels; identical images
report infinity.
"""

from __future__ import annotations

import math
import struct
import zlib
from pathlib import Path


def read_ppm(path: Path) -> tuple:
    """Binary P6 PPM: returns (width, height, rgb bytes)."""
    data = Path(path).read_bytes()
    fields = []
    position = 0
    while len(fields) < 4:
        while data[position:position + 1].isspace():
            position += 1
        start = position
        while not data[position:position + 1].isspace():
            position += 1
        fields.append(data[start:position])
    position += 1  # single whitespace byte before the raster
    magic, width, height, maxval = fields
    if magic != b"P6" or maxval != b"255":
        raise ValueError(f"{path}: only binary 8-bit P6 is supported")
    width, height = int(width), int(height)
    rgb = data[position:position + width * height * 3]
    if len(rgb) != width * height * 3:
        raise ValueError(f"{path}: raster is {len(rgb)} bytes, expected {width * height * 3}")
    return width, height, rgb


def compare(image_a: tuple, image_b: tuple) -> dict:
    """pct_bad, psnr_db and mse of two (width, height, rgb) images of the same size."""
    width_a, height_a, rgb_a = image_a
    width_b, height_b, rgb_b = image_b
    if (width_a, height_a) != (width_b, height_b):
        raise ValueError("images differ in size")
    bad = 0
    squared = 0
    pixels = width_a * height_a
    for offset in range(0, len(rgb_a), 3):
        r = rgb_a[offset] - rgb_b[offset]
        g = rgb_a[offset + 1] - rgb_b[offset + 1]
        b = rgb_a[offset + 2] - rgb_b[offset + 2]
        squared += r * r + g * g + b * b
        if abs(r) > 8 or abs(g) > 8 or abs(b) > 8:
            bad += 1
    mse = squared / (pixels * 3)
    psnr = math.inf if mse == 0 else 10.0 * math.log10(255.0 * 255.0 / mse)
    return {"pct_bad": 100.0 * bad / pixels, "psnr_db": psnr, "mse": mse}


def diff_image(image_a: tuple, image_b: tuple, gain: int = 4) -> tuple:
    """Grey image of the largest channel difference, multiplied by gain and clipped."""
    width, height, rgb_a = image_a
    rgb_b = image_b[2]
    out = bytearray(len(rgb_a))
    for offset in range(0, len(rgb_a), 3):
        largest = max(abs(rgb_a[offset + c] - rgb_b[offset + c]) for c in range(3))
        value = min(255, largest * gain)
        out[offset:offset + 3] = bytes((value, value, value))
    return width, height, bytes(out)


def write_png(path: Path, image: tuple) -> None:
    width, height, rgb = image
    stride = width * 3
    raw = b"".join(b"\x00" + rgb[row * stride:(row + 1) * stride] for row in range(height))

    def chunk(kind: bytes, body: bytes) -> bytes:
        crc = zlib.crc32(kind + body) & 0xFFFFFFFF
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", crc)

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
