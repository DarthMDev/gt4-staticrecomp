"""Launch the qualified manual shape-capture candidate with existing save protection."""
import argparse
import sys
from pathlib import Path

from play_external_runtime import ROOT, play


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets-root', type=Path, required=True)
    parser.add_argument('--aspect', choices=('16:9', '4:3'), default='16:9')
    options = parser.parse_args()
    try:
        play(options.aspect, options.assets_root, ROOT / 'docs/inputs/slice134-manual-capture.json',
             capture_shapes=True)
        return 0
    except (OSError, ValueError, KeyError, StopIteration) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
