"""Derive fresh, hash-pinned measurement-only runtime sources; preserve the donor.

Options:
  --split-vu  attribute VU interpreter time per unit (vu_interpreter_category).
  --vu1-aot   also overlay the VU1 AOT registry (scripts/vu1_aot_overlay.inc). Implies
              --split-vu. Default and --split-vu derivations are unchanged by this option.
  --vu0-aot   also overlay the VU0 AOT registry (scripts/vu0_aot_overlay.inc, slice 160). Implies
              --vu1-aot (the VU0 capsule keeps the VU1 table) and --split-vu.
  --mtvu-measure  TASK-011 step 0 (slice 169): also add the counting hooks of native/mtvu_measure.hpp
              to system.cpp and vif.cpp and derive gif.cpp and memory.cpp. Implies --vu0-aot. Counters
              only (GT4_MTVU_MEASURE=0 disables them); no guest-visible behavior change. Derivations
              without this option are unchanged.

Slices 156 and 158 used --gs-crt-override and --display-table-override. Both hooks were removed in slice164
(the 480p switch is a host-side guest RAM write, native/race_display.*). Their capsules stay pinned in
docs/inputs and are not re-derivable from this script.
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
VU0_OVERLAY_PATH = ROOT / 'scripts/vu0_aot_overlay.inc'
# Each overlay section is inserted at one unique donor anchor, by mode.
OVERLAY_ANCHORS = {
    'includes': ('after', '#include "ps2/vu_ops.hpp"\n'),
    'kick_sink': ('before', 'std::uint32_t g_top = 0, g_itop = 0;\n'),
    'xgkick_hook': ('replace', '    gif::transfer(1, buffer, count);\n'),
    'helpers': ('before', 'void run_entry(int unit, std::uint32_t pc) {\n'),
    'run_gate': ('before', '    if (unit == 1 && !interpret_only) {\n'),
}
# VU0 overlay (slice 160). The run() gate sits before the donor's PS2_VU_EXACT line; the helpers before run().
VU0_OVERLAY_ANCHORS = {
    'includes': ('after', '#include "ps2/vu_ops.hpp"\n'),
    'helpers': ('before', 'void run(int n, std::uint32_t address) {\n'),
    'run_gate': ('before', '    static const bool exact = std::getenv("PS2_VU_EXACT") != nullptr;\n'),
}
PREFIX = '// Measurement-only MIT donor derivation; see native/THIRD_PARTY.md.\n#include "perf_probe.hpp"\n'
# TASK-011 step 0 (slice 169): counting hooks only. Each site is one unique donor text; a hook adds
# lines and never changes donor statements, so removing the hooks restores the input (checked).
MTVU_MEASURE_PREFIX = '#include "mtvu_measure.hpp"\n'
MTVU_MEASURE_PATCHES = {
    'system.cpp': [
        ('void vblank() {\n    ++g_frames;\n',
         'void vblank() {\n    ++g_frames;\n    gt4::mtvu::on_vblank();\n'),
        ('    vif::stats();\n',
         '    vif::stats();\n    gt4::mtvu::final_report(stderr);\n')],
    'vif.cpp': [
        ('    vu::run(unit, address);\n}\n',
         '    {\n        gt4::mtvu::VuCallScope mtvu_call(unit);\n        vu::run(unit, address);\n    }\n}\n'),
        ('void transfer(int unit, const std::uint8_t *data, std::uint32_t qwc) {\n'
         '    gt4::perf::Scope perf_vif(gt4::perf::Category::vif_gif);\n',
         'void transfer(int unit, const std::uint8_t *data, std::uint32_t qwc) {\n'
         '    gt4::perf::Scope perf_vif(gt4::perf::Category::vif_gif);\n    gt4::mtvu::on_vif_transfer(unit, qwc);\n'),
        ('std::uint32_t read(int unit, std::uint32_t reg) {\n',
         'std::uint32_t read(int unit, std::uint32_t reg) {\n    gt4::mtvu::on_vif_read(unit, reg);\n'),
        ('void write(int unit, std::uint32_t reg, std::uint32_t value) {\n',
         'void write(int unit, std::uint32_t reg, std::uint32_t value) {\n    gt4::mtvu::on_vif_write(unit, reg);\n')],
    'gif.cpp': [
        ('void transfer(int path, const std::uint8_t *data, std::uint32_t qwc) {\n',
         'void transfer(int path, const std::uint8_t *data, std::uint32_t qwc) {\n'
         '    gt4::mtvu::GifScope mtvu_gif(path, data, qwc);\n')],
    'memory.cpp': [
        ('    const Target t = resolve(a);\n    if (t.host) {\n        T v;\n',
         '    const Target t = resolve(a);\n    if (t.region == Region::Vu) gt4::mtvu::on_ee_vu_memory(t.address, false);\n'
         '    if (t.host) {\n        T v;\n'),
        ('    const Target t = resolve(a);\n    if (t.host) {\n        std::memcpy(t.host, &v, sizeof v);\n',
         '    const Target t = resolve(a);\n    if (t.region == Region::Vu) gt4::mtvu::on_ee_vu_memory(t.address, true);\n'
         '    if (t.host) {\n        std::memcpy(t.host, &v, sizeof v);\n'),
        ('    if (t.host) std::memcpy(&out, t.host, 16);\n',
         '    if (t.region == Region::Vu) gt4::mtvu::on_ee_vu_memory(t.address, false);\n'
         '    if (t.host) std::memcpy(&out, t.host, 16);\n'),
        ('    if (t.host) std::memcpy(t.host, &v, 16);\n',
         '    if (t.region == Region::Vu) gt4::mtvu::on_ee_vu_memory(t.address, true);\n'
         '    if (t.host) std::memcpy(t.host, &v, 16);\n')],
}
# Extra donor sources derived only with --mtvu-measure: capsule name -> (donor path, pinned donor sha256).
MTVU_MEASURE_SOURCES = {
    'gif.cpp': ('gs/gif.cpp', 'eee761d759464b18fa84ca0970dd76fad5da6b7ce594a0b8383905d7016d1884'),
    'memory.cpp': ('memory.cpp', 'c33b9fedc5727f96bcacffad1c806406b442ce86685e3d60777990a67d11f52e'),
}


def overlay_sections(text, anchors=OVERLAY_ANCHORS):
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
    if set(sections) != set(anchors):
        raise ValueError('AOT overlay sections do not match the anchor table')
    return sections


def apply_overlay(source, path, anchors, label):
    """Inserts every overlay section at its anchor; checks that removing them restores the input."""
    sections = overlay_sections(path.read_text(encoding='utf-8').replace('\r\n', '\n'), anchors)
    result = source
    edits = []
    for name, (mode, anchor) in anchors.items():
        if result.count(anchor) != 1:
            raise ValueError(f'Expected one qualified {label} anchor: {name}')
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
            raise ValueError(f'Unexpected {label} overlay reversal')
        inverse = inverse.replace(replacement, anchor)
    if inverse != source:
        raise ValueError(f'{label} overlay does not reverse to the input')
    return result


def apply_vu1_aot(source):
    """VU1 overlay (slice 151-157); its output and receipt are unchanged by the VU0 overlay."""
    return apply_overlay(source, OVERLAY_PATH, OVERLAY_ANCHORS, 'VU1 AOT')


def apply_vu0_aot(source):
    """VU0 overlay (slice 160), applied on top of the VU1 overlay."""
    return apply_overlay(source, VU0_OVERLAY_PATH, VU0_OVERLAY_ANCHORS, 'VU0 AOT')


def apply_mtvu_measure(name, source):
    """Adds the TASK-011 step 0 counting hooks; checks that removing them restores the input."""
    result = source
    for before, after in MTVU_MEASURE_PATCHES[name]:
        if result.count(before) != 1:
            raise ValueError(f'Expected one qualified MTVU measure site in {name}')
        result = result.replace(before, after)
    inverse = result
    for before, after in reversed(MTVU_MEASURE_PATCHES[name]):
        if inverse.count(after) != 1:
            raise ValueError(f'Unexpected MTVU measure reversal in {name}')
        inverse = inverse.replace(after, before)
    if inverse != source:
        raise ValueError(f'MTVU measure hooks do not reverse to the input: {name}')
    return result


def derive_mtvu_source(name, source):
    """Derives one extra --mtvu-measure source (gif.cpp, memory.cpp): hooks only."""
    return PREFIX + MTVU_MEASURE_PREFIX + apply_mtvu_measure(name, source)


def derive(name, source, *, split_vu=False, vu1_aot=False, vu0_aot=False, mtvu_measure=False):
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
    if vu0_aot and name == 'vu_micro.cpp':
        # apply_vu0_aot checks that its overlay reverses to the VU1-overlaid text.
        source = apply_vu0_aot(source)
    inverse = patched
    for before, after in reversed(patches):
        inverse = inverse.replace(after, before)
    if inverse != original:
        raise ValueError('Unexpected patch reversal')
    if mtvu_measure:
        if name in MTVU_MEASURE_PATCHES:
            source = apply_mtvu_measure(name, source)
        return PREFIX + MTVU_MEASURE_PREFIX + source
    return PREFIX + source


def prepare(output, assets_root=None, *, split_vu=False, vu1_aot=False, vu0_aot=False, mtvu_measure=False):
    if mtvu_measure:
        vu0_aot = True
    if vu0_aot:
        vu1_aot = True
    if vu1_aot:
        split_vu = True
    output = Path(output).absolute()
    # The capsule lives in the assets checkout: a worktree has no private/ of its own and must not link it.
    private_root = (Path(assets_root or ROOT) / 'private').resolve()
    if output != output.resolve() or output.parent != private_root:
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
        sources[name] = derive(name, text, split_vu=split_vu, vu1_aot=vu1_aot, vu0_aot=vu0_aot,
                               mtvu_measure=mtvu_measure)
    if mtvu_measure:
        for name, (donor_path, expected) in MTVU_MEASURE_SOURCES.items():
            data = (donor / donor_path).read_bytes()
            if hashlib.sha256(data).hexdigest() != expected:
                raise ValueError(f'Qualified donor source differs: {donor_path}')
            sources[name] = derive_mtvu_source(name, data.decode('utf-8').replace('\r\n', '\n'))
    output.mkdir()
    receipt = {'donor_revision': '1b4808793a12c45125262fd2d5f50a9ec9f7c020',
               'donor_sha256': PINS, 'source_sha256': {}}
    if split_vu:
        receipt['vu_scope_profile'] = 'unit_split_run_fast_only'
    if vu1_aot:
        receipt['vu1_aot_profile'] = 'overlay-v1'
        # Hashed with LF line endings: a checkout with core.autocrlf stores the overlay with CRLF, and the
        # pinned value (slice 154) is the LF text that the derivation parses.
        receipt['vu1_aot_overlay_sha256'] = hashlib.sha256(OVERLAY_PATH.read_bytes().replace(b'\r\n', b'\n')).hexdigest()
    if vu0_aot:
        receipt['vu0_aot_profile'] = 'overlay-v1'
        receipt['vu0_aot_overlay_sha256'] = hashlib.sha256(VU0_OVERLAY_PATH.read_bytes().replace(b'\r\n', b'\n')).hexdigest()
    if mtvu_measure:
        receipt['mtvu_measure_profile'] = 'step0-counters-v1'
        receipt['mtvu_measure_donor_sha256'] = {name: expected for name, (_, expected) in MTVU_MEASURE_SOURCES.items()}
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
    parser.add_argument('--vu0-aot', action='store_true')
    parser.add_argument('--mtvu-measure', action='store_true')
    args = parser.parse_args()
    prepare(args.output, args.assets_root, split_vu=args.split_vu, vu1_aot=args.vu1_aot, vu0_aot=args.vu0_aot,
            mtvu_measure=args.mtvu_measure)
