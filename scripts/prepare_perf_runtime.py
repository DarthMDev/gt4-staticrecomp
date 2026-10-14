"""Derive fresh, hash-pinned measurement-only runtime sources; preserve the donor."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PINS = {
    'system.cpp': 'f30c8dd90c40e930635375ae2a3091f052655d797744de4348a5c7261a710bb2',
    'vu_micro.cpp': '5fc349d96364f72818da96cd2376637421d3d4d9150547c22034863c15d22a1d',
    'vif.cpp': '6fc9efc739daec951172bc7f97f64b05802145314bc17b049c0c3219be654e18',
}
PATCHES = {
    'system.cpp': [
        ('        iop::run(t - g_iop_synced);',
         '        gt4::perf::Scope perf_iop(gt4::perf::Category::iop_run);\n        iop::run(t - g_iop_synced);'),
        ('        dispatch(c, 0, 0);',
         '        gt4::perf::Scope perf_ee(gt4::perf::Category::ee_dispatch);\n        dispatch(c, 0, 0);')],
    'vu_micro.cpp': [
        ('    run_fast(unit, pc);',
         '    {\n        gt4::perf::Scope perf_vu(gt4::perf::Category::vu_interpret);\n        run_fast(unit, pc);\n    }')],
    'vif.cpp': [
        ('void transfer(int unit, const std::uint8_t *data, std::uint32_t qwc) {\n',
         'void transfer(int unit, const std::uint8_t *data, std::uint32_t qwc) {\n    gt4::perf::Scope perf_vif(gt4::perf::Category::vif_gif);\n')],
}


def derive(name, source, *, split_vu=False):
    original = source
    patches = [(before, after.replace('gt4::perf::Category::vu_interpret',
                'gt4::perf::vu_interpreter_category(unit)') if split_vu else after)
               for before, after in PATCHES[name]]
    for before, after in patches:
        if source.count(before) != 1:
            raise ValueError(f'Expected one qualified patch site in {name}')
        source = source.replace(before, after)
    prefix = '// Measurement-only MIT donor derivation; see native/THIRD_PARTY.md.\n#include "perf_probe.hpp"\n'
    result = prefix + source
    inverse = result.removeprefix(prefix)
    for before, after in reversed(patches):
        inverse = inverse.replace(after, before)
    if inverse != original:
        raise ValueError('Unexpected patch reversal')
    return result


def prepare(output, assets_root=None, *, split_vu=False):
    output = Path(output).absolute()
    if output != output.resolve() or output.parent != (ROOT / 'private').resolve():
        raise ValueError('Runtime capsule must be an ordinary immediate private child')
    if output.exists():
        raise ValueError('Preserve existing output; choose a fresh runtime capsule')
    donor = Path(assets_root or ROOT) / 'private/references/ps2-web-recomp/runtime/src'
    sources = {}
    for name, expected in PINS.items():
        data = (donor / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError(f'Qualified donor source differs: {name}')
        text = data.decode('utf-8').replace('\r\n', '\n')
        derived = derive(name, text, split_vu=split_vu)
        sources[name] = derived
    output.mkdir()
    receipt = {'donor_revision': '1b4808793a12c45125262fd2d5f50a9ec9f7c020',
               'donor_sha256': PINS, 'source_sha256': {}}
    if split_vu:
        receipt['vu_scope_profile'] = 'unit_split_run_fast_only'
    for name, text in sources.items():
        path = output / name
        path.write_text(text, encoding='utf-8', newline='\n')
        receipt['source_sha256'][name] = hashlib.sha256(path.read_bytes()).hexdigest()
    (output / 'runtime.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--assets-root', type=Path)
    parser.add_argument('--split-vu', action='store_true')
    args = parser.parse_args()
    prepare(args.output, args.assets_root, split_vu=args.split_vu)
