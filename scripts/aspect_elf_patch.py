"""TASK-080 P2 ELF patch table and the re-translated EE library profile "aspect" (decision 0071, slice217).

Some race HUD constants are immediates in the game's code. The translator turns an immediate (`lui at,imm`,
`addiu rt,zero,imm`) into a C++ constant, and every call after the only dispatched entry (RaceDisplay::render) is a
direct jal, so no runtime hook can reach them. Each one is patched in a derivative of the reconstructed ELF into a
load of the same register from the host-filled HUD parameter block (native/display_aspect.hpp, kHudParameterBlock):

    old: lui at,0x3F40          new: lw at,0x7F00(zero)
    old: addiu t0,zero,640      new: lw t0,0x7F04(zero)

One instruction for one instruction, so the translator's analysis (functions, entries, labels) is unchanged and only
the units holding a patched word change. The host writes the original constants into the block at 16:9, so the
patched code computes exactly what the original did; at another ratio it writes the values for that ratio.

Commands:
  check --elf PATH                 verify every old word in an ELF (the pinned reconstructed ELF), print the table
  produce --assets-root A --output private/slice217-ee-aspect-producer
                                   fresh producer: the verified ELF (scripts/ee_producer.py prepare), the patched
                                   derivative SCUS_973.28.aspect.elf, translation with the pinned accepted seed set
                                   (slice208), receipt aspect-generation.json
  record-library --output <same>   after building game-build/ps2game.lib (tools/ee_producer, clang-cl 23.1.2): checks
                                   the object inventory against the slice208 library (only the patched units differ)
                                   and writes docs/inputs/slice217-ee-libraries.json (profile "aspect")
"""

import argparse
import hashlib
import json
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))

PARAMETER_BLOCK = 0x00007F00  # native/display_aspect.hpp kHudParameterBlock
LIBRARY_MANIFEST = 'docs/inputs/slice217-ee-libraries.json'
PROFILE = 'aspect'
SEED_SET = 'accepted'
BASE_PRODUCER = 'private/slice208-ee-entries-producer'  # the slice208 coverage library (same seeds, original ELF)
ELF_SHA256 = '10f82e2231a51404b95682ed3ea81171100a1af2fefdeed3391943016c7c935c'


@dataclass(frozen=True)
class Patch:
    address: int
    old_word: int
    new_word: int
    block_offset: int
    value_16_9: int
    function: str
    purpose: str


# The table. Every row is listed in docs/decisions/0071 with the same words; tests/python/test_aspect_elf_patch.py
# checks the rows against the pinned ELF and against native/display_aspect.hpp.
PATCHES = (
    Patch(0x0039F554, 0x3C013F40, 0x8C017F00, 0x00, 0x3F400000,
          'RaceDisplay::render (0x0039F498)',
          'race HUD font x scale in wide mode: lui at,0x3F40 (0.75 = (4/3)/(16/9)) -> lw at,0x7F00(zero); '
          'block value (4/3)/R, stored by func_0044E390 into the race font object D_00623838 + 0x18'),
    Patch(0x0033EF98, 0x24080280, 0x8C087F04, 0x04, 640,
          'rear-view mirror placement (code after the jal at 0x0033EF70, RacePS2Base render path)',
          'mirror horizontal divisor: addiu t0,zero,640 -> lw t0,0x7F04(zero); the mirror is 260 x width / divisor '
          'frame pixels wide and centred, block value round(640 x R / (16/9)) for R > 16:9'),
)

LUI = 0x0F
ADDIU = 0x09
LW = 0x23


def decode(word):
    return {'opcode': word >> 26, 'rs': (word >> 21) & 31, 'rt': (word >> 16) & 31, 'imm': word & 0xFFFF}


def encode_lw(rt, offset):
    if not 0 <= offset < 0x8000 or offset % 4:
        raise ValueError('lw offset(zero) must be a non-negative aligned 15-bit offset')
    return (LW << 26) | (0 << 21) | (rt << 16) | offset


def original_value(word):
    """The 32-bit value the original instruction puts in its register (lui: imm << 16; addiu rt,zero,imm: imm)."""
    fields = decode(word)
    if fields['opcode'] == LUI and fields['rs'] == 0:
        return (fields['imm'] << 16) & 0xFFFFFFFF
    if fields['opcode'] == ADDIU and fields['rs'] == 0:
        imm = fields['imm']
        return (imm - 0x10000 if imm & 0x8000 else imm) & 0xFFFFFFFF
    raise ValueError(f'unsupported original instruction 0x{word:08X}')


