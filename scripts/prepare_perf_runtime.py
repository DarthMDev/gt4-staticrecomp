"""Derive a private copy of the frozen donor runtime with subsystem timers.

The performance investigation must attribute emulation-thread time to EE
translated execution, VU interpretation, IOP execution and VIF/GIF packet
processing. The frozen donor checkout is never modified: this script verifies
the donor hashes first, copies only the files it patches into a private output
directory, and applies named one-line measurement patches that open a
gt4::perf::Scope. The patches add no guest-visible behavior, so the runtime
semantics and the rendered output stay identical; the pixel-identical frame
capture control is re-run for every variant produced here.
"""

import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DONOR = ROOT / 'private/references/ps2-web-recomp'
RELATIVE = 'runtime/src'

# Donor files touched by the patches, pinned before any copy.
PINS = {
    'system.cpp': 'f30c8dd90c40e930635375ae2a3091f052655d797744de4348a5c7261a710bb2',
    'vu_micro.cpp': '5fc349d96364f72818da96cd2376637421d3d4d9150547c22034863c15d22a1d',
    'vif.cpp': '6fc9efc739daec951172bc7f97f64b05802145314bc17b049c0c3219be654e18',
}

# (file, unique donor text, replacement, short description)
PATCHES = [
    (
        'system.cpp',
        '        iop::run(t - g_iop_synced);',
        '        gt4::perf::Scope iop_run(gt4::perf::Stage::iop_run);\n'
        '        iop::run(t - g_iop_synced);',
        'IOP execution',
    ),
    (
        'system.cpp',
        '        dispatch(c, 0, 0);',
        '        gt4::perf::Scope ee_dispatch(gt4::perf::Stage::ee_dispatch);\n'
        '        dispatch(c, 0, 0);',
        'EE translated execution',
    ),
    (
        'vu_micro.cpp',
        '    run_fast(unit, pc);',
        '    {\n'
        '        gt4::perf::Scope vu_interpret(gt4::perf::Stage::vu_interpret);\n'
        '        run_fast(unit, pc);\n'
        '    }',
        'interpreted VU microprogram execution',
    ),
    (
        'vif.cpp',
        'void transfer(int unit, const std::uint8_t *data, std::uint32_t qwc) {\n',
        'void transfer(int unit, const std::uint8_t *data, std::uint32_t qwc) {\n'
        '    gt4::perf::Scope vif_transfer(gt4::perf::Stage::vif_gif);\n',
        'VIF/GIF packet processing',
    ),
]


def pin(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def apply_patches(name, source, provenance):
    for patched, original, replacement, description in PATCHES:
        if patched != name:
            continue
        occurrences = source.count(original)
        if occurrences != 1:
            raise ValueError(f'{name}: patch site must occur exactly once, '
                             f'found {occurrences}: {description}')
        source = source.replace(original, replacement)
        provenance['patches'].append({'file': name, 'stage': description,
                                     'donor_text': original})
    return source


def prepare(output):
    output = output.resolve()
    private = ROOT / 'private'
    if not output.is_relative_to(private) or output == private:
        raise ValueError('Derived runtime must stay inside private/')
    donor_root = DONOR / RELATIVE
    if output == donor_root or donor_root.is_relative_to(output) or output.is_relative_to(donor_root):
        raise ValueError('Derived runtime cannot replace the frozen donor checkout')
    for name, expected in PINS.items():
        if expected == 'PLACEHOLDER':
            continue
        actual = pin(donor_root / name)
        if actual != expected:
            raise ValueError(f'Donor source differs from the qualified version: {name}')
    patched_names = sorted({item[0] for item in PATCHES})
    provenance = {'donor': 'private/references/ps2-web-recomp',
                  'donor_files': {}, 'patches': [],
                  'probe_header': 'native/perf_probe.hpp'}
    for name in patched_names:
        source = (donor_root / name).read_text(encoding='utf-8')
        provenance['donor_files'][name] = pin(donor_root / name)
        source = apply_patches(name, source, provenance)
        # The probe header ships with the native sources; prepend the include
        # so the derived file does not depend on the donor include order.
        source = ('// Derived for the performance investigation by '
                  'scripts/prepare_perf_runtime.py; measurement only.\n'
                  '#include "perf_probe.hpp"\n' + source)
        if 'perf_probe.hpp' not in source:
            raise ValueError(f'{name}: probe header was not included')
        (output / name).parent.mkdir(parents=True, exist_ok=True)
        (output / name).write_text(source, encoding='utf-8')
        provenance['derived_files'] = provenance.get('derived_files', {})
        provenance['derived_files'][name] = pin(output / name)
    (output / 'provenance.json').write_text(
        json.dumps(provenance, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    return provenance


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    options = parser.parse_args()
    prepare(options.output)
    print(f'{options.output}: derived runtime with subsystem timers')
