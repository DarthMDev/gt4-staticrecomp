"""Derive fresh, hash-pinned measurement-only runtime sources; preserve the donor.

Options:
  --split-vu  attribute VU interpreter time per unit (vu_interpreter_category).
  --vu1-aot   also overlay the VU1 AOT registry (scripts/vu1_aot_overlay.inc). Implies
              --split-vu. Default and --split-vu derivations are unchanged by this option.
"""
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
OVERLAY_PATH = ROOT / 'scripts/vu1_aot_overlay.inc'
# Each overlay section is inserted at one unique donor anchor, by mode.
OVERLAY_ANCHORS = {
    'includes': ('after', '#include "ps2/vu_ops.hpp"\n'),
    'kick_sink': ('before', 'std::uint32_t g_top = 0, g_itop = 0;\n'),
    'xgkick_hook': ('replace', '    gif::transfer(1, buffer, count);\n'),
    'helpers': ('before', 'void run_entry(int unit, std::uint32_t pc) {\n'),
    'run_gate': ('before', '    if (unit == 1 && !interpret_only) {\n'),
}
PREFIX = '// Measurement-only MIT donor derivation; see native/THIRD_PARTY.md.\n#include "perf_probe.hpp"\n'


def overlay_sections(text):
    sections, name, lines = {}, None, []
    for line in text.split('\n'):
        if line.startswith('//@section '):
            if name is not None:
                sections[name] = '\n'.join(lines).rstrip('\n') + '\n'
            name, lines = line.removeprefix('//@section ').strip(), []
        else:
            lines.append(line)
    if name is not None:
        sections[name] = '\n'.join(lines).rstrip('\n') + '\n'
    if set(sections) != set(OVERLAY_ANCHORS):
        raise ValueError('VU1 AOT overlay sections do not match the anchor table')
    return sections


def apply_vu1_aot(source):
    """Inserts every overlay section; checks that removing them restores the input."""
    sections = overlay_sections(OVERLAY_PATH.read_text(encoding='utf-8').replace('\r\n', '\n'))
    result = source
    edits = []
    for name, (mode, anchor) in OVERLAY_ANCHORS.items():
        if result.count(anchor) != 1:
            raise ValueError(f'Expected one qualified VU1 AOT anchor: {name}')
        text = sections[name]
        if mode == 'after':
            replacement = anchor + text
        elif mode == 'before':
            replacement = text + anchor
        else:
            replacement = text
        result = result.replace(anchor, replacement)
        edits.append((anchor, replacement))
    inverse = result
    for anchor, replacement in reversed(edits):
        if inverse.count(replacement) != 1:
            raise ValueError('Unexpected VU1 AOT overlay reversal')
        inverse = inverse.replace(replacement, anchor)
    if inverse != source:
        raise ValueError('VU1 AOT overlay does not reverse to the input')
    return result


def derive(name, source, *, split_vu=False, vu1_aot=False):
    original = source
    patches = [(before, after.replace('gt4::perf::Category::vu_interpret',
                'gt4::perf::vu_interpreter_category(unit)') if split_vu else after)
               for before, after in PATCHES[name]]
    for before, after in patches:
        if source.count(before) != 1:
            raise ValueError(f'Expected one qualified patch site in {name}')
        source = source.replace(before, after)
    patched = source
    if vu1_aot and name == 'vu_micro.cpp':
        # apply_vu1_aot checks that its own overlay reverses to the patched text.
        source = apply_vu1_aot(patched)
    inverse = patched
    for before, after in reversed(patches):
        inverse = inverse.replace(after, before)
    if inverse != original:
        raise ValueError('Unexpected patch reversal')
    return PREFIX + source


def prepare(output, assets_root=None, *, split_vu=False, vu1_aot=False):
    if vu1_aot:
        split_vu = True
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
        sources[name] = derive(name, text, split_vu=split_vu, vu1_aot=vu1_aot)
    output.mkdir()
    receipt = {'donor_revision': '1b4808793a12c45125262fd2d5f50a9ec9f7c020',
               'donor_sha256': PINS, 'source_sha256': {}}
    if split_vu:
        receipt['vu_scope_profile'] = 'unit_split_run_fast_only'
    if vu1_aot:
        receipt['vu1_aot_profile'] = 'overlay-v1'
        receipt['vu1_aot_overlay_sha256'] = hashlib.sha256(OVERLAY_PATH.read_bytes()).hexdigest()
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
    parser.add_argument('--vu1-aot', action='store_true')
    args = parser.parse_args()
    prepare(args.output, args.assets_root, split_vu=args.split_vu, vu1_aot=args.vu1_aot)
