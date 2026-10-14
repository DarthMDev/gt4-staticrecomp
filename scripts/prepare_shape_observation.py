"""Prepare pinned observer-only runtime overlays in a fresh private capsule."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from ee_producer import (DONOR_COMMIT, trusted_manifests, validate_output,
                         verify_donor, write_json)

SOURCE_PINS = {
    'dispatch.cpp': 'ff9ecf53655ad02a76963fca70a90016bb2a53d8fe0dee8e20cbb00f27db79a3',
    'kernel.cpp': 'cfd4904fa6c4628dfc0eb3f1316eb60e2fb6b3481f0c821a05c1e056027cf8ef',
    'system.cpp': '8dbb63c3014fc2f95a82557629190585fb1418e43353760a872949cdc2d92ff8',
}


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise ValueError('Runtime overlay anchor is missing or ambiguous')
    return source.replace(before, after)


def overlay_source(name, data):
    if name not in SOURCE_PINS or type(data) is not bytes or hashlib.sha256(data).hexdigest() != SOURCE_PINS[name]:
        raise ValueError('Runtime overlay source differs from pinned canonical donor bytes')
    source = data.decode('utf-8')
    replacements = []
    if name == 'dispatch.cpp':
        anchor = 'void probe(Cpu &c, std::uint32_t pc) {\n'
        replacements.append((anchor, anchor + '    if (gt4::observation::observe_probe(c, pc)) return;\n'))
    elif name == 'kernel.cpp':
        replacements.extend([
            ('    g_threads[t.id] = t;\n    if (g_options.trace_threads)',
             '    gt4::observation::invalidate_runtime_owner(t.id);\n    g_threads[t.id] = t;\n    if (g_options.trace_threads)'),
            ('void reset_thread(Thread &t, std::uint32_t argument) {\n',
             'void reset_thread(Thread &t, std::uint32_t argument) {\n    gt4::observation::invalidate_runtime_owner(t.id);\n'),
            ('    if (!t) return;\n    t->status = kDormant;\n',
             '    if (!t) return;\n    gt4::observation::invalidate_runtime_owner(t->id);\n    t->status = kDormant;\n'),
            ('    case 0x21: g_threads.erase(static_cast<int>(arg(c, 0))); ret(c, 0); break;',
             '    case 0x21: gt4::observation::invalidate_runtime_owner(static_cast<int>(arg(c, 0))); g_threads.erase(static_cast<int>(arg(c, 0))); ret(c, 0); break;'),
            ('        if (t) {\n            for (auto &[id, s] : g_semas)',
             '        if (t) {\n            gt4::observation::invalidate_runtime_owner(t->id);\n            for (auto &[id, s] : g_semas)'),
            ('void reset() {\n', 'void reset() {\n    gt4::observation::reset_runtime_epoch();\n'),
            ('void start_main_thread(std::uint32_t entry, std::uint32_t gp, std::uint32_t argc, std::uint32_t argv) {\n',
             'void start_main_thread(std::uint32_t entry, std::uint32_t gp, std::uint32_t argc, std::uint32_t argv) {\n    gt4::observation::invalidate_runtime_owner(1);\n'),
        ])
    else:
        for anchor in ('bool load_state(const std::uint8_t *data, std::size_t size) {\n',
                       'bool load_state(const std::string &path) {\n',
                       'void boot(std::uint32_t entry, const std::vector<std::string> &args) {\n'):
            replacements.append((anchor, anchor + '    gt4::observation::reset_runtime_epoch();\n'))
    for before, after in replacements:
        source = replace_once(source, before, after)
    return ('#include "shape_observer_adapter.hpp"\n' + source).encode('utf-8')


def prepare_runtime(assets_root, output):
    assets_root = assets_root.resolve()
    reference = next(row for row in trusted_manifests()['external-recomp-references.json']['references']
                     if row['name'] == 'ps2-web-recomp')
    donor = verify_donor(assets_root, reference)
    prepared = {}
    for name in SOURCE_PINS:
        canonical = subprocess.check_output(['git', '-C', str(donor), 'show',
                                             f'{DONOR_COMMIT}:runtime/src/{name}'])
        if (donor / 'runtime/src' / name).read_bytes().replace(b'\r\n', b'\n') != canonical:
            raise ValueError('Runtime worktree text differs from canonical donor source')
        prepared[name] = overlay_source(name, canonical)
    output = validate_output(output, assets_root)
    output.mkdir()
    for name, data in prepared.items():
        with (output / name).open('xb') as stream:
            stream.write(data)
    result = {'schema_version': 1, 'donor_revision': DONOR_COMMIT,
              'source_canonical_sha256': SOURCE_PINS,
              'overlay_sha256': {name: hashlib.sha256(data).hexdigest() for name, data in prepared.items()},
              'qualification': 'Prepared only; native link/lifecycle controls pending'}
    write_json(output / 'runtime-overlay.json', result)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare_runtime(args.assets_root, args.output), indent=2))
