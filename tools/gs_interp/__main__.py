"""Command line: python -m gs_interp (run from the tools directory).

  python -m gs_interp oracle --capture CAP --exe REPLAY --real-dir DIR --work DIR
         --frames 300 301 --samples DIR --tag menu --json OUT.json
  python -m gs_interp real --capture CAP --exe REPLAY --real-dir DIR
  python -m gs_interp info --capture CAP --frames 300

'real' replays the untouched capture once into --real-dir (PPM per frame). 'oracle' builds the
interpolated and extrapolated frame for each --frames value and compares them with the real
frames in --real-dir. 'info' prints the primitive and event counts of frames.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from .capture import decode_frames
from .oracle import build_real_replay, clean_work, dump_report, load_frames, run_oracle, save_samples
from .prims import PRIM_NAMES
from .match import match_primitives


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="gs_interp")
    commands = parser.add_subparsers(dest="command", required=True)

    real = commands.add_parser("real", help="replay the untouched capture into PPM frames")
    real.add_argument("--capture", required=True)
    real.add_argument("--exe", required=True)
    real.add_argument("--real-dir", required=True, type=Path)

    oracle = commands.add_parser("oracle", help="interpolation and extrapolation oracle")
    oracle.add_argument("--capture", required=True)
    oracle.add_argument("--exe", required=True)
    oracle.add_argument("--real-dir", required=True, type=Path)
    oracle.add_argument("--work", required=True, type=Path)
    oracle.add_argument("--frames", required=True, type=int, nargs="+")
    oracle.add_argument("--samples", type=Path)
    oracle.add_argument("--tag", default="capture")
    oracle.add_argument("--json", type=Path)
    oracle.add_argument("--keep-work", action="store_true")

    info = commands.add_parser("info", help="event and primitive counts of frames")
    info.add_argument("--capture", required=True)
    info.add_argument("--frames", type=int, nargs="+", required=True)

    args = parser.parse_args(argv)
    if args.command == "real":
        from .oracle import replay

        print(replay(args.exe, Path(args.capture), args.real_dir))
        return 0
    if args.command == "info":
        return info_command(args)
    return oracle_command(args)


def info_command(args) -> int:
    from .prims import PrimitiveBuilder

    wanted = set(args.frames)
    builder = PrimitiveBuilder()
    last = max(wanted)
    for frame in decode_frames(args.capture, wanted=None, last=last):
        prims = builder.build(frame.number, frame.events)
        if frame.number in wanted:
            kinds: dict = {}
            for primitive in prims:
                label = PRIM_NAMES[primitive.kind] + (" (shared)" if primitive.shared else "")
                kinds[label] = kinds.get(label, 0) + 1
            events = {}
            for event in frame.events:
                events[event[0]] = events.get(event[0], 0) + 1
            print(json.dumps({"frame": frame.number, "events": events, "primitives": kinds}))
    return 0


def oracle_command(args) -> int:
    results = []
    for frame in args.frames:
        build_real_replay(args.exe, args.capture, args.real_dir)
        report = run_oracle(args.capture, args.real_dir, args.work, args.exe, frame)
        if args.samples:
            save_samples(report, args.samples, args.tag)
        report.pop("_images", None)
        results.append(report)
        print(json.dumps(report, default=str))
        if not args.keep_work:
            clean_work(args.work / f"interp_{frame:05d}")
            clean_work(args.work / f"extrap_{frame:05d}")
            for capture in args.work.glob(f"*_{frame:05d}.gscap"):
                capture.unlink()
    if args.json:
        args.json.write_text(json.dumps(results, indent=2, default=str), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
