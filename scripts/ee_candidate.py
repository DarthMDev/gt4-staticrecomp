"""Prepare and qualify isolated EE libraries without changing production pins."""

import argparse
import json
from pathlib import Path
import shutil
import sys

from ee_archive import object_inventory, inventory_digest
from ee_producer import (ROOT, QUALIFIED_OBJECT_INVENTORY, existing_producer,
                         generated_inventory, inventory_summary, identity,
                         prepare, read_json, reject_link, verify_identity,
                         verify_toolchain, write_json)
from ee_entry_coverage import checked_spans, inspect_generated, qualify_profiles
from prepare_external_vulkan import prepare as prepare_port


def reviewed_sources(baseline, coverage):
    baseline, baseline_metadata, study = existing_producer(baseline)
    coverage, coverage_metadata, _ = existing_producer(coverage)
    if baseline_metadata['assets_root'] != coverage_metadata['assets_root']:
        raise ValueError('Candidate producers use different assets checkouts')
    baseline_evidence = read_json(ROOT / 'docs/inputs/slice116-ee-producer-contracts.json')
    coverage_evidence = read_json(ROOT / 'docs/inputs/slice117-ee-entry-coverage.json')
    baseline_library = baseline / 'game-build/ps2game.lib'
    reject_link(baseline_library)
    verify_identity(baseline_library, baseline_evidence['fresh_library'])
    if inventory_digest(object_inventory(baseline_library.read_bytes())) != QUALIFIED_OBJECT_INVENTORY:
        raise ValueError('Rebuilt baseline object inventory changed')
    records = generated_inventory(baseline / 'generated')
    if inventory_summary(records) != study['generated']:
        raise ValueError('Baseline source inventory changed')
    verify_toolchain(baseline / 'game-build/toolchain.txt', baseline_metadata, baseline / 'generated')
    report = read_json(coverage / 'entry-coverage.json')
    if checked_spans((coverage / 'SCUS_973.28.elf').read_bytes()) != coverage_evidence['spans']:
        raise ValueError('Reviewed callback spans changed')
    analysis, controls = {}, {}
    for name, row in report['profiles'].items():
        source = coverage / name / 'generated'
        actual = generated_inventory(source)
        if inventory_summary(actual) != coverage_evidence['profiles'][name]['generated']:
            raise ValueError('Reviewed coverage profile source changed')
        analysis[name] = row['analysis']
        if analysis[name] != coverage_evidence['profiles'][name]['analysis']:
            raise ValueError('Reviewed analysis receipt changed')
        controls[name] = inspect_generated(source)
    qualify_profiles(analysis, controls)
    return baseline, coverage, baseline_metadata, coverage_evidence


def prepare_candidates(assets_root, output, baseline, coverage):
    baseline, coverage, metadata, evidence = reviewed_sources(baseline, coverage)
    assets_root = assets_root.resolve()
    if assets_root != Path(metadata['assets_root']).resolve():
        raise ValueError('Assets checkout differs from the qualified producers')
    prepare(assets_root, output)
    output, _, _ = existing_producer(output)
    baseline_folder = output / 'baseline'
    baseline_folder.mkdir()
    shutil.copyfile(baseline / 'game-build/ps2game.lib', baseline_folder / 'ps2game.lib')
    verify_identity(baseline_folder / 'ps2game.lib', identity(baseline / 'game-build/ps2game.lib'))
    generated = output / 'recovered/generated'
    generated.mkdir(parents=True)
    source = coverage / 'supplier_seed/generated'
    for path in source.iterdir():
        shutil.copyfile(path, generated / path.name)
    if inventory_summary(generated_inventory(generated)) != evidence['profiles']['supplier_seed']['generated']:
        raise ValueError('Copied recovered source differs from the reviewed profile')
    prepare_port(output / 'port', assets_root)
    native = read_json(ROOT / 'docs/inputs/slice114-native-base.json')
    shaders = output / 'shaders'
    shaders.mkdir()
    for pin in native['runtime_dependencies']:
        original = assets_root / pin['path']
        reject_link(original)
        verify_identity(original, {key: pin[key] for key in ('size_bytes', 'sha256')})
        destination = shaders / original.name
        shutil.copyfile(original, destination)
        verify_identity(destination, {key: pin[key] for key in ('size_bytes', 'sha256')})
    result = {'schema_version': 1, 'baseline_producer': str(baseline),
              'coverage_producer': str(coverage),
              'baseline_library': identity(baseline_folder / 'ps2game.lib'),
              'recovered_generated': evidence['profiles']['supplier_seed']['generated'],
              'recovered_seeds': ['0x0044EAC8'],
              'changed_files': evidence['profiles']['supplier_seed']['changed_files'],
              'qualification': 'Prepared only; libraries/runtime/gameplay not yet qualified'}
    write_json(output / 'candidate-preparation.json', result)
    return result


