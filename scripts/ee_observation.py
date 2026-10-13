"""Generate the exact two-probe research profile without relaxing baseline gates."""

import argparse
import json
from pathlib import Path
import subprocess

from ee_producer import (ROOT, existing_producer, generated_inventory, identity,
                         inventory_summary, read_json, reject_link, validate_output,
                         verify_identity, verify_toolchain, write_json)

PROBE_FILE = b'0x00455A5C\n0x00455A64\n'
PROBE_LINES = (b'    probe(c, 0x00455A5Cu);\n', b'    probe(c, 0x00455A64u);\n')
LABELS = (b'L_00455A5C:\n', b'L_00455A64:\n')
UNIT = 'unit_00D5.cpp'


def validate_probes(data):
    if type(data) is not bytes or data != PROBE_FILE:
        raise ValueError('Probe file must contain only the exact two canonical frontier addresses')


def qualify_unit(original, candidate):
    if type(original) is not bytes or type(candidate) is not bytes:
        raise ValueError('Generated source must be exact bytes')
    if b'probe(c,' in original:
        raise ValueError('Baseline already contains probes')
    expected = original
    for label, probe_line in zip(LABELS, PROBE_LINES):
        if original.count(label) != 1:
            raise ValueError('Missing or ambiguous baseline frontier label')
        expected = expected.replace(label, label + probe_line)
    if candidate != expected:
        raise ValueError('Candidate differs beyond the two exact pre-instruction probes')


def qualify_profile(original, candidate, baseline_records):
    reject_link(candidate)
    expected = {row['path']: row for row in baseline_records}
    if UNIT not in expected or len(expected) != len(baseline_records):
        raise ValueError('Baseline profile lacks the selected unit or repeats a filename')
    paths = list(candidate.iterdir())
    if {path.name for path in paths} != expected.keys():
        raise ValueError('Observation profile file membership changed')
    for path in paths:
        reject_link(path)
        allowed_size = expected[path.name]['size_bytes']
        if path.name == UNIT:
            allowed_size += sum(map(len, PROBE_LINES))
        if not path.is_file() or path.stat().st_size != allowed_size:
            raise ValueError('Observation profile source extent changed')
    records = generated_inventory(candidate)
    for row in records:
        if row['path'] == UNIT:
            qualify_unit((original / UNIT).read_bytes(), (candidate / UNIT).read_bytes())
        elif row != expected[row['path']]:
            raise ValueError('Unexpected source change outside the selected unit')
    return records


def generate_observation(baseline, output):
    baseline, metadata, study = existing_producer(baseline)
    original = baseline / 'generated'
    records = generated_inventory(original)
    if inventory_summary(records) != study['generated']:
        raise ValueError('Baseline generated inventory differs from frozen source')
    if read_json(baseline / 'generation.json')['generated'] != records:
        raise ValueError('Baseline generation receipt changed')
    translator = baseline / 'tool-build/eerecomp.exe'
    reject_link(translator)
    toolchain = verify_toolchain(baseline / 'tool-build/toolchain.txt', metadata)
    evidence = read_json(ROOT / 'docs/inputs/slice116-ee-producer-contracts.json')
    translator_pin = verify_identity(translator, evidence['fresh_translator'])
    output = validate_output(output, Path(metadata['assets_root']))
    output.mkdir()
    probes = output / 'shape-probes.txt'
    with probes.open('xb') as stream:
        stream.write(PROBE_FILE)
    validate_probes(probes.read_bytes())
    arguments = [str(translator), str(baseline / 'SCUS_973.28.elf'),
                 '--probes', str(probes), '--out', str(output / 'generated')]
    with (output / 'generation.log').open('xb') as stream:
        subprocess.run(arguments, stdout=stream, stderr=subprocess.STDOUT, check=True)
    observed = qualify_profile(original, output / 'generated', records)
    if generated_inventory(original) != records:
        raise ValueError('Baseline source changed during observation generation')
    verify_identity(translator, translator_pin)
    verify_identity(baseline / 'SCUS_973.28.elf', metadata['elf'])
    validate_probes(probes.read_bytes())
    result = {'schema_version': 1, 'baseline': str(baseline), 'arguments': arguments,
              'translator': translator_pin, 'toolchain': toolchain, 'elf': metadata['elf'],
              'probes': identity(probes), 'generated': observed,
              'comparison': inventory_summary(observed), 'unchanged_files': len(records) - 1,
              'changed_files': [UNIT], 'exact_frontier_probe_count': 2,
              'qualification': 'Exact source insertion only; library/runtime/lifecycle/live capture unqualified'}
    write_json(output / 'observation-generation.json', result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(generate_observation(args.baseline, args.output), indent=2))


if __name__ == '__main__':
    main()
