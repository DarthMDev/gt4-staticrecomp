"""Open the corrected performance candidate with protected owner-controlled saves."""
import argparse
import sys
from pathlib import Path
from play_external_runtime import ROOT, play
from external_runtime_probe import PERF_PROFILES


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets-root', type=Path, required=True)
    parser.add_argument('--aspect', choices=('16:9', '4:3'), default='16:9')
    parser.add_argument('--mode', choices=tuple(PERF_PROFILES), default='detailed')
    args = parser.parse_args()
    try:
        manifest, _ = PERF_PROFILES[args.mode]
        play(args.aspect, args.assets_root, ROOT / 'docs/inputs' / manifest, performance=True)
        return 0
    except (OSError, ValueError, KeyError, StopIteration) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
