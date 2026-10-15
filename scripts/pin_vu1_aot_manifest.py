"""Pin the VU1 AOT runtime capsule: writes docs/inputs/slice151-vu1-aot-runtime.json.

Run after scripts/prepare_perf_runtime.py --vu1-aot. Inputs (PORT, SHADERS) are copied
from the qualified slice149 split manifest; every source and the receipt are re-hashed.
"""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CAPSULE = ROOT / 'private/slice151-vu1-aot-runtime'
BASE = ROOT / 'docs/inputs/slice149-vu-split-runtime.json'
OUTPUT = ROOT / 'docs/inputs/slice151-vu1-aot-runtime.json'


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    base = json.loads(BASE.read_text(encoding='utf-8'))
    receipt = json.loads((CAPSULE / 'runtime.json').read_text(encoding='utf-8'))
    manifest = {
        'donor_revision': base['donor_revision'],
        'donor_sha256': base['donor_sha256'],
        'source_sha256': {name: sha256(CAPSULE / name) for name in ('system.cpp', 'vu_micro.cpp', 'vif.cpp')},
        'receipt_sha256': sha256(CAPSULE / 'runtime.json'),
        'inputs': base['inputs'],
        'vu_scope_profile': receipt['vu_scope_profile'],
        'vu1_aot_profile': receipt['vu1_aot_profile'],
        'vu1_aot_overlay_sha256': receipt['vu1_aot_overlay_sha256'],
    }
    OUTPUT.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f'pinned {OUTPUT.relative_to(ROOT)}')


if __name__ == '__main__':
    main()
