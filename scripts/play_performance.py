"""Open the corrected performance candidate with protected owner-controlled saves.

VU1 AOT candidate (--mode vu1-aot):
  --vu1-verify N         compare every Nth generated VU1 call with the interpreter; mismatches
                         are counted and printed to the session log.
  --vu1-interpret-only   run the same vu1-aot binary with the interpreter only (A/B).
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
    parser.add_argument('--aspect', choices=('16:9', '4:3'), default='16:9')
    parser.add_argument('--mode', choices=MODES, default='detailed')
    parser.add_argument('--vu1-verify', type=int, default=0, metavar='N',
                        help='sample every Nth generated VU1 call and compare (vu1-aot only)')
    parser.add_argument('--vu1-interpret-only', action='store_true',
                        help='force the interpreter in the vu1-aot binary (vu1-aot only)')
    args = parser.parse_args()
    try:
        if (args.vu1_verify or args.vu1_interpret_only) and args.mode != 'vu1-aot':
            raise ValueError('--vu1-verify and --vu1-interpret-only require --mode vu1-aot')
        if args.vu1_verify < 0:
            raise ValueError('--vu1-verify takes a positive N')
        if args.vu1_verify and args.vu1_interpret_only:
            raise ValueError('--vu1-verify compares against the AOT path; it cannot be combined with --vu1-interpret-only')
        if args.mode == 'vu1-record':
            play(args.aspect, args.assets_root, ROOT / 'docs/inputs' / VU1_RECORD_MANIFEST,
                 performance=True, vu1_record=True)
        else:
            manifest, _ = PERF_PROFILES[args.mode]
            options = {}
            if args.vu1_verify:
                options['vu1_verify'] = args.vu1_verify
            if args.vu1_interpret_only:
                options['vu1_interpret_only'] = True
            play(args.aspect, args.assets_root, ROOT / 'docs/inputs' / manifest, performance=True, **options)
        return 0
    except (OSError, ValueError, KeyError, StopIteration) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
