"""Compare explicit callback-seed recovery without changing production pins."""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import json

from ee_producer import (existing_producer, generated_inventory, identity,
                         inventory_summary, verify_toolchain, write_json, reject_link)
from render_contract_inventory import executable_segments, address_prefix, resolve_dispatch


SPANS = {
    0x0044EA58: (112, '7aaa37ba67140d0364263591c3dca897baba6d8405611b12907d1d60ab1ad284'),
    0x0044EAC8: (68, 'e82cfc898f1123db93397537cbe27ebf7edab55e8f35279a509fd52e94d9cd05'),
}
PROFILES = {'baseline': [], 'supplier_seed': [0x0044EAC8],
            'callback_seed': [0x0044EA58], 'both_seeds': [0x0044EA58, 0x0044EAC8]}


def checked_spans(elf):
    segments = executable_segments(elf)
    result = []
    for address, (size, expected) in SPANS.items():
        observed = address_prefix(segments, address, size)
        if observed['sha256'] != expected:
            raise ValueError('Reviewed callback code span changed')
        result.append({'address': f'0x{address:08X}', **observed,
                       'scope': 'Through return delay slot; excludes following padding'})
    return result


def changed_files(baseline, candidate):
    original = {row['path']: row for row in baseline}
    observed = {row['path']: row for row in candidate}
    if original.keys() != observed.keys():
        raise ValueError('Unexpected generated file set change')
    return [name for name in sorted(original) if original[name] != observed[name]]


def inspect_generated(folder):
    table = (folder / 'table.cpp').read_text(encoding='utf-8')
    sources = {path.stem[5:]: path.read_text(encoding='utf-8') for path in folder.glob('unit_*.cpp')}
    entries = {}
    for address in SPANS:
        unit, entry = resolve_dispatch(table, address, sources, require_registered=False)
        entries[f'0x{address:08X}'] = {'unit': 'unit_' + unit, 'entry_id': entry,
                                      'body_label': f'L_{address:08X}:' in sources[unit]}
    source = sources['00D3']
    continuations = {f'0x{address:08X}': f'L_{address:08X}:' in source
                     for address in (0x0044EA90, 0x0044EAB8, 0x0044EB00)}
    wrapper_unit, wrapper_id = resolve_dispatch(table, 0x00455528, sources)
    pattern = re.compile(r'if \(depth < kMaxDepth\) unit_' + wrapper_unit
                         + r'\(c,\s*' + str(wrapper_id) + r'u,\s*depth \+ 1u\);')
    calls = sum(len(pattern.findall(text)) for text in sources.values())
    stops = sum(text.count('stop(c,') for text in sources.values())
    if stops:
        raise ValueError('Explicit seed recovery emitted unsupported-code stops')
    return {'callbacks': entries, 'continuation_labels': continuations,
            'literal_wrapper_call_sites': calls, 'emitted_stop_sites': stops}


def qualify_profiles(analysis, generated):
    if set(analysis) != set(PROFILES) or set(generated) != set(PROFILES):
        raise ValueError('Missing or duplicate recovery profile')
    addresses = {f'0x{address:08X}' for address in SPANS}
    continuation_addresses = {'0x0044EA90', '0x0044EAB8', '0x0044EB00'}
    baseline_calls = generated['baseline']['literal_wrapper_call_sites']
    for profile in PROFILES:
        callbacks = generated[profile]['callbacks']
        continuations = generated[profile]['continuation_labels']
        candidates = analysis[profile]['candidates']
        observed = {row['address']: row for row in candidates}
        if (set(callbacks) != addresses or set(continuations) != continuation_addresses
                or len(candidates) != 3 or set(observed) != addresses | {'0x0044EB10'}):
            raise ValueError('Unexpected callback or analyzer address set')
        for address in addresses:
            expected = profile in ('supplier_seed', 'both_seeds') or (
                profile == 'callback_seed' and address == '0x0044EA58')
            row = callbacks[address]
            candidate = observed[address]
            if bool(row['entry_id']) != expected or row['body_label'] != expected:
                raise ValueError('Unexpected callback body/registry coverage')
            if candidate['code'] != expected or candidate['entry'] != expected or candidate['rejected']:
                raise ValueError('Analyzer and generated callback coverage disagree')
        dispatcher = observed['0x0044EB10']
        if not dispatcher['code'] or not dispatcher['entry'] or dispatcher['rejected']:
            raise ValueError('Existing dispatcher coverage changed')
        for address in continuation_addresses:
            expected = profile != 'baseline' and (address != '0x0044EB00' or profile != 'callback_seed')
            if continuations[address] != expected:
                raise ValueError('Unexpected callback return continuation coverage')
        expected_calls = baseline_calls + (0 if profile == 'baseline' else 2)
        if generated[profile]['literal_wrapper_call_sites'] != expected_calls:
            raise ValueError('Unexpected render-wrapper direct-call delta')


