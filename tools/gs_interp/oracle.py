"""The offline oracle: real frame N against the primitive-interpolated and extrapolated N.

For a target frame N the oracle builds
  interpolated N:  frame N+1's events with its geometry lerped from frame N-1 at t = 0.5
  extrapolated N:  frame N-1's events with its geometry extrapolated from frame N-2 at t = 2.0
(predicted = 2*(N-1) - (N-2)); the later frame supplies every non-geometry event.
Each synthetic frame is written as a capture: the real records before frame N, then the
synthetic records, then the 'V' record of frame N. The offline replay renders the capture
(native/hw_gs/cpuref hw_cpuref_replay, the TASK-027 reference renderer), and the PPM of frame
N is compared with the real frame N rendered from the untouched capture.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path

from .capture import decode_frames, encode_frame_records
from .metrics import compare, diff_image, read_ppm, write_png
from .match import literal_key, match_primitives
from .prims import PrimitiveBuilder
from .synth import synthesize


@dataclass
class KeptFrame:
    events: list
    prims: list
    start: int
    end: int


def load_frames(capture: str, numbers: set, last: int) -> dict:
    """Decode frames 0..last (the register state needs every frame) and keep the wanted ones."""
    builder = PrimitiveBuilder()
    kept: dict = {}
    for frame in decode_frames(capture, wanted=None, last=last):
        prims = builder.build(frame.number, frame.events)
        if frame.number in numbers:
            kept[frame.number] = KeptFrame(frame.events, prims, frame.start, frame.end)
    missing = numbers - set(kept)
    if missing:
        raise ValueError(f"frames {sorted(missing)} are not in the capture")
    return kept


def copy_prefix(source: str, destination: Path, length: int) -> None:
    remaining = length
    with open(source, "rb") as src, open(destination, "wb") as out:
        while remaining > 0:
            chunk = src.read(min(remaining, 16 << 20))
            if not chunk:
                raise ValueError(f"{source} is shorter than {length} bytes")
            out.write(chunk)
            remaining -= len(chunk)


def replay(exe: str, capture: Path, output: Path) -> str:
    output.mkdir(parents=True, exist_ok=True)
    started = time.time()
    result = subprocess.run([exe, str(capture), str(output)], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"replay of {capture} failed: {result.stderr.strip()}")
    return f"{result.stdout.strip()} ({round(time.time() - started, 1)} s)"


def frame_path(directory: Path, number: int) -> Path:
    return directory / f"frame_{number:05d}.ppm"


def build_real_replay(exe: str, capture: str, directory: Path) -> str:
    """Replay the untouched capture once; every real frame PPM is kept in the directory."""
    if directory.is_dir() and any(directory.glob("frame_*.ppm")):
        return "reused"
    return replay(exe, Path(capture), directory)


def run_oracle(capture: str, real_dir: Path, work: Path, exe: str, frame: int,
               interp_t: float = 0.5, extrap_t: float = 2.0, remap_pages: bool = True) -> dict:
    kept = load_frames(capture, {frame - 2, frame - 1, frame, frame + 1}, last=frame + 1)
    # Page-selecting writes follow the parity of the target: frame N-2 has N's parity.
    interp = synthesize(kept[frame + 1].events, kept[frame - 1].prims, kept[frame + 1].prims, interp_t,
                        events_page=kept[frame - 2].events if remap_pages else None)
    extrap = synthesize(kept[frame - 1].events, kept[frame - 2].prims, kept[frame - 1].prims, extrap_t,
                        events_page=kept[frame - 2].events if remap_pages else None)

    work.mkdir(parents=True, exist_ok=True)
    interp_capture = work / f"interp_{frame:05d}.gscap"
    extrap_capture = work / f"extrap_{frame:05d}.gscap"
    copy_prefix(capture, interp_capture, kept[frame].start)
    with open(interp_capture, "ab") as out:
        out.write(encode_frame_records(interp.events, frame))
    copy_prefix(capture, extrap_capture, kept[frame - 1].start)
    with open(extrap_capture, "ab") as out:
        out.write(encode_frame_records(extrap.events, frame))

    interp_dir = work / f"interp_{frame:05d}"
    extrap_dir = work / f"extrap_{frame:05d}"
    interp_log = replay(exe, interp_capture, interp_dir)
    extrap_log = replay(exe, extrap_capture, extrap_dir)

    real = {n: read_ppm(frame_path(real_dir, n)) for n in (frame - 1, frame, frame + 1)}
    synth_interp = read_ppm(frame_path(interp_dir, frame))
    synth_extrap = read_ppm(frame_path(extrap_dir, frame))
    report = {
        "frame": frame,
        "interp_t": interp_t,
        "extrap_t": extrap_t,
        "interp_match": match_summary(interp.match),
        "extrap_match": match_summary(extrap.match),
        "interp_match_literal_key": match_summary(match_primitives(
            kept[frame - 1].prims, kept[frame + 1].prims, literal_key)),
        "extrap_match_literal_key": match_summary(match_primitives(
            kept[frame - 2].prims, kept[frame - 1].prims, literal_key)),
        "interp_substituted": interp.substituted,
        "extrap_substituted": extrap.substituted,
        "interp_rejected": interp.rejected,
        "interp_page_writes_remapped": interp.page_remapped,
        "extrap_page_writes_remapped": extrap.page_remapped,
        "remap_pages": remap_pages,
        "extrap_rejected": extrap.rejected,
        "interp_vs_real": compare(synth_interp, real[frame]),
        "extrap_vs_real": compare(synth_extrap, real[frame]),
        "hold_previous_vs_real": compare(real[frame - 1], real[frame]),
        "hold_next_vs_real": compare(real[frame + 1], real[frame]),
        "replay": {"interp": interp_log, "extrap": extrap_log},
    }
    report["_images"] = {
        "real": real[frame],
        "interp": synth_interp,
        "extrap": synth_extrap,
    }
    return report


def match_summary(match) -> dict:
    return {
        "primitives_a": match.count_a,
        "primitives_b": match.count_b,
        "matched": match.matched,
        "substitutable": match.substitutable,
        "shared_matched": match.shared_matched,
        "match_rate_of_b": round(match.rate_b, 4),
        "match_rate_of_a": round(match.rate_a, 4),
        "substitutable_rate_of_b": round(match.substitutable_rate_b, 4),
        "unmatched_b_by_kind": match.unmatched_b_by_kind,
    }


def save_samples(report: dict, directory: Path, tag: str) -> list:
    """PNG triplets (real, interpolated, extrapolated) and the two diffs for one report."""
    from .metrics import diff_image as _diff

    directory.mkdir(parents=True, exist_ok=True)
    frame = report["frame"]
    images = report.pop("_images")
    written = []
    outputs = {
        "real": images["real"],
        "interp": images["interp"],
        "extrap": images["extrap"],
        "diff_interp": _diff(images["real"], images["interp"]),
        "diff_extrap": _diff(images["real"], images["extrap"]),
    }
    for kind, image in outputs.items():
        path = directory / f"{tag}_f{frame:05d}_{kind}.png"
        write_png(path, image)
        written.append(str(path))
    report["samples"] = written
    return written


def dump_report(report: dict, path: Path) -> None:
    serial = {key: value for key, value in report.items() if not key.startswith("_")}
    path.write_text(json.dumps(serial, indent=2, default=str), encoding="utf-8")


def clean_work(work: Path) -> None:
    if work.is_dir():
        shutil.rmtree(work)
