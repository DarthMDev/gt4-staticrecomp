"""Build receipts for the exact two-probe library, separate from production."""

import argparse
from pathlib import Path
import shutil

from ee_archive import object_inventory, inventory_digest
from ee_candidate import compare_objects
from ee_observation import qualify_profile
from ee_producer import (ROOT, QUALIFIED_OBJECT_INVENTORY, existing_producer,
                         generated_inventory, identity, inventory_summary,
                         producer_path, read_json, reject_link, validate_output,
                         verify_identity, verify_toolchain, write_json)


def qualify_recipe(baseline, candidate):
    def comparable(path):
        lines = path.read_text(encoding='utf-8').splitlines()
        if sum(line.startswith('generated=') for line in lines) != 1:
            raise ValueError('Compiler receipt needs one generated-source path')
        return [line for line in lines if not line.startswith('generated=')]
    if comparable(baseline) != comparable(candidate):
        raise ValueError('Compiler recipe changed beyond the generated-source directory')


def qualify_objects(baseline, candidate, expected_d5_hash):
    changed = compare_objects(baseline, candidate, ['unit_00D5.cpp'])
    d5 = next(row for row in candidate if row['name'] == 'unit_00D5.cpp.obj')
    if d5['sha256_without_timestamp'] != expected_d5_hash:
        raise ValueError('D5 object differs from the independently compiled probe unit')
    return changed


def reviewed_sources(baseline, profile):
    baseline, metadata, study = existing_producer(baseline)
    records = generated_inventory(baseline / 'generated')
    if inventory_summary(records) != study['generated']:
        raise ValueError('Baseline source inventory changed')
    evidence = read_json(ROOT / 'docs/inputs/slice116-ee-producer-contracts.json')
    library = baseline / 'game-build/ps2game.lib'
    reject_link(library)
    verify_identity(library, evidence['fresh_library'])
    verify_toolchain(baseline / 'game-build/toolchain.txt', metadata, baseline / 'generated')
    if inventory_digest(object_inventory(library.read_bytes())) != QUALIFIED_OBJECT_INVENTORY:
        raise ValueError('Baseline object inventory changed')
    profile = producer_path(profile)
    profile_evidence = read_json(ROOT / 'docs/inputs/slice129-observation-profile.json')
    receipt = profile / 'observation-generation.json'
    reject_link(receipt)
    receipt_key = profile_evidence['generation']['path'] + '/observation-generation.json'
    if identity(receipt)['sha256'] != profile_evidence['private_artifact_sha256'][receipt_key]:
        raise ValueError('Reviewed observation generation receipt changed')
    observed = qualify_profile(baseline / 'generated', profile / 'generated', records)
    if (inventory_summary(observed) != profile_evidence['generation']['inventory'] or
            read_json(receipt)['generated'] != observed):
        raise ValueError('Observation generated sources changed')
    return baseline, profile, metadata, observed


def prepare_library(baseline, profile, output):
    baseline, profile, metadata, observed = reviewed_sources(baseline, profile)
    output = validate_output(output, Path(metadata['assets_root']))
    output.mkdir()
    generated = output / 'generated'
    generated.mkdir()
    for row in observed:
        shutil.copyfile(profile / 'generated' / row['path'], generated / row['path'])
    if generated_inventory(generated) != observed:
        raise ValueError('Copied observation source differs from reviewed profile')
    result = {'schema_version': 1, 'baseline': str(baseline), 'profile': str(profile),
              'baseline_library': identity(baseline / 'game-build/ps2game.lib'),
              'generated': observed, 'qualification': 'Prepared source only; library controls pending'}
    write_json(output / 'library-preparation.json', result)
    return result


def record_library(output):
    output = producer_path(output)
    preparation_path = output / 'library-preparation.json'
    reject_link(preparation_path)
    preparation = read_json(preparation_path)
    baseline, _, metadata, observed = reviewed_sources(Path(preparation['baseline']), Path(preparation['profile']))
    baseline_pin = identity(baseline / 'game-build/ps2game.lib')
    if preparation['baseline_library'] != baseline_pin:
        raise ValueError('Preparation cannot repin the qualified baseline library')
    if preparation['generated'] != observed or generated_inventory(output / 'generated') != observed:
        raise ValueError('Prepared source changed during compilation')
    compiler = verify_toolchain(output / 'game-build/toolchain.txt', metadata, output / 'generated')
    qualify_recipe(baseline / 'game-build/toolchain.txt', output / 'game-build/toolchain.txt')
    library = output / 'game-build/ps2game.lib'
    reject_link(library.parent)
    reject_link(library)
    if library.stat().st_size > 128 * 1024 * 1024:
        raise ValueError('Probed archive exceeds the qualified read limit')
    objects = object_inventory(library.read_bytes())
    expected_members = {row['path'] + '.obj' for row in observed if row['path'].endswith('.cpp')}
    if {row['name'] for row in objects} != expected_members:
        raise ValueError('Probed library membership differs from reviewed sources')
    original = object_inventory((baseline / 'game-build/ps2game.lib').read_bytes())
    expected_d5 = read_json(ROOT / 'docs/inputs/slice129-observation-profile.json')['unit_compile']['object_sha256_without_timestamp']
    changed = qualify_objects(original, objects, expected_d5)
    result = {'schema_version': 1, 'baseline_library': baseline_pin, 'library': identity(library),
              'generated': inventory_summary(observed), 'compiler_receipt': compiler,
              'object_count': len(objects), 'object_inventory_without_timestamps': inventory_digest(objects),
              'changed_objects': changed, 'unchanged_objects': len(objects) - len(changed),
              'qualification': 'Full library/source/object delta checked; runtime linkage and gameplay pending'}
    write_json(output / 'observation-library.json', result)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    preparation = commands.add_parser('prepare')
    preparation.add_argument('--baseline', type=Path, required=True)
    preparation.add_argument('--profile', type=Path, required=True)
    preparation.add_argument('--output', type=Path, required=True)
    recording = commands.add_parser('record-library')
    recording.add_argument('--output', type=Path, required=True)
    options = parser.parse_args()
    if options.command == 'prepare':
        prepare_library(options.baseline, options.profile, options.output)
        print('Prepared exact reviewed probe sources in a fresh library producer.')
    else:
        record_library(options.output)
        print('Qualified full archive against the exact one-unit observation delta.')
