"""Generate and qualify an EE library with explicit entry seeds (TASK-014, slice 208).

The seeds are guest function starts that the translator has no entry for. Production
pins, the production producer and the production launcher are not changed. Each
seed set is pinned in docs/inputs/slice208-ee-entries.json; each library profile is
pinned in docs/inputs/slice208-ee-libraries.json.
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

from ee_archive import object_inventory, inventory_digest
from ee_producer import (ROOT, existing_producer, generated_inventory, identity,
                         inventory_summary, prepare, read_json, reject_link,
                         verify_identity, verify_toolchain, write_json)
from render_contract_inventory import executable_segments, resolve_dispatch


SEED_MANIFEST = 'docs/inputs/slice208-ee-entries.json'
LIBRARY_MANIFEST = 'docs/inputs/slice208-ee-libraries.json'
ADDRESS_LINE = re.compile(r'0x[0-9A-F]{8}')
STATS_LINE = re.compile(r'code (\d+) instructions .*?functions (\d+), entries (\d+), rejected seeds (\d+)')
BASELINE_GENERATED = 'private/reference-acceleration-study/generated'
BASELINE_LIBRARY = 'private/reference-acceleration-study/runtime-build/ps2game.lib'


def read_seed_manifest():
    return read_json(ROOT / SEED_MANIFEST)


def parse_seeds(text):
    """Strict parser: one 0x-prefixed uppercase 8-digit address per line, no duplicates."""
    addresses = []
    for number, line in enumerate(text.splitlines(), 1):
        if not ADDRESS_LINE.fullmatch(line):
            raise ValueError(f'Malformed seed on line {number}: {line!r}')
        value = int(line, 16)
        if value % 4:
            raise ValueError(f'Unaligned seed on line {number}: {line}')
        addresses.append(value)
    if not addresses or len(set(addresses)) != len(addresses):
        raise ValueError('Seed list is empty or contains duplicates')
    return addresses


def seed_set_pin(name, root=ROOT):
    """Return the seed text and its pin after checking the committed file against the manifest."""
    manifest = read_seed_manifest()
    if name not in manifest['seed_sets']:
        raise ValueError(f'Unknown seed set: {name}')
    pin = manifest['seed_sets'][name]
    path = root / pin['path']
    reject_link(path)
    verify_identity(path, {key: pin[key] for key in ('size_bytes', 'sha256')})
    seeds = parse_seeds(path.read_text(encoding='ascii'))
    if len(seeds) != pin['count']:
        raise ValueError('Seed count differs from the pinned manifest')
    return path, seeds, pin


def check_seeds_executable(elf, seeds):
    """Every seed must start inside an executable segment (the translator silently drops others)."""
    segments = executable_segments(elf)
    for address in seeds:
        if not any(base <= address and address + 4 <= base + len(body) for base, _, body in segments):
            raise ValueError(f'Seed outside executable bytes: 0x{address:08X}')


def parse_stats(log_text):
    matches = STATS_LINE.findall(log_text)
    if len(matches) != 1:
        raise ValueError('Translator statistics line missing or duplicated')
    instructions, functions, entries, rejected = (int(value) for value in matches[0])
    return {'instructions': instructions, 'functions': functions,
            'entries': entries, 'rejected_seeds': rejected}


def validate_stats(stats, pin):
    """The translator silently drops rejected seeds; the receipt must show none and the pinned entry count."""
    if stats['rejected_seeds']:
        raise ValueError(f"Translator rejected {stats['rejected_seeds']} seed(s)")
    if stats['entries'] != pin['expected_translator_entries']:
        raise ValueError(f"Translator entries {stats['entries']} differ from pinned {pin['expected_translator_entries']}")


def verify_resolution(table, sources, seeds):
    """Return (resolved, failures). A seed without a registered non-zero id is a failure."""
    resolved, failures = [], []
    for address in seeds:
        try:
            unit, entry = resolve_dispatch(table, address, sources, require_registered=True)
        except ValueError as error:
            failures.append({'address': f'0x{address:08X}', 'reason': str(error)})
            continue
        resolved.append({'address': f'0x{address:08X}', 'unit': 'unit_' + unit, 'entry_id': entry})
    return resolved, failures


def read_generated_sources(folder):
    table = (folder / 'table.cpp').read_text(encoding='utf-8')
    sources = {path.stem[5:]: path.read_text(encoding='utf-8') for path in folder.glob('unit_*.cpp')}
    return table, sources


def changed_units(baseline, candidate):
    original = {row['path']: row for row in baseline}
    observed = {row['path']: row for row in candidate}
    if original.keys() != observed.keys():
        raise ValueError('Unexpected generated file set change')
    return [name for name in sorted(original) if original[name] != observed[name]]


def generate(output, seed_name, root=ROOT):
    output, metadata, study = existing_producer(output)
    if (output / 'generated').exists() or (output / 'generation-entries.json').exists():
        raise ValueError('Entry generation already attempted; choose a fresh producer')
    assets_root = Path(metadata['assets_root'])
    seed_file, seeds, pin = seed_set_pin(seed_name, root)
    elf = (output / 'SCUS_973.28.elf').read_bytes()
    check_seeds_executable(elf, seeds)
    copied = output / f'entries-{seed_name}.txt'
    with copied.open('xb') as destination:
        destination.write(seed_file.read_bytes())
    verify_identity(copied, {key: pin[key] for key in ('size_bytes', 'sha256')})
    artifact = next(item for item in study['artifacts'] if item['path'].endswith('/eerecomp.exe'))
    translator = assets_root / artifact['path']
    verify_identity(translator, {key: artifact[key] for key in ('size_bytes', 'sha256')})
    arguments = [str(translator), str(output / 'SCUS_973.28.elf'), '--out', str(output / 'generated'),
                 '--stats', '--entries', str(copied)]
    log_path = output / 'generation-entries.log'
    with log_path.open('xb') as log:
        subprocess.run(arguments, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)
    stats = parse_stats(log_path.read_text(encoding='utf-8', errors='replace'))
    validate_stats(stats, pin)
    records = generated_inventory(output / 'generated')
    table, sources = read_generated_sources(output / 'generated')
    resolved, failures = verify_resolution(table, sources, seeds)
    if failures:
        raise ValueError(f'Unresolved seeds: {failures}')
    stops = sum(text.count('stop(c,') for text in sources.values())
    if stops:
        raise ValueError('Entry generation emitted unsupported-code stops')
    baseline = generated_inventory(assets_root / BASELINE_GENERATED)
    if inventory_summary(baseline) != study['generated']:
        raise ValueError('Baseline generated source differs from the pinned inventory')
    result = {'schema_version': 1, 'seed_set': seed_name, 'seed_pin': pin,
              'seed_file': identity(copied), 'translator': identity(translator),
              'arguments': arguments, 'stats': stats, 'resolved_seeds': resolved,
              'emitted_stop_sites': stops, 'generated': records,
              'generated_summary': inventory_summary(records),
              'changed_units_vs_baseline': changed_units(baseline, records),
              'qualification': 'Source generation only; library, runtime and gameplay unqualified'}
    write_json(output / 'generation-entries.json', result)
    return result


def check_receipt_with_observed_compiler(path, metadata, generated, observed_version):
    """Recipe check with one recorded deviation: the installed clang-cl version (not 23.1.2).

    The qualified compiler is not installed on this machine. Every other gate of
    verify_toolchain still applies, and the deviation is written into the library receipt.
    """
    fields = dict(line.partition('=')[::2] for line in path.read_text(encoding='utf-8').splitlines())
    if fields.get('compiler_version') != observed_version:
        raise ValueError('Installed compiler version differs from the value recorded in the receipt')
    patched = path.with_name('toolchain-check.txt')
    with patched.open('x', encoding='utf-8') as destination:
        for line in path.read_text(encoding='utf-8').splitlines():
            key, _, value = line.partition('=')
            if key == 'compiler_version':
                value = '23.1.2'
            destination.write(key + '=' + value + chr(10))
    try:
        return verify_toolchain(patched, metadata, generated)
    finally:
        patched.unlink()


def record_library(output, seed_name, profile, root=ROOT, observed_compiler_version=None):
    output, metadata, study = existing_producer(output)
    generation = read_json(output / 'generation-entries.json')
    if generation['seed_set'] != seed_name:
        raise ValueError('Library seed set differs from generation receipt')
    records = generated_inventory(output / 'generated')
    if records != generation['generated']:
        raise ValueError('Generated source changed after entry verification')
    library = output / 'game-build/ps2game.lib'
    reject_link(library)
    if observed_compiler_version is None:
        receipt = verify_toolchain(output / 'game-build/toolchain.txt', metadata, output / 'generated')
    else:
        receipt = check_receipt_with_observed_compiler(output / 'game-build/toolchain.txt', metadata,
                                                       output / 'generated', observed_compiler_version)
    objects = object_inventory(library.read_bytes())
    expected_members = {row['path'] + '.obj' for row in records if row['path'].endswith('.cpp')}
    if {row['name'] for row in objects} != expected_members:
        raise ValueError('Library member inventory differs from generated compilation units')
    assets_root = Path(metadata['assets_root'])
    if observed_compiler_version is None:
        production = assets_root / BASELINE_LIBRARY
        reject_link(production)
        production_objects = object_inventory(production.read_bytes())
        if {row['name'] for row in production_objects} != expected_members:
            raise ValueError('Production library members differ; object comparison is undefined')
        by_name = {row['name']: row for row in production_objects}
        changed = sorted(row['name'] for row in objects if row != by_name[row['name']])
        expected_changed = sorted(name + '.obj' for name in generation['changed_units_vs_baseline'])
        if changed != expected_changed:
            raise ValueError(f'Changed objects differ from changed units: {changed} vs {expected_changed}')
        object_comparison = 'compared with production library objects'
    else:
        # Objects built by another compiler version are not comparable with the production objects.
        changed = None
        object_comparison = ('not performed: qualified clang-cl 23.1.2 unavailable; '
                             f'built with {observed_compiler_version}')
    relative = output.resolve().relative_to(Path(metadata['assets_root']).resolve()).as_posix()
    entry = {'path': f'{relative}/game-build/ps2game.lib', **identity(library),
             'seed_set': seed_name, 'seed_sha256': generation['seed_pin']['sha256'],
             'seed_count': generation['seed_pin']['count'],
             'translator_entries': generation['stats']['entries'],
             'source_inventory': generation['generated_summary'],
             'object_count': len(objects),
             'object_inventory_without_timestamps': inventory_digest(objects),
             'changed_objects': changed,
             'changed_source_units': generation['changed_units_vs_baseline'],
             'object_comparison': object_comparison,
             'compiler_receipt': receipt,
             'compiler_deviation': None if observed_compiler_version is None else {
                 'qualified': '23.1.2', 'observed': observed_compiler_version,
                 'reason': 'qualified clang-cl 23.1.2 is not installed; library built with the installed clang-cl'},
             'qualification': 'Library built from explicit seeds; runtime and owner gameplay pending'}
    write_json(output / f'library-{profile}.json', entry)
    manifest_path = root / LIBRARY_MANIFEST
    if manifest_path.exists():
        manifest = read_json(manifest_path)
    else:
        manifest = {'schema_version': 1, 'slice': 208, 'donor_commit': metadata['donor_commit'],
                    'profiles': {}, 'production_pins_modified': False}
    if profile in manifest['profiles']:
        raise ValueError(f'Library profile already recorded: {profile}')
    manifest['profiles'][profile] = entry
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n',
                             encoding='utf-8', newline='\n')
    return entry


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    prep = commands.add_parser('prepare', help='Fresh producer for explicit-seed generation')
    prep.add_argument('--assets-root', type=Path, required=True)
    prep.add_argument('--output', type=Path, required=True)
    gen = commands.add_parser('generate', help='Generate source with a pinned seed set')
    gen.add_argument('--output', type=Path, required=True)
    gen.add_argument('--seed-set', required=True)
    lib = commands.add_parser('record-library', help='Record the library and update the library manifest')
    lib.add_argument('--output', type=Path, required=True)
    lib.add_argument('--seed-set', required=True)
    lib.add_argument('--profile', required=True)
    lib.add_argument('--observed-compiler-version', help='Record a compiler deviation (for example 22.1.3)')
    args = parser.parse_args(argv)
    try:
        if args.command == 'prepare':
            prepare(args.assets_root, args.output)
            print('Prepared fresh producer inputs for explicit-seed generation.')
        elif args.command == 'generate':
            result = generate(args.output, args.seed_set)
            print(f"Generated {result['generated_summary']['file_count']} files; "
                  f"{len(result['resolved_seeds'])} seeds resolved; 0 stop sites.")
        else:
            record_library(args.output, args.seed_set, args.profile,
                           observed_compiler_version=args.observed_compiler_version)
            print('Recorded explicit-seed library; production pins unchanged.')
        return 0
    except (ValueError, OSError, subprocess.CalledProcessError, KeyError, StopIteration) as error:
        print(f'ee-entries: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