def compare_objects(baseline, candidate, changed_sources):
    original = {row['name']: row for row in baseline}
    observed = {row['name']: row for row in candidate}
    if original.keys() != observed.keys():
        raise ValueError('Candidate archive membership differs from baseline')
    changed = {name for name in original if original[name] != observed[name]}
    expected = {name + '.obj' for name in changed_sources}
    if changed != expected:
        raise ValueError('Candidate object differences exceed or omit reviewed changed units')
    return sorted(changed)


def record_libraries(output):
    output, metadata, _ = existing_producer(output)
    preparation = read_json(output / 'candidate-preparation.json')
    baseline, coverage, _, evidence = reviewed_sources(
        Path(preparation['baseline_producer']), Path(preparation['coverage_producer']))
    baseline_pin = identity(baseline / 'game-build/ps2game.lib')
    if preparation['baseline_library'] != baseline_pin:
        raise ValueError('Candidate preparation baseline pin differs from qualified producer')
    verify_identity(output / 'baseline/ps2game.lib', baseline_pin)
    generated = output / 'recovered/generated'
    if inventory_summary(generated_inventory(generated)) != evidence['profiles']['supplier_seed']['generated']:
        raise ValueError('Recovered sources changed during library compilation')
    verify_toolchain(output / 'recovered/game-build/toolchain.txt', metadata, generated)
    library = output / 'recovered/game-build/ps2game.lib'
    reject_link(library)
    objects = object_inventory(library.read_bytes())
    original = object_inventory((baseline / 'game-build/ps2game.lib').read_bytes())
    changed = compare_objects(original, objects, evidence['profiles']['supplier_seed']['changed_files'])
    relative = output.relative_to(ROOT).as_posix()
    result = {'schema_version': 1, 'slice': 118, 'donor_commit': metadata['donor_commit'],
              'profiles': {
                  'baseline': {'path': relative + '/baseline/ps2game.lib', **baseline_pin,
                               'source_inventory': read_json(ROOT / 'docs/inputs/slice116-ee-producer-contracts.json')['generated'],
                               'object_inventory_without_timestamps': QUALIFIED_OBJECT_INVENTORY},
                  'recovered': {'path': relative + '/recovered/game-build/ps2game.lib', **identity(library),
                                'source_inventory': evidence['profiles']['supplier_seed']['generated'],
                                'object_inventory_without_timestamps': inventory_digest(objects)}},
              'object_count': len(objects), 'changed_objects': changed,
              'unchanged_objects': len(objects) - len(changed),
              'port_directory': relative + '/port', 'shader_directory': relative + '/shaders',
              'recovered_seeds': ['0x0044EAC8'],
              'qualification': 'Library/source/object delta qualified; runtime and owner gameplay pending',
              'production_pins_modified': False}
    write_json(output / 'candidate-libraries.json', result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    preparation = commands.add_parser('prepare')
    preparation.add_argument('--assets-root', type=Path, required=True)
    preparation.add_argument('--output', type=Path, required=True)
    preparation.add_argument('--baseline-producer', type=Path, default=ROOT / 'private/slice116-ee-producer')
    preparation.add_argument('--coverage-producer', type=Path, default=ROOT / 'private/slice117-ee-entry-audit')
    libraries = commands.add_parser('record-libraries')
    libraries.add_argument('--output', type=Path, required=True)
    options = parser.parse_args()
    try:
        if options.command == 'prepare':
            prepare_candidates(options.assets_root, options.output, options.baseline_producer, options.coverage_producer)
            print('Prepared fresh isolated baseline and reviewed recovered-source candidate.')
        else:
            record_libraries(options.output)
            print('Qualified full candidate archive against reviewed baseline object/source delta.')
        return 0
    except (OSError, ValueError, KeyError) as error:
        print(f'ee-candidate: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