def probe(output):
    output, metadata, study = existing_producer(output)
    spans = checked_spans((output / 'SCUS_973.28.elf').read_bytes())
    for path in (output / 'tool-build', output / 'tool-build/eerecomp.exe',
                 output / 'tool-build/ee_analysis_controls.exe'):
        reject_link(path)
    verify_toolchain(output / 'tool-build/toolchain.txt', metadata)
    if any((output / name).exists() for name in (*PROFILES, 'entry-coverage.json', 'analysis.jsonl', 'changed-units.txt')):
        raise ValueError('Coverage experiment already attempted; preserve it and select a fresh producer')
    inspection = subprocess.run([str(output / 'tool-build/ee_analysis_controls.exe'), '--inspect',
                                 str(output / 'SCUS_973.28.elf')], capture_output=True,
                                text=True, check=True, timeout=30)
    lines = [json.loads(line) for line in inspection.stdout.splitlines()]
    analysis = {row['profile']: row for row in lines}
    if len(lines) != len(PROFILES) or set(analysis) != set(PROFILES):
        raise ValueError('Unexpected analyzer profile output')
    (output / 'analysis.jsonl').write_text(inspection.stdout, encoding='utf-8')
    records, generated, results = {}, {}, {}
    translator = output / 'tool-build/eerecomp.exe'
    for name, seeds in PROFILES.items():
        folder = output / name
        folder.mkdir()
        arguments = [str(translator), str(output / 'SCUS_973.28.elf'),
                     '--out', str(folder / 'generated'), '--stats']
        seed_identity = None
        if seeds:
            seed_file = folder / 'entries.txt'
            seed_file.write_text(''.join(f'0x{address:08X}\n' for address in seeds), encoding='ascii')
            seed_identity = identity(seed_file)
            arguments += ['--entries', str(seed_file)]
        with (folder / 'generation.log').open('xb') as log:
            subprocess.run(arguments, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
        records[name] = generated_inventory(folder / 'generated')
        summary = inventory_summary(records[name])
        if name == 'baseline' and summary != study['generated']:
            raise ValueError('Diagnostic baseline does not reproduce pinned source')
        generated[name] = inspect_generated(folder / 'generated')
        results[name] = {'seeds': [f'0x{address:08X}' for address in seeds],
                         'seed_file': seed_identity, 'arguments': arguments,
                         'generated': summary, 'changed_files': changed_files(records['baseline'], records[name]),
                         'analysis': analysis[name], 'registry_controls': generated[name]}
    qualify_profiles(analysis, generated)
    if records['supplier_seed'] != records['both_seeds']:
        raise ValueError('Supplier-only and both-seed source differ; review required')
    changed_units = results['supplier_seed']['changed_files']
    if not changed_units or any(not re.fullmatch(r'unit_[0-9A-Fa-f]{4}\.cpp', name) for name in changed_units):
        raise ValueError('Unexpected diagnostic compilation-unit set')
    with (output / 'changed-units.txt').open('x', encoding='ascii') as destination:
        destination.write(''.join(name + '\n' for name in changed_units))
    result = {'schema_version': 1, 'elf': metadata['elf'], 'spans': spans,
              'translator': identity(translator), 'analysis_tool': identity(output / 'tool-build/ee_analysis_controls.exe'),
              'profiles': results, 'supplier_and_both_source_identical': True,
              'qualification': 'Offline recovery/source only; candidate runtime and live reachability unqualified',
              'production_pins_modified': False}
    write_json(output / 'entry-coverage.json', result)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='Fresh prepared producer with analysis tools built')
    args = parser.parse_args(argv)
    try:
        result = probe(args.output)
        print('Verified four recovery profiles; supplier-only and both-seed C++ are identical.')
        print('No production replacement or live reachability qualification.')
        return 0
    except (ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
        print(f'ee-entry-coverage: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
