"""Compare headless frame dumps (binary P6 PPM) written at two internal resolutions.

The candidate may be a native-size dump (SCALE times larger in each axis, written with
GT4_DUMP_NATIVE_SIZE=1); it is box-averaged down by SCALE before the comparison, so every
PSNR is measured at the reference resolution. A candidate of the reference size is compared
directly. PSNR is 8-bit RGB over all channels; identical frames report infinity.

usage: compare_frame_dumps.py <reference-dir> <candidate-dir> [--scale N] [--json out.json]
       [--png-out dir --png-frame NAME ...]
"""
import argparse
import json
import math
import struct
import sys
import zlib
from pathlib import Path


def read_ppm(path):
    data = Path(path).read_bytes()
    fields = []
    position = 0
    while len(fields) < 4:
        while data[position:position + 1].isspace():
            position += 1
        if data[position:position + 1] == b'#':
            position = data.index(b'\n', position) + 1
            continue
        start = position
        while not data[position:position + 1].isspace():
            position += 1
        fields.append(data[start:position])
    magic, width, height, maximum = fields
    if magic != b'P6' or maximum != b'255':
        raise ValueError(f'{path}: expected binary P6 with maxval 255')
    width, height = int(width), int(height)
    pixels = data[position + 1:]
    if len(pixels) != width * height * 3:
        raise ValueError(f'{path}: pixel data is {len(pixels)} bytes for {width}x{height}')
    return width, height, pixels


def box_downsample(width, height, pixels, scale):
    """Average each scale x scale block (rounded), channel by channel."""
    if scale == 1:
        return pixels
    if width % scale or height % scale:
        raise ValueError('candidate size is not a multiple of the scale')
    out_width, out_height = width // scale, height // scale
    count = scale * scale
    result = bytearray(out_width * out_height * 3)
    row = width * 3
    for y in range(out_height):
        for x in range(out_width):
            sums = [0, 0, 0]
            for j in range(scale):
                base = (y * scale + j) * row + (x * scale) * 3
                for i in range(scale):
                    offset = base + i * 3
                    sums[0] += pixels[offset]
                    sums[1] += pixels[offset + 1]
                    sums[2] += pixels[offset + 2]
            target = (y * out_width + x) * 3
            for channel in range(3):
                result[target + channel] = (sums[channel] + count // 2) // count
    return bytes(result)


def psnr(reference, candidate):
    if len(reference) != len(candidate):
        raise ValueError('frames differ in size')
    squared = 0
    for a, b in zip(reference, candidate):
        squared += (a - b) * (a - b)
    if squared == 0:
        return math.inf
    mse = squared / len(reference)
    return 10.0 * math.log10(255.0 * 255.0 / mse)


def compare_frame(reference_path, candidate_path, scale):
    ref_width, ref_height, ref_pixels = read_ppm(reference_path)
    cand_width, cand_height, cand_pixels = read_ppm(candidate_path)
    if (cand_width, cand_height) != (ref_width * scale, ref_height * scale) and (cand_width, cand_height) != (ref_width, ref_height):
        raise ValueError(f'{candidate_path.name}: {cand_width}x{cand_height} matches neither {ref_width}x{ref_height} nor its {scale}x size')
    if (cand_width, cand_height) != (ref_width, ref_height):
        cand_pixels = box_downsample(cand_width, cand_height, cand_pixels, scale)
    return psnr(ref_pixels, cand_pixels), (cand_width, cand_height) != (ref_width, ref_height)


def compare_directories(reference_dir, candidate_dir, scale):
    reference_dir, candidate_dir = Path(reference_dir), Path(candidate_dir)
    names = sorted(path.name for path in reference_dir.glob('*.ppm'))
    if not names:
        raise ValueError(f'no reference frames in {reference_dir}')
    results = {}
    for name in names:
        candidate = candidate_dir / name
        if not candidate.exists():
            raise ValueError(f'missing candidate frame {name}')
        value, downsampled = compare_frame(reference_dir / name, candidate, scale)
        results[name] = {'psnr_db': value, 'downsampled_from_native': downsampled}
    finite = [item['psnr_db'] for item in results.values() if math.isfinite(item['psnr_db'])]
    summary = {
        'frames': len(results),
        'identical': sum(1 for item in results.values() if item['psnr_db'] == math.inf),
        'min_psnr_db': min(finite) if finite else math.inf,
        'mean_psnr_db': sum(finite) / len(finite) if finite else math.inf,
        'scale': scale,
    }
    return summary, results


def jsonable(value):
    """JSON cannot carry infinity: identical frames are written as the string 'inf'."""
    if isinstance(value, dict):
        return {key: jsonable(item) for key, item in value.items()}
    if isinstance(value, float) and math.isinf(value):
        return 'inf'
    return value


def write_png(path, width, height, pixels):
    """Write an 8-bit RGB PNG with zlib; no imaging library is needed."""
    rows = b''.join(b'\x00' + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)

    header = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('reference_dir', type=Path)
    parser.add_argument('candidate_dir', type=Path)
    parser.add_argument('--scale', type=int, required=True, help='internal scale of the candidate (2 or 3)')
    parser.add_argument('--json', type=Path, help='write the per-frame report here')
    parser.add_argument('--png-out', type=Path, help='directory for PNG copies of the frames named by --png-frame')
    parser.add_argument('--png-frame', action='append', default=[], help='frame file name, e.g. frame_00400.ppm')
    options = parser.parse_args()
    try:
        if options.scale < 1:
            raise ValueError('--scale takes a positive integer')
        summary, results = compare_directories(options.reference_dir, options.candidate_dir, options.scale)
        if options.png_out:
            options.png_out.mkdir(parents=True, exist_ok=True)
            for name in options.png_frame:
                width, height, pixels = read_ppm(options.candidate_dir / name)
                write_png(options.png_out / (Path(name).stem + '.png'), width, height, pixels)
        if options.json:
            options.json.write_text(json.dumps(jsonable({'summary': summary, 'frames': results}), indent=2) + '\n', encoding='utf-8')
    except (OSError, ValueError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1
    print(json.dumps(jsonable(summary), indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