def check_table():
    """The table is self-consistent: new word = lw of the same register from the block; 16:9 value = original value."""
    offsets = set()
    for patch in PATCHES:
        fields = decode(patch.old_word)
        if patch.new_word != encode_lw(fields['rt'], PARAMETER_BLOCK + patch.block_offset):
            raise ValueError(f'0x{patch.address:08X}: new word is not lw rt,block+offset(zero)')
        if original_value(patch.old_word) != patch.value_16_9:
            raise ValueError(f'0x{patch.address:08X}: 16:9 block value differs from the original constant')
        if patch.block_offset in offsets:
            raise ValueError('two patches share a block word')
        offsets.add(patch.block_offset)
    return True


def load_segments(elf):
    if elf[:4] != b'\x7fELF' or elf[4] != 1 or elf[5] != 1:
        raise ValueError('not a 32-bit little-endian ELF')
    phoff, = struct.unpack_from('<I', elf, 0x1C)
    phentsize, phnum = struct.unpack_from('<HH', elf, 0x2A)
    segments = []
    for index in range(phnum):
        kind, offset, vaddr, _, filesz, _, _, _ = struct.unpack_from('<8I', elf, phoff + index * phentsize)
        if kind == 1:
            segments.append((vaddr, offset, filesz))
    return segments


def file_offset(elf, address):
    for vaddr, offset, filesz in load_segments(elf):
        if vaddr <= address and address + 4 <= vaddr + filesz:
            return offset + address - vaddr
    raise ValueError(f'0x{address:08X} is not in a loaded segment')


def word_at(elf, address):
    return struct.unpack_from('<I', elf, file_offset(elf, address))[0]


def verify_old_words(elf):
    check_table()
    for patch in PATCHES:
        observed = word_at(elf, patch.address)
        if observed != patch.old_word:
            raise ValueError(f'0x{patch.address:08X}: ELF word 0x{observed:08X}, table old word 0x{patch.old_word:08X}')
    return True


def patch_elf(elf):
    """The patched derivative; refuses unless every old word matches."""
    verify_old_words(elf)
    patched = bytearray(elf)
    for patch in PATCHES:
        struct.pack_into('<I', patched, file_offset(elf, patch.address), patch.new_word)
    return bytes(patched)


