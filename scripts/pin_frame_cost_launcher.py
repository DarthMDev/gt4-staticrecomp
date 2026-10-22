"""Pin a TASK-084 slice (260..269) as a launcher profile: writes docs/inputs/slice<N>-pcsx2.json.

The manifest is the release 1.2.0 launcher pin (slice240-pcsx2.json) with this slice's executable, runtime capsule,
license-guard capsule and provenance. Inputs that do not change (shaders, gt4-gs package, SDL3, card seeds, the frozen
EE library, the VU1/VU0 tables) are copied from the base manifest unchanged; the pin tests then verify each of them
against the files on disk, so a copy can only be correct if the files really match.

The output file must not exist: a new slice always gets a new pin file (docs/inputs are history, never edited).
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = 'docs/inputs/slice240-pcsx2.json'


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def pin(number, capsule, guard, build, assets_root=None, purpose=None, status=None, classification=None,
        preserved=None, date='2026-10-22'):
    assets = Path(assets_root or ROOT)
    exe = assets / build / 'gt4vulkan.exe'
    if not exe.exists():
        raise ValueError(f'executable not found: {exe}')
    guard_pin = json.loads((ROOT / 'docs/inputs' / f'{guard}.json').read_text(encoding='utf-8'))
    base = json.loads((ROOT / BASE).read_text(encoding='utf-8'))
    manifest = json.loads(json.dumps(base))
    manifest.update({
        'slice': number,
        'date': date,
        'baseline_commit': 'slice240 (release 1.2.0, main ca0c262)',
        'branch': 'task/084-frame-cost-122 (TASK-084 frame cost, release 1.2.2 candidate)',
        'runtime': {
            'path': f'{build}/gt4vulkan.exe',
            'size_bytes': exe.stat().st_size,
            'sha256': sha256(exe),
        },
        'source_capsule': f'private/{capsule}',
        'source_manifest': f'{capsule}.json',
        'license_guard_capsule': f'private/{guard}',
        'license_guard_receipt_sha256': guard_pin['receipt_sha256'],
        'license_guard_source_sha256': guard_pin['source_sha256'],
        'purpose': purpose or 'TASK-084 frame-cost candidate (runtime side).',
        'status': status or 'candidate; checks in docs/BUILD.md and docs/FRAME-COST-STUDY.md',
        'classification': classification or (
            'TASK-084 candidate on the release 1.2.0 recipe: the slice240 build with the TASK-084 runtime capsule '
            '(instrumentation, VIF1 chain jobs, GIF fast path, VBlank journal) and the TASK-084 license guard '
            '(kernel frame counters). Same port, shaders, EE library, VU1/VU0 tables and gt4-gs package as slice240; '
            'only the executable and its runtime capsule change.'),
        'preserved_inputs': preserved or (
            'Same port (slice200), memcard capsule, shaders (slice153), VU1 and VU0 tables, EE library profile aspect '
            '(slice217) and gt4-gs 1.0.1 package as slice240. Changed: the runtime capsule (private/<capsule>) and the '
            'license guard capsule (kernel.cpp counters only).'),
    })
    output = ROOT / 'docs/inputs' / f'slice{number}-pcsx2.json'
    if output.exists():
        raise ValueError(f'preserve the existing pin: {output}')
    output.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='\n')
    return manifest, output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('slice', type=int)
    parser.add_argument('capsule')
    parser.add_argument('guard')
    parser.add_argument('build', help='build directory relative to the assets root, e.g. private/slice260-build/build')
    parser.add_argument('--assets-root', type=Path)
    options = parser.parse_args()
    manifest, output = pin(options.slice, options.capsule, options.guard, options.build, options.assets_root)
    print(json.dumps({'pin': str(output), 'runtime': manifest['runtime']}, indent=2))
