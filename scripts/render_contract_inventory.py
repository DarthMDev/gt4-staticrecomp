"""Produce bounded static render-contract metadata from a verified EE producer."""

import argparse
import hashlib
from pathlib import Path
import re
import struct
import sys

from ee_producer import (existing_producer, generated_inventory, identity,
                         inventory_summary, write_json)


CANDIDATES = {
    'model_begin': 0x00455370,
    'model_end': 0x004554D0,
    'model_render_all': 0x00455528,
    'model_render_internal': 0x00455708,
    'camera_projection': 0x0044F210,
    'camera_modelview': 0x0044F280,
    'matrix_copy': 0x004A7550,
}
REGISTRY_ROW = re.compile(r'\{0x([0-9A-Fa-f]+)u,\s*(\d+)u,\s*&unit_([0-9A-Fa-f]+),\s*entries_\3\}')


def executable_segments(elf):
    if len(elf) < 52 or elf[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('Expected ELF32 little-endian input')
    header = struct.unpack_from('<HHIIIIIHHHHHH', elf, 16)
    if header[0:3] != (2, 8, 1) or header[8] != 32 or header[9] > 16:
        raise ValueError('Unexpected MIPS ELF/program header')
    table, stride, count = header[4], header[8], header[9]
    if table + stride * count > len(elf):
        raise ValueError('Truncated ELF program headers')
    segments = []
    for index in range(count):
        kind, offset, address, _, size, memory, flags, _ = struct.unpack_from('<8I', elf, table + index * stride)
        if offset + size > len(elf) or size > memory or address + memory > 0x100000000:
            raise ValueError('Invalid ELF segment bounds')
        if kind == 1 and flags & 1:
            if address % 4 or size % 4:
                raise ValueError('Unaligned executable segment')
            if any(address < old_address + len(body) and address + size > old_address
                   for old_address, _, body in segments):
                raise ValueError('Overlapping executable segments')
            segments.append((address, offset, elf[offset:offset + size]))
    if not segments:
        raise ValueError('No executable segment')
    return segments


def address_prefix(segments, address, size=64):
    matches = [(base, offset, body) for base, offset, body in segments
               if base <= address and address + size <= base + len(body)]
    if address % 4 or len(matches) != 1:
        raise ValueError('Candidate prefix is outside executable file bytes')
    base, offset, body = matches[0]
    prefix = body[address - base:address - base + size]
    return {'file_offset': offset + address - base, 'size_bytes': size,
            'sha256': hashlib.sha256(prefix).hexdigest()}


def direct_jal_sites(segments, targets):
    sites = {address: [] for address in targets}
    for base, _, body in segments:
        for offset in range(0, len(body), 4):
            word = struct.unpack_from('<I', body, offset)[0]
            if word >> 26 == 3:
                pc = base + offset
                target = ((pc + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
                if target in sites:
                    sites[target].append(pc)
    return sites


def resolve_dispatch(table, address, sources, require_registered=True):
    matches = [(int(base, 16), int(size), suffix) for base, size, suffix in REGISTRY_ROW.findall(table)
               if int(base, 16) <= address < int(base, 16) + int(size)]
    if address % 4 or len(matches) != 1:
        raise ValueError('Missing, ambiguous or unaligned generated unit entry')
    base, size, suffix = matches[0]
    source = sources[suffix]
    array = re.search(r'const std::uint16_t entries_' + suffix + r'\[(\d+)\]\s*=\s*\{([^}]+)\}', source)
    if array is None or size % 4:
        raise ValueError('Missing generated entry table')
    entries = [int(value.strip()) for value in array[2].split(',') if value.strip()]
    if len(entries) != int(array[1]) or len(entries) != size // 4:
        raise ValueError('Generated entry table size differs from registry')
    entry = entries[(address - base) // 4]
    label = f'L_{address:08X}'
    if require_registered and (not entry or not re.search(
            r'case\s+' + str(entry) + r'u:\s*goto\s+' + label + r';', source)):
        raise ValueError('Candidate is not a registered generated dispatch label')
    return suffix, entry


def inventory(output):
    output, metadata, study = existing_producer(output)
    generated = output / 'generated'
    if inventory_summary(generated_inventory(generated)) != study['generated']:
        raise ValueError('Contract inventory requires pinned generated source')
    elf = (output / 'SCUS_973.28.elf').read_bytes()
    segments = executable_segments(elf)
    table = (generated / 'table.cpp').read_text(encoding='utf-8')
    sources = {path.stem[5:]: path.read_text(encoding='utf-8') for path in generated.glob('unit_*.cpp')}
    jal = direct_jal_sites(segments, CANDIDATES.values())
    rows = []
    for name, address in CANDIDATES.items():
        suffix, entry = resolve_dispatch(table, address, sources)
        call_pattern = re.compile(r'if \(depth < kMaxDepth\) unit_' + suffix
                                  + r'\(c,\s*' + str(entry) + r'u,\s*depth \+ 1u\);')
        sites = [{'file': 'unit_' + unit + '.cpp', 'line': number}
                 for unit, source in sources.items()
                 for number, line in enumerate(source.splitlines(), 1) if call_pattern.search(line)]
        rows.append({'candidate_name': name, 'guest_address': f'0x{address:08X}',
                     'unit': f'unit_{suffix}', 'entry_id': entry,
                     'unit_source': identity(generated / f'unit_{suffix}.cpp'),
                     'prefix_only': address_prefix(segments, address),
                     'generated_direct_call_sites': sites,
                     'raw_jal_sites': [f'0x{site:08X}' for site in jal[address]],
                     'limits': 'Candidate names are research labels; not whole-function fingerprints, dynamic counts, or complete call graphs.'})
    omissions = []
    for address in (0x0044EA58, 0x0044EAC8):
        suffix, entry = resolve_dispatch(table, address, sources, require_registered=False)
        omissions.append({'guest_address': f'0x{address:08X}', 'entry_id': entry,
                          'generated_label_present': f'L_{address:08X}:' in sources[suffix],
                          'prefix_only': address_prefix(segments, address),
                          'limits': 'Decomp callback correspondence reviewed separately; live reachability remains Unknown.'})
    result = {'schema_version': 1, 'elf': metadata['elf'], 'candidates': rows,
              'known_callback_coverage_candidates': omissions,
              'classification': 'Confirmed static bytes/registry; semantic names require contract review',
              'limits': 'Executable file words may include data. Raw JAL decoding does not establish reachability. Indirect, tail and optimized calls are excluded.'}
    write_json(output / 'render-contracts.json', result)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='Existing verified private EE producer')
    args = parser.parse_args(argv)
    try:
        result = inventory(args.output)
        print(f"Recorded {len(result['candidates'])} bounded static candidates; no game execution or hooks.")
        return 0
    except (ValueError, OSError, KeyError) as error:
        print(f'render-contract-inventory: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
