"""Launch a tested isolated EE candidate using the owner's existing card and locks."""

import argparse
from pathlib import Path
import sys

from play_external_runtime import ROOT, play


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=('baseline', 'recovered'), default='baseline')
    parser.add_argument('--assets-root', type=Path, required=True)
    parser.add_argument('--aspect', choices=('16:9', '4:3'), default='16:9')
    options = parser.parse_args()
    manifest = ROOT / f'docs/inputs/slice118-ee-{options.profile}.json'
    try:
        play(options.aspect, options.assets_root, manifest)
        return 0
    except (OSError, ValueError, KeyError, StopIteration) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
