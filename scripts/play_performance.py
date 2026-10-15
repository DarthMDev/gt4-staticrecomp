"""Open the corrected performance candidate with protected owner-controlled saves.

VU1 AOT candidate (--mode vu1-aot):
  --vu1-verify N         compare every Nth generated VU1 call with the interpreter; mismatches
                         are counted and printed to the session log.
  --vu1-interpret-only   run the same vu1-aot binary with the interpreter only (A/B).

Asynchronous GS candidate (--mode gs-async): VU1 AOT plus asynchronous GS submission. The
same verify and interpreter options apply. Set GT4_GS_SYNC=1 in the environment before
launching to restore the synchronous GS path for A/B comparison.

Pacing candidate (--mode pacing): gs-async plus the paced guest clock (60 Hz wall grid,
non-blocking audio with rate control). Set GT4_PACING=audio to restore the blocking audio
clock for A/B, and GT4_PRESENT_MODE=fifo|mailbox|immediate to test presentation modes
(default fifo). Both variables are recorded in the run evidence.

VU1 flag candidate (--mode vu1-flags): pacing with the slice154 VU1 table, which computes
MAC and clip only where a later reader or the program exit can observe them. Same options,
same pacing environment variables; the VU1 status word is not kept (no image instruction reads it).

VU1 full-entry candidate (--mode vu1-full): vu1-flags with the slice155 static VU1 table (426
entries generated from the pinned image, not from recordings). Same options and environment.

Quick-fix candidate (--mode quickfix): vu1-full with the 16:9 presentation fixed, borderless
fullscreen on Alt+Enter or F11, per-interval pacing counters in the F8 report, and the
experimental 480p request rewrite. --force-480p (GT4_FORCE_480P=1) enables the rewrite;
without it the guest request is passed through unchanged.

VU1 all-entries candidate (--mode vu1-all): vu1-flags with the slice157 static VU1 table, every
8-byte address of the pinned image (2,048 programs, built as 128 parts). Same options and environment.

Quick-fix 2 candidate (--mode quickfix2): vu1-all with the quick fixes of slice156 (16:9, fullscreen,
interval counters, late_frames with a 1 ms tolerance) and the slice158 CRT table rewrite. Kept as history:
its build no longer compiles from this tree (native/display_table.* was removed in slice164).

Current main (--mode main, slice166): everything merged to main (all VU1 and VU0 entries compiled, quick
fixes); accepts --force-480p, --vu0-verify N and the VU1 options. This is the build to play.

Quick-fix 3 candidate (--mode quickfix3, slice164): vu1-all with the quick fixes. --force-480p (GT4_FORCE_480P=1,
default off) holds the game's race display option at 480p progressive: at each guest VBlank, when the option
index is 0 (normal) it is written 1 (480p). Indices 1 and 2 are never changed.
VU0 candidate (--mode vu0-aot, slice 160): vu1-all plus the VU0 AOT overlay, which runs the 14 VCALLMS
entries of the pinned VU0 image compiled. --vu0-verify N compares every Nth generated VU0 call with the
exact interpreter path (GT4_VU0_VERIFY); the VU1 options apply as in vu1-all.

paraLLEl-GS candidate (--mode parallel, slice168, TASK-012): the main feature set (all VU1 and VU0 entries
compiled, quick fixes) with the paraLLEl-GS renderer and window output. --renderer parallel (default) or
--renderer donor runs the same binary with either renderer for A/B. --ssaa N (1, 2, 4, 8 or 16, default 1)
sets paraLLEl-GS super-sampling (experimental); --hires (with --ssaa 4, 8 or 16) presents paraLLEl-GS's
high-resolution scanout (2x horizontal; 2x vertical in 480p, full-frame 448 lines from the 224-line fields in
480i menus). Accepts --force-480p, --vu0-verify N and the VU1 options.
High-resolution candidate (--mode parallel-hires, slice185, TASK-012): the paraLLEl-GS renderer with a 2x or 4x
scanout. --hires N (2 or 4) is required and sets GT4_PGS_HIRES=N and GT4_PGS_SSAA=16 (the 4x4 ordered grid); 2 is
the donor 2x quadrant average, 4 is one super sample per output pixel. The window opens at 16:9 with height 448 * N,
clamped to the primary work area and centred. The 3x path is not exposed (owner decision).
--mode parallel keeps the slice169 build: --hires without a value there is the old 2x option (GT4_PGS_HIRES=1).
The build lives in the assets checkout's private/ even when this launcher runs from a git worktree.
"""
import argparse
import sys
from pathlib import Path
from play_external_runtime import ROOT, play
from external_runtime_probe import PERF_PROFILES, VU1_RECORD_MANIFEST

