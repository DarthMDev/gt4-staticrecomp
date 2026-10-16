#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Frame comparison for the TASK-034 PoC: reference frames against candidate frames.

Stdlib only. Reads binary PPM (P6, maxval 255) directly and PNG through the ffmpeg command
(`ffmpeg -i file.png -f rawvideo -pix_fmt rgb24`), so the PCSX2 frame dumps can be compared
without a PNG decoder. Writes a JSON report and side-by-side PNG files.

Metrics (per frame pair, same size required):
- pct_bad: percent of pixels where any colour channel differs by more than --threshold (default 8)
- max_delta: largest channel difference
- psnr: 10 * log10(255^2 / MSE) over all channels; "inf" when identical

Frame alignment: --offsets tries reference frame n + k for candidate frame n and reports the best k
by mean pct_bad over the sampled frames.
"""

import argparse
import json
import math
import struct
import subprocess
import sys
import zlib
from pathlib import Path
from typing import List, Optional, Tuple

Image = Tuple[int, int, bytes]  # width, height, interleaved RGB


def read_ppm(path: Path) -> Image:
    data = path.read_bytes()
    tokens = []
    position = 0
    while len(tokens) < 4:
        while data[position:position + 1].isspace():
            position += 1
        if data[position:position + 1] == b"#":
            while data[position:position + 1] not in (b"\n", b""):
                position += 1
            continue
        start = position
        while not data[position:position + 1].isspace():
            position += 1
        tokens.append(data[start:position])
    position += 1  # single whitespace byte before the raster
    magic, width, height, maxval = tokens
    if magic != b"P6" or maxval != b"255":
        raise ValueError(f"{path}: only binary P6 with maxval 255 is supported")
    width, height = int(width), int(height)
    pixels = data[position:position + width * height * 3]
    if len(pixels) != width * height * 3:
        raise ValueError(f"{path}: raster is truncated")
    return width, height, pixels


def read_png_via_ffmpeg(path: Path, ffmpeg: str = "ffmpeg") -> Image:
    probe = subprocess.run(
        [ffmpeg, "-v", "error", "-i", str(path), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    size_line = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height",
         "-of", "csv=p=0", str(path)], stdout=subprocess.PIPE, check=True).stdout.decode().strip()
    width, height = (int(value) for value in size_line.split(","))
    if len(probe.stdout) != width * height * 3:
        raise ValueError(f"{path}: decoded size does not match {width}x{height}")
    return width, height, probe.stdout


def read_image(path: Path) -> Image:
    if path.suffix.lower() == ".ppm":
        return read_ppm(path)
    if path.suffix.lower() == ".png":
        return read_png_via_ffmpeg(path)
    raise ValueError(f"{path}: unsupported image type")


def compare(reference: Image, candidate: Image, threshold: int = 8) -> dict:
    ref_w, ref_h, ref = reference
    cand_w, cand_h, cand = candidate
    if (ref_w, ref_h) != (cand_w, cand_h):
        return {"size_mismatch": [[ref_w, ref_h], [cand_w, cand_h]]}
    bad = 0
    max_delta = 0
    squared = 0
    pixel_count = ref_w * ref_h
    for index in range(0, len(ref), 3):
        dr = abs(ref[index] - cand[index])
        dg = abs(ref[index + 1] - cand[index + 1])
        db = abs(ref[index + 2] - cand[index + 2])
        worst = max(dr, dg, db)
        if worst > threshold:
            bad += 1
        if worst > max_delta:
            max_delta = worst
        squared += dr * dr + dg * dg + db * db
    mse = squared / (pixel_count * 3)
    psnr = float("inf") if mse == 0 else 10.0 * math.log10(255.0 * 255.0 / mse)
    return {
        "width": ref_w,
        "height": ref_h,
        "pct_bad": 100.0 * bad / pixel_count,
        "max_delta": max_delta,
        "psnr": psnr if psnr != float("inf") else "inf",
        "threshold": threshold,
    }


def downscale_box(image: Image, factor: int) -> Image:
    width, height, pixels = image
    out_w, out_h = width // factor, height // factor
    out = bytearray(out_w * out_h * 3)
    area = factor * factor
    for y in range(out_h):
        for x in range(out_w):
            sums = [0, 0, 0]
            for dy in range(factor):
                row = ((y * factor + dy) * width + x * factor) * 3
                for dx in range(factor):
                    base = row + dx * 3
                    sums[0] += pixels[base]
                    sums[1] += pixels[base + 1]
                    sums[2] += pixels[base + 2]
            target = (y * out_w + x) * 3
            out[target] = sums[0] // area
            out[target + 1] = sums[1] // area
            out[target + 2] = sums[2] // area
    return out_w, out_h, bytes(out)


def side_by_side(images: List[Image], destination: Path) -> None:
    """Write the images left to right on one black canvas, top aligned, as an 8-bit RGB PNG."""
    height = max(image[1] for image in images)
    width = sum(image[0] for image in images) + 8 * (len(images) - 1)
    canvas = bytearray(width * height * 3)
    x_offset = 0
    for image_width, image_height, pixels in images:
        for y in range(image_height):
            source = y * image_width * 3
            target = (y * width + x_offset) * 3
            canvas[target:target + image_width * 3] = pixels[source:source + image_width * 3]
        x_offset += image_width + 8
    write_png(destination, width, height, bytes(canvas))


def write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)  # filter type: none
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                     + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))


def crop_rows(image: Image, top: int, height: int) -> Image:
    width, _, pixels = image
    row_bytes = width * 3
    return width, height, pixels[top * row_bytes:(top + height) * row_bytes]


def align_candidate(reference: Image, candidate: Image, threshold: int) -> Tuple[Image, int, dict]:
    """Crop the candidate vertically to the reference height, choosing the best top row (0..H-h)."""
    ref_w, ref_h, _ = reference
    cand_w, cand_h, _ = candidate
    if cand_w != ref_w or cand_h < ref_h:
        return candidate, 0, {"size_mismatch": [[ref_w, ref_h], [cand_w, cand_h]]}
    if cand_h == ref_h:
        return candidate, 0, compare(reference, candidate, threshold)
    best = None
    for top in range(0, cand_h - ref_h + 1):
        window = crop_rows(candidate, top, ref_h)
        result = compare(reference, window, threshold)
        if best is None or result["pct_bad"] < best[2]["pct_bad"]:
            best = (window, top, result)
    return best


def pattern_path(directory: Path, pattern: str, index: int) -> Path:
    return directory / pattern.format(index)


def best_offset(reference_dir: Path, candidate_dir: Path, frames: List[int], offsets: List[int],
                threshold: int, reference_pattern: str, candidate_pattern: str, downscale: int) -> Tuple[int, dict]:
    scores = {}
    for offset in offsets:
        totals = []
        for frame in frames:
            ref_path = pattern_path(reference_dir, reference_pattern, frame + offset)
            cand_path = pattern_path(candidate_dir, candidate_pattern, frame)
            if not ref_path.exists() or not cand_path.exists():
                continue
            candidate = read_image(cand_path)
            if downscale > 1:
                candidate = downscale_box(candidate, downscale)
            _, _, result = align_candidate(read_image(ref_path), candidate, threshold)
            if "pct_bad" in result:
                totals.append(result["pct_bad"])
        if totals:
            scores[offset] = sum(totals) / len(totals)
    if not scores:
        return 0, {}
    winner = min(scores, key=lambda key: (scores[key], abs(key)))
    return winner, scores


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("reference_dir", type=Path)
    parser.add_argument("candidate_dir", type=Path)
    parser.add_argument("--frames", default="60,300,700,900,1100")
    parser.add_argument("--threshold", type=int, default=8)
    parser.add_argument("--offsets", default="0", help="comma list of reference offsets to test, e.g. -2,-1,0,1,2")
    parser.add_argument("--reference-pattern", default="frame_{:05d}.ppm")
    parser.add_argument("--candidate-pattern", default="frame_{:05d}.ppm")
    parser.add_argument("--downscale", type=int, default=1, help="box-downscale candidates by this factor first")
    parser.add_argument("--crop-top", type=int, default=None,
                        help="use this top row of a taller candidate instead of scanning (after a scan)")
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--sheet-dir", type=Path, help="write side-by-side PNGs here")
    parser.add_argument("--sheet-extra", type=Path, help="optional extra directory shown last, downscaled to reference size")
    parser.add_argument("--sheet-extra-pattern", default="frame_{:05d}.png")
    parser.add_argument("--extra-factor", type=int, default=3)
    args = parser.parse_args(argv)

    frames = [int(value) for value in args.frames.split(",") if value]
    offsets = [int(value) for value in args.offsets.split(",")]
    if len(offsets) == 1:
        winner, scores = offsets[0], {}
    else:
        winner, scores = best_offset(args.reference_dir, args.candidate_dir, frames, offsets, args.threshold,
                                     args.reference_pattern, args.candidate_pattern, args.downscale)
    report = {"reference": str(args.reference_dir), "candidate": str(args.candidate_dir),
              "threshold": args.threshold, "downscale": args.downscale,
              "offset_scan_mean_pct_bad": {str(k): v for k, v in scores.items()},
              "chosen_reference_offset": winner, "frames": []}
    for frame in frames:
        ref_path = pattern_path(args.reference_dir, args.reference_pattern, frame + winner)
        cand_path = pattern_path(args.candidate_dir, args.candidate_pattern, frame)
        entry = {"frame": frame, "reference": ref_path.name, "candidate": cand_path.name}
        if not ref_path.exists() or not cand_path.exists():
            entry["missing"] = True
            report["frames"].append(entry)
            continue
        reference = read_image(ref_path)
        candidate_full = read_image(cand_path)
        candidate = downscale_box(candidate_full, args.downscale) if args.downscale > 1 else candidate_full
        if args.crop_top is not None and candidate[1] > reference[1]:
            aligned = crop_rows(candidate, args.crop_top, reference[1])
            top = args.crop_top
            result = compare(reference, aligned, args.threshold)
        else:
            aligned, top, result = align_candidate(reference, candidate, args.threshold)
        entry.update(result)
        entry["candidate_crop_top"] = top
        report["frames"].append(entry)
        if args.sheet_dir:
            args.sheet_dir.mkdir(parents=True, exist_ok=True)
            images = [reference, aligned]
            if args.sheet_extra:
                extra_path = args.sheet_extra / args.sheet_extra_pattern.format(frame)
                if extra_path.exists():
                    extra = read_image(extra_path)
                    images.append(downscale_box(extra, args.extra_factor))
            side_by_side(images, args.sheet_dir / f"frame_{frame:05d}_side_by_side.png")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"chosen_reference_offset": winner, "offset_scan": report["offset_scan_mean_pct_bad"]}))
    for entry in report["frames"]:
        print(entry.get("frame"), entry.get("pct_bad"), entry.get("psnr"), entry.get("max_delta"),
              entry.get("candidate_crop_top"), entry.get("missing", ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