def table_rows():
    return [{'address': f'0x{p.address:08X}', 'old_word': f'0x{p.old_word:08X}', 'new_word': f'0x{p.new_word:08X}',
             'block_word': f'0x{PARAMETER_BLOCK + p.block_offset:08X}', 'value_16_9': f'0x{p.value_16_9:08X}',
             'function': p.function, 'purpose': p.purpose} for p in PATCHES]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def produce(assets_root, output):
    import ee_entries
    import ee_producer
    ee_producer.prepare(assets_root, output)
    output, metadata, study = ee_producer.existing_producer(output)
    original = (output / 'SCUS_973.28.elf').read_bytes()
    if sha256(original) != ELF_SHA256:
        raise ValueError('reconstructed ELF differs from the pinned ELF')
    patched = patch_elf(original)
    patched_path = output / 'SCUS_973.28.aspect.elf'
    with patched_path.open('xb') as destination:
        destination.write(patched)
    seed_file, seeds, pin = ee_entries.seed_set_pin(SEED_SET)
    ee_entries.check_seeds_executable(patched, seeds)
    copied = output / f'entries-{SEED_SET}.txt'
    with copied.open('xb') as destination:
        destination.write(seed_file.read_bytes())
    artifact = next(item for item in study['artifacts'] if item['path'].endswith('/eerecomp.exe'))
    translator = Path(metadata['assets_root']) / artifact['path']
    ee_producer.verify_identity(translator, {key: artifact[key] for key in ('size_bytes', 'sha256')})
    arguments = [str(translator), str(patched_path), '--out', str(output / 'generated'), '--stats', '--entries',
                 str(copied)]
    log_path = output / 'generation-aspect.log'
    with log_path.open('xb') as log:
        subprocess.run(arguments, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
    stats = ee_entries.parse_stats(log_path.read_text(encoding='utf-8', errors='replace'))
    ee_entries.validate_stats(stats, pin)
    records = ee_producer.generated_inventory(output / 'generated')
    table, sources = ee_entries.read_generated_sources(output / 'generated')
    resolved, failures = ee_entries.verify_resolution(table, sources, seeds)
    if failures:
        raise ValueError(f'Unresolved seeds: {failures}')
    if sum(text.count('stop(c,') for text in sources.values()):
        raise ValueError('generation emitted unsupported-code stops')
    base = Path(metadata['assets_root']) / BASE_PRODUCER
    base_generation = ee_producer.read_json(base / 'generation-entries.json')
    if ee_producer.generated_inventory(base / 'generated') != base_generation['generated']:
        raise ValueError('slice208 generated source differs from its receipt')
    changed = ee_entries.changed_units(base_generation['generated'], records)
    expected = sorted({unit_holding(sources, patch.address) for patch in PATCHES})
    if changed != expected:
        raise ValueError(f'changed units {changed} differ from the units holding the patched words {expected}')
    result = {'schema_version': 1, 'task': 'TASK-080 P2', 'slice': 217,
              'elf': {'path': 'SCUS_973.28.elf', 'sha256': sha256(original)},
              'patched_elf': {'path': patched_path.name, 'sha256': sha256(patched), 'size_bytes': len(patched)},
              'parameter_block': f'0x{PARAMETER_BLOCK:08X}', 'patches': table_rows(),
              'seed_set': SEED_SET, 'seed_pin': pin, 'translator': ee_producer.identity(translator),
              'arguments': arguments, 'stats': stats, 'resolved_seed_count': len(resolved),
              'generated': records, 'generated_summary': ee_producer.inventory_summary(records),
              'changed_units_vs_slice208': changed,
              'qualification': 'Source generation only; library, runtime and gameplay unqualified'}
    ee_producer.write_json(output / 'aspect-generation.json', result)
    return result


def unit_holding(sources, address):
    label = f'L_{address:08X}:'
    owners = [f'unit_{name}.cpp' for name, text in sources.items() if label in text]
    if len(owners) == 1:
        return owners[0]
    # The patched word may sit inside a block without its own label: the unit whose code mentions the next labels.
    for delta in range(4, 0x400, 4):
        label = f'L_{address - delta:08X}:'
        owners = [f'unit_{name}.cpp' for name, text in sources.items() if label in text]
        if len(owners) == 1:
            return owners[0]
    raise ValueError(f'no unit holds 0x{address:08X}')


def record_library(output):
    import ee_archive
    import ee_producer
    output, metadata, _ = ee_producer.existing_producer(output)
    generation = ee_producer.read_json(output / 'aspect-generation.json')
    records = ee_producer.generated_inventory(output / 'generated')
    if records != generation['generated']:
        raise ValueError('generated source changed after the generation receipt')
    library = output / 'game-build/ps2game.lib'
    ee_producer.reject_link(library)
    receipt = ee_producer.verify_toolchain(output / 'game-build/toolchain.txt', metadata, output / 'generated')
    objects = ee_archive.object_inventory(library.read_bytes())
    base_library = Path(metadata['assets_root']) / BASE_PRODUCER / 'game-build/ps2game.lib'
    base_objects = {row['name']: row for row in ee_archive.object_inventory(base_library.read_bytes())}
    if {row['name'] for row in objects} != set(base_objects):
        raise ValueError('library members differ from the slice208 library')
    changed = sorted(row['name'] for row in objects if row != base_objects[row['name']])
    expected = sorted(name + '.obj' for name in generation['changed_units_vs_slice208'])
    if changed != expected:
        raise ValueError(f'changed objects {changed} differ from the changed units {expected}')
    relative = output.resolve().relative_to(Path(metadata['assets_root']).resolve()).as_posix()
    entry = {'path': f'{relative}/game-build/ps2game.lib', **ee_producer.identity(library),
             'patched_elf_sha256': generation['patched_elf']['sha256'], 'parameter_block': generation['parameter_block'],
             'patches': generation['patches'], 'seed_set': generation['seed_set'],
             'seed_sha256': generation['seed_pin']['sha256'], 'translator_entries': generation['stats']['entries'],
             'source_inventory': generation['generated_summary'], 'object_count': len(objects),
             'object_inventory_without_timestamps': ee_archive.inventory_digest(objects),
             'changed_objects_vs_slice208': changed, 'compiler_receipt': receipt,
             'base_library': f'{BASE_PRODUCER}/game-build/ps2game.lib',
             'qualification': 'slice208 coverage library with the TASK-080 P2 ELF patch table; owner gameplay pending'}
    manifest = {'schema_version': 1, 'slice': 217, 'donor_commit': metadata['donor_commit'],
                'profiles': {PROFILE: entry}, 'production_pins_modified': False}
    path = ROOT / LIBRARY_MANIFEST
    with path.open('x', encoding='utf-8', newline='\n') as destination:
        destination.write(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
    return entry


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    check = commands.add_parser('check')
    check.add_argument('--elf', type=Path, required=True)
    prod = commands.add_parser('produce')
    prod.add_argument('--assets-root', type=Path, required=True)
    prod.add_argument('--output', type=Path, required=True)
    rec = commands.add_parser('record-library')
    rec.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == 'check':
            verify_old_words(args.elf.read_bytes())
            print(json.dumps(table_rows(), indent=2))
        elif args.command == 'produce':
            result = produce(args.assets_root, args.output)
            print(f"Generated {result['generated_summary']['file_count']} files; changed units "
                  f"{result['changed_units_vs_slice208']}; patched ELF {result['patched_elf']['sha256']}")
        else:
            entry = record_library(args.output)
            print(f"Recorded library {entry['sha256']}; changed objects {entry['changed_objects_vs_slice208']}")
        return 0
    except (ValueError, OSError, subprocess.CalledProcessError, KeyError, StopIteration) as error:
        print(f'aspect-elf-patch: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
