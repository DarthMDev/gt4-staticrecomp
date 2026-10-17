#!/usr/bin/env python3
"""Interlace quality metrics for consecutive frame dumps (TASK-047).

Reads 8-bit RGB/RGBA PNG frames (as written by gt4-gs -dumpdir or [GT4GS] DumpDir) with the standard library only
and reports, per frame and over a frame range:

  adjacent   mean |Y[y] - Y[y+1]|  (luma 0..255, all columns, rows 0..H-2): line-to-line difference energy
  alternate  mean |Y[y] - Y[y+2]|  (rows 0..H-3): difference between lines of the same field
  comb       adjacent / alternate. Natural images and correctly woven fields give values below 1; fields woven in
             the wrong order (combing: every other line belongs to another moment or another half-line position)
             push it above 1; line-doubled output (one field stretched, no deinterlacing) pulls it down.
  flicker    mean |Y_t - Y_(t-1)| between this frame and the previous one in the range (static scenes: the
             vertical bounce of undeinterlaced fields shows up here).

Luma: Y = (299 R + 587 G + 114 B) / 1000 (BT.601 weights), integer pixels.

Usage:
  gs_frame_metrics.py <dir> --pattern NAME_frame{:05d}.png --first N --last M [--json out.json]
"""

import argparse
import json
import struct
import sys
import zlib
from pathlib import Path

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class PngError(ValueError):
    """Raised for PNG files this reader does not support."""


def read_png_luma(path: Path):
    """Return (width, height, rows) where rows is a list of lists of integer luma values."""
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise PngError(f"{path}: not a PNG file")
    position = len(PNG_SIGNATURE)
    width = height = None
    channels = 0
    compressed = bytearray()
    while position < len(data):
        (length,) = struct.unpack_from(">I", data, position)
        kind = data[position + 4:position + 8]
        body = data[position + 8:position + 8 + length]
        position += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, _compression, _filter, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or interlace != 0 or color not in (2, 6):
                raise PngError(f"{path}: only 8-bit non-interlaced RGB or RGBA is supported")
            channels = 3 if color == 2 else 4
        elif kind == b"IDAT":
            compressed += body
        elif kind == b"IEND":
            break
    if width is None:
        raise PngError(f"{path}: no IHDR")
    raw = zlib.decompress(bytes(compressed))
    stride = width * channels
    previous = bytearray(stride)
    rows = []
    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        line = bytearray(raw[offset + 1:offset + 1 + stride])
        offset += 1 + stride
        if filter_type == 1:  # Sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filter_type == 2:  # Up
            line = bytearray((a + b) & 0xFF for a, b in zip(line, previous))
        elif filter_type == 3:  # Average
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif filter_type == 4:  # Paeth
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = previous[i]
                c = previous[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                predictor = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + predictor) & 0xFF
        elif filter_type != 0:
            raise PngError(f"{path}: bad filter type {filter_type}")
        previous = line
        r, g, b = line[0::channels], line[1::channels], line[2::channels]
        rows.append([(299 * rr + 587 * gg + 114 * bb) // 1000 for rr, gg, bb in zip(r, g, b)])
    return width, height, rows


def mean_abs_row_difference(rows, step):
    total = 0
    count = 0
    for upper, lower in zip(rows, rows[step:]):
        total += sum(abs(a - b) for a, b in zip(upper, lower))
        count += len(upper)
    return total / count if count else 0.0


def mean_abs_frame_difference(rows_a, rows_b):
    total = 0
    count = 0
    for line_a, line_b in zip(rows_a, rows_b):
        total += sum(abs(a - b) for a, b in zip(line_a, line_b))
        count += len(line_a)
    return total / count if count else 0.0


def frame_metrics(rows, previous_rows=None):
    adjacent = mean_abs_row_difference(rows, 1)
    alternate = mean_abs_row_difference(rows, 2)
    result = {
        "adjacent": adjacent,
        "alternate": alternate,
        "comb": adjacent / alternate if alternate else 0.0,
    }
    if previous_rows is not None:
        result["flicker"] = mean_abs_frame_difference(rows, previous_rows)
    return result


def range_metrics(directory: Path, pattern: str, first: int, last: int):
    frames = []
    previous = None
    for number in range(first, last + 1):
        path = directory / pattern.format(number)
        _width, _height, rows = read_png_luma(path)
        entry = {"frame": number}
        entry.update(frame_metrics(rows, previous))
        frames.append(entry)
        previous = rows
    summary = {key: sum(f[key] for f in frames if key in f) / max(1, sum(1 for f in frames if key in f))
               for key in ("adjacent", "alternate", "comb", "flicker")}
    summary["frames"] = len(frames)
    summary["flicker_max"] = max((f["flicker"] for f in frames if "flicker" in f), default=0.0)
    return {"summary": summary, "frames": frames}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--pattern", required=True, help="file name with a {} field for the frame number")
    parser.add_argument("--first", type=int, required=True)
    parser.add_argument("--last", type=int, required=True)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args(argv)
    if args.last < args.first:
        parser.error("--last is before --first")
    result = range_metrics(args.directory, args.pattern, args.first, args.last)
    s = result["summary"]
    print(f"frames {s['frames']}: adjacent {s['adjacent']:.3f} alternate {s['alternate']:.3f} comb {s['comb']:.3f} "
          f"flicker mean {s['flicker']:.3f} max {s['flicker_max']:.3f}")
    if args.json:
        args.json.write_text(json.dumps(result, indent=1) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
