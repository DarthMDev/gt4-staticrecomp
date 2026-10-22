"""Pin a license guard capsule generated with --frame-counters (TASK-084, slice260) into docs/inputs/.

The capsule's runtime.json already holds every source hash, the donor pins, the overlay hashes and the profile names;
the pin file is that receipt plus a sha256 of the receipt itself, which native/CMakeLists.txt checks. The output file
must not exist: a new capsule always gets a new pin file (docs/inputs are history, never edited).
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def pin(capsule_name, assets_root=None):
    assets = Path(assets_root or ROOT)
    capsule = assets / 'private' / capsule_name
    receipt_path = capsule / 'runtime.json'
    if not receipt_path.exists():
        raise ValueError(f'capsule receipt not found: {receipt_path}')
    receipt = json.loads(receipt_path.read_text(encoding='utf-8'))
    if receipt.get('frame_counters_profile') is None:
        raise ValueError('the capsule was not generated with --frame-counters')
    manifest = dict(receipt)
    manifest['source_sha256'] = {name: sha256(capsule / name) for name in receipt['source_sha256']}
    if manifest['source_sha256'] != receipt['source_sha256']:
        raise ValueError('capsule sources changed after the receipt was written')
    manifest['receipt_sha256'] = sha256(receipt_path)
    output = ROOT / 'docs/inputs' / f'{capsule_name}.json'
    if output.exists():
        raise ValueError(f'preserve the existing pin: {output}')
    output.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='\n')
    return manifest, output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capsule', help='guard capsule directory name under private/')
    parser.add_argument('--assets-root', type=Path)
    options = parser.parse_args()
    manifest, output = pin(options.capsule, options.assets_root)
    print(json.dumps({'pin': str(output), 'receipt_sha256': manifest['receipt_sha256']}, indent=2))