MODES = tuple(PERF_PROFILES) + ('vu1-record',)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--assets-root', type=Path, required=True)
    parser.add_argument('--mode', choices=MODES, default='detailed')
    parser.add_argument('--vu1-verify', type=int, default=0, metavar='N',
                        help='sample every Nth generated VU1 call and compare (vu1-aot, gs-async, pacing and vu1-flags)')
    parser.add_argument('--vu1-interpret-only', action='store_true',
                        help='force the interpreter in the vu1-aot, gs-async, pacing and vu1-flags binaries')
    parser.add_argument('--force-480p', action='store_true',
                        help='experimental: hold the race display option at 480p (quickfix3 mode)')
    parser.add_argument('--vu0-verify', type=int, default=0, metavar='N',
                        help='sample every Nth generated VU0 call and compare with the exact interpreter (vu0-aot only)')
    parser.add_argument('--renderer', choices=('parallel', 'donor'),
                        help='parallel mode only: GS renderer of the same binary (default parallel)')
    parser.add_argument('--ssaa', type=int, default=1, metavar='N',
                        help='parallel mode with the parallel renderer: super-sampling 1, 2, 4, 8 or 16 (experimental)')
    parser.add_argument('--hires', type=int, nargs='?', const=1, metavar='N',
                        help='high-resolution scanout. parallel: no value (2x, needs --ssaa 4, 8 or 16). '
                             'parallel-hires: N = 2 or 4 with GT4_PGS_SSAA=16')
    args = parser.parse_args()
    try:
        if (args.vu1_verify or args.vu1_interpret_only) and args.mode not in ('vu1-aot', 'gs-async', 'pacing', 'vu1-flags', 'vu1-full', 'quickfix', 'vu1-all', 'quickfix2', 'quickfix3', 'vu0-aot', 'main', 'parallel', 'parallel-hires'):
            raise ValueError('--vu1-verify and --vu1-interpret-only require --mode vu1-aot, gs-async, pacing, vu1-flags, vu1-full, quickfix, vu1-all, quickfix2, quickfix3, vu0-aot, main, parallel or parallel-hires')
        if args.vu0_verify and args.mode not in ('vu0-aot', 'main', 'parallel', 'parallel-hires'):
            raise ValueError('--vu0-verify requires --mode vu0-aot, main, parallel or parallel-hires')
        if args.renderer and args.mode not in ('parallel', 'parallel-hires'):
            raise ValueError('--renderer requires --mode parallel or parallel-hires')
        if args.ssaa != 1 and (args.mode not in ('parallel', 'parallel-hires') or args.renderer == 'donor' or args.ssaa not in (2, 4, 8, 16)):
            raise ValueError('--ssaa takes 1, 2, 4, 8 or 16 and requires --mode parallel with the parallel renderer')
        if args.mode == 'parallel-hires':
            if args.hires not in (2, 4):
                raise ValueError('--mode parallel-hires needs --hires 2 or 4')
            if args.renderer == 'donor' or args.ssaa not in (1, 16):
                raise ValueError('--mode parallel-hires is the paraLLEl-GS scanout: no --renderer donor, and --ssaa is 16 (set by --hires)')
        elif args.hires is not None:
            if args.mode != 'parallel' or args.renderer == 'donor' or args.ssaa not in (4, 8, 16) or args.hires != 1:
                raise ValueError('--hires without a value requires --mode parallel with the parallel renderer and --ssaa 4, 8 or 16; use --mode parallel-hires for --hires 2 or 4')
        if args.vu1_verify < 0:
            raise ValueError('--vu1-verify takes a positive N')
        if args.vu1_verify and args.vu1_interpret_only:
            raise ValueError('--vu1-verify compares against the AOT path; it cannot be combined with --vu1-interpret-only')
        if args.force_480p and args.mode not in ('quickfix', 'quickfix2', 'quickfix3', 'main', 'parallel', 'parallel-hires'):
            raise ValueError('--force-480p requires --mode quickfix, quickfix2, quickfix3, main, parallel or parallel-hires')
        if args.mode == 'vu1-record':
            play(args.assets_root, ROOT / 'docs/inputs' / VU1_RECORD_MANIFEST,
                 performance=True, vu1_record=True)
        else:
            manifest, _ = PERF_PROFILES[args.mode]
            options = {}
            if args.vu1_verify:
                options['vu1_verify'] = args.vu1_verify
            if args.vu1_interpret_only:
                options['vu1_interpret_only'] = True
            if args.force_480p:
                options['force_480p'] = True
            if args.vu0_verify:
                options['vu0_verify'] = args.vu0_verify
            if args.renderer:
                options['renderer'] = args.renderer
            if args.mode == 'parallel':
                if args.ssaa != 1:
                    options['super_sampling'] = args.ssaa
                if args.hires:
                    options['high_resolution'] = True
            if args.mode == 'parallel-hires':
                options['super_sampling'] = 16
                options['high_resolution'] = args.hires
            play(args.assets_root, ROOT / 'docs/inputs' / manifest, performance=True, **options)
        return 0
    except (OSError, ValueError, KeyError, StopIteration) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
