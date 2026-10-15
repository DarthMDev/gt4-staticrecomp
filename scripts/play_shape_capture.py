"""Launch the qualified manual shape-capture candidate with existing save protection."""
import argparse
import sys
from pathlib import Path

from play_external_runtime import ROOT, play
from external_runtime_probe import CAPTURE_MANIFEST


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets-root', type=Path, required=True)
    options = parser.parse_args()
    try:
        play(options.assets_root, ROOT / 'docs/inputs' / CAPTURE_MANIFEST,
             capture_shapes=True)
        return 0
    except (OSError, ValueError, KeyError, StopIteration) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
