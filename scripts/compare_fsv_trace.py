"""Compare FSV read trace lines with the same sectors read from a GT4 ISO.

Input: a log from a run with GT4_FSV_TRACE=1 (iop/cdvdfsv.cpp read_to_ee, slice 181), one line per guest read:
  [fsv] read lsn=<LSN> sectors=<count> dst=0x<EE address> bytes=<delivered length> crc32=<CRC-32 hex>
For each read, the same byte range is read from the ISO at LSN * 2048 (zero-filled past the end, as the
runtime's disc::read does) and its CRC-32 is compared with the logged one.

Names: ISO9660 files are mapped with pycdlib. When the ISO matches the pinned disc, sectors inside GT4.VOL are
also mapped to RoFS catalogue paths (gt4_volume.verified_volume; the catalogue is reviewed in slice119).

Exit status: 0 when the trace parsed; 1 when the trace has no [fsv] lines or the ISO cannot be read.
A CRC mismatch is reported in the summary and does not change the exit status.
"""

import argparse
import json
import posixpath
from pathlib import Path
import re
import sys
import zlib

import pycdlib

SECTOR = 2048
LINE = re.compile(r'\[fsv\] read lsn=(\d+) sectors=(\d+) dst=0x([0-9A-Fa-f]{8}) bytes=(\d+) crc32=([0-9A-Fa-f]{8})\s*$')


def parse_trace(lines):
    """Returns the logged reads in order; other lines (the rest of the game log) are ignored."""
    reads = []
    for number, line in enumerate(lines, 1):
        match = LINE.search(line)
        if not match:
            continue
        lsn, sectors, dst, length, crc = match.groups()
        reads.append({'line': number, 'lsn': int(lsn), 'sectors': int(sectors), 'dst': int(dst, 16),
                      'bytes': int(length), 'crc32': int(crc, 16)})
    return reads


def read_iso_range(stream, offset, length):
    """Bytes from the ISO, zero-filled past the end (the runtime's disc::read behaviour)."""
    stream.seek(offset)
    data = stream.read(length)
    return data + bytes(length - len(data))


def iso9660_extents(iso_path):
    """(first LSN, end LSN exclusive, ISO path) for every ISO9660 file with data."""
    iso = pycdlib.PyCdlib()
    iso.open(str(iso_path))
    extents = []
    try:
        for dirpath, _dirnames, filenames in iso.walk(iso_path='/'):
            for name in filenames:
                path = posixpath.join(dirpath, name)
                record = iso.get_record(iso_path=path)
                start = record.extent_location()
                length = record.data_length
                if length:
                    extents.append((start, start + (length + SECTOR - 1) // SECTOR, path))
    finally:
        iso.close()
    return extents


def volume_extents(iso_path):
    """RoFS catalogue extents inside GT4.VOL, or [] when the ISO is not the pinned disc."""
    import gt4_volume
    try:
        with gt4_volume.verified_volume(iso_path) as stream:
            catalogue = gt4_volume.read_catalogue(stream)
    except ValueError as error:
        print(f'note: GT4.VOL catalogue not mapped: {error}', file=sys.stderr)
        return []
    extents = []
    for name, record in catalogue.files.items():
        if not record.stored_size:
            continue
        start_byte = gt4_volume.VOLUME_OFFSET + record.offset
        start = start_byte // SECTOR
        end = (start_byte + record.stored_size + SECTOR - 1) // SECTOR
        extents.append((start, end, 'GT4.VOL:' + name))
    return extents


def names_for(lsn, sectors, extents):
    end = lsn + sectors
    return [name for start, stop, name in extents if start < end and lsn < stop]


def compare(reads, stream, extents=()):
    """Adds the ISO CRC-32 and a match flag to each read; returns the list of result dicts."""
    results = []
    for read in reads:
        data = read_iso_range(stream, read['lsn'] * SECTOR, read['bytes'])
        iso_crc = zlib.crc32(data) & 0xFFFFFFFF
        results.append(dict(read, iso_crc32=iso_crc, match=iso_crc == read['crc32'],
                            names=names_for(read['lsn'], read['sectors'], extents)))
    return results


def format_table(results):
    rows = ['line  lsn      sectors  dst         bytes   trace_crc  iso_crc    match  names']
    for r in results:
        rows.append('%-5d %-8d %-8d 0x%08X  %-7d %08X   %08X   %-5s  %s' % (
            r['line'], r['lsn'], r['sectors'], r['dst'], r['bytes'], r['crc32'], r['iso_crc32'],
            'yes' if r['match'] else 'NO', ', '.join(r['names']) or '(unmapped)'))
    mismatches = sum(1 for r in results if not r['match'])
    rows.append('reads: %d  matches: %d  mismatches: %d' % (len(results), len(results) - mismatches, mismatches))
    return '\n'.join(rows)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('trace', type=Path, help='game log containing [fsv] lines')
    parser.add_argument('iso', type=Path, help='GT4 ISO (read only)')
    parser.add_argument('--json', action='store_true', help='print JSON instead of the table')
    args = parser.parse_args(argv)
    try:
        reads = parse_trace(args.trace.read_text(encoding='utf-8', errors='replace').splitlines())
    except OSError as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1
    if not reads:
        print('FAIL: no [fsv] read lines in the trace (was GT4_FSV_TRACE=1 set?)', file=sys.stderr)
        return 1
    try:
        extents = iso9660_extents(args.iso) + volume_extents(args.iso)
        with args.iso.open('rb') as stream:
            results = compare(reads, stream, extents)
    except (OSError, pycdlib.pycdlibexception.PyCdlibException) as error:
        print(f'FAIL: cannot read ISO: {error}', file=sys.stderr)
        return 1
    print(json.dumps(results, indent=2) if args.json else format_table(results))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
