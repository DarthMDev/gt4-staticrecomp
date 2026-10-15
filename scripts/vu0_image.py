"""Static VU0 microcode image of GT4 USA v2.00, and the VCALLMS entry list for gt4_vu_recomp.

Slice 160 (TASK-013). The EE uploads the VU0 microcode at startup from vaddr 0x6D3280, size 0xA80
(docs/VU0-MICROCODE-MAP.md). Unlike VU1, the bytes are stored in plain form in the pinned
reconstructed ELF at file offset 0x5D4280. The image is the 2,688-byte upload padded with zeros to
the 4 KiB VU0 code memory, so the emitter and the runtime read the same bytes.

Commands:
  build    read the upload from the pinned ELF, pad to 4096 bytes and write a fresh image directory
           (vu0_image.bin and vu0_image.json). Also writes the PT_LOAD control and the EE upload
           control (lui/addiu pairs for the vaddr, not a full routine decode)
  check    verify an image file against the pin in docs/inputs/slice160-vu0-image.json
  entries  write the VCALLMS entry list for gt4_vu_recomp --entries (one 4-digit hex address per
           line) and a LIST.json sidecar with the policy label

Entry list (docs/VU0-MICROCODE-MAP.md, the 14 entries listed there). The plan (slice 158) lists 12
addresses as VCALLMS-only and two (0x308, 0x478) as fall-through suffixes of 0x2E8 and 0x460; the
slice-160 table includes all 14 so that a VCALLMS to any listed address runs compiled. A
VCALLMS to an address outside the list is counted as interpreted_calls by the runtime.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ELF_OFFSET = 0x5D4280
ELF_VADDR = 0x6D3280
UPLOAD_BYTES = 0xA80
IMAGE_BYTES = 4096
PT_LOAD = 1
PINS = ROOT / 'docs/inputs/slice160-vu0-image.json'
ELF_PIN = ROOT / 'docs/inputs/usa-v2.00-native.json'
VCALLMS_ENTRIES = (0x058, 0x078, 0x0B8, 0x0F8, 0x2E8, 0x308, 0x460, 0x478, 0x548, 0x678, 0x700, 0x858, 0x900, 0x930)


def load_segments(elf_bytes):
    """Returns the PT_LOAD headers (offset, vaddr, filesz) of a little-endian ELF32 file."""
    if elf_bytes[:4] != b'\x7fELF' or elf_bytes[4] != 1 or elf_bytes[5] != 1:
        raise ValueError('ELF must be 32-bit little-endian')
    phoff, = struct.unpack_from('<I', elf_bytes, 0x1C)
    phentsize, phnum = struct.unpack_from('<HH', elf_bytes, 0x2A)
    segments = []
    for index in range(phnum):
        at = phoff + index * phentsize
        p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack_from('<IIIII', elf_bytes, at)
        if p_type == PT_LOAD:
            segments.append((p_offset, p_vaddr, p_filesz))
    return segments


def upload_bytes(elf_bytes):
    """Checks the PT_LOAD mapping of the upload and returns its UPLOAD_BYTES bytes."""
    for p_offset, p_vaddr, p_filesz in load_segments(elf_bytes):
        if p_offset <= ELF_OFFSET and ELF_OFFSET + UPLOAD_BYTES <= p_offset + p_filesz:
            if p_vaddr + (ELF_OFFSET - p_offset) != ELF_VADDR:
                raise ValueError('PT_LOAD does not map the VU0 upload to vaddr 0x6D3280')
            data = elf_bytes[ELF_OFFSET:ELF_OFFSET + UPLOAD_BYTES]
            if len(data) != UPLOAD_BYTES:
                raise ValueError('ELF is too short for the VU0 upload')
            return data
    raise ValueError('no PT_LOAD segment contains the VU0 upload at file offset 0x5D4280')


def pad_image(upload):
    """The upload padded with zeros to the 4 KiB VU0 code memory."""
    if len(upload) != UPLOAD_BYTES:
        raise ValueError(f'upload must be {UPLOAD_BYTES} bytes')
    return upload + bytes(IMAGE_BYTES - UPLOAD_BYTES)


def ee_upload_control(elf_bytes):
    """Counts lui/addiu pairs that build 0x006D3280 (the VU0 vaddr) and 0x00000A80 (the size).

    This is a word scan of the ELF, not a routine decode. It shows the constants exist in code;
    the caller and the upload routine are documented in docs/VU0-MICROCODE-MAP.md (Unknown here).
    """
    lui_hi_vaddr = 0x006D
    pairs = 0
    lui_words = 0
    for at in range(0, len(elf_bytes) - 8, 4):
        word, = struct.unpack_from('<I', elf_bytes, at)
        if (word >> 26) != 0x0F:  # LUI
            continue
        if word & 0xFFFF != lui_hi_vaddr:
            continue
        lui_words += 1
        rt = (word >> 16) & 0x1F
        for step in (4, 8):
            following, = struct.unpack_from('<I', elf_bytes, at + step)
            if (following >> 26) == 0x09 and ((following >> 21) & 0x1F) == rt and (following >> 16) & 0x1F == rt and following & 0xFFFF == 0x3280:
                pairs += 1
                break
    return {'lui_0x006D_words': lui_words, 'lui_addiu_0x3280_pairs': pairs}


def cmd_build(arguments):
    elf_path = Path(arguments.elf)
    elf_bytes = elf_path.read_bytes()
    elf_sha = hashlib.sha256(elf_bytes).hexdigest()
    pinned = json.loads(ELF_PIN.read_text(encoding='utf-8'))['native_elf_sha256']
    if elf_sha != pinned:
        raise ValueError(f'ELF differs from the pin in {ELF_PIN.name}: {elf_sha}')
    upload = upload_bytes(elf_bytes)
    image = pad_image(upload)
    out = Path(arguments.out)
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        raise ValueError(f'output must be a fresh or empty directory: {out}')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'vu0_image.bin').write_bytes(image)
    receipt = {
        'elf_sha256': elf_sha,
        'elf_file_offset': ELF_OFFSET,
        'elf_vaddr': ELF_VADDR,
        'upload_bytes': UPLOAD_BYTES,
        'image_bytes': IMAGE_BYTES,
        'upload_sha256': hashlib.sha256(upload).hexdigest(),
        'image_sha256': hashlib.sha256(image).hexdigest(),
        'padding': 'zeros from 0x0A80 to 0x0FFF',
        'ee_upload_control': ee_upload_control(elf_bytes),
    }
    (out / 'vu0_image.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(f"vu0-image: {IMAGE_BYTES} bytes sha256={receipt['image_sha256']} upload={UPLOAD_BYTES} bytes")
    return 0


def cmd_check(arguments):
    pins = json.loads(PINS.read_text(encoding='utf-8'))
    data = Path(arguments.image).read_bytes()
    actual = hashlib.sha256(data).hexdigest()
    if len(data) != IMAGE_BYTES or actual != pins['image']['sha256']:
        raise ValueError(f'image differs from the pin: {actual}')
    if data[UPLOAD_BYTES:] != bytes(IMAGE_BYTES - UPLOAD_BYTES):
        raise ValueError('image padding is not zero')
    print(f'vu0-image: pin verified {actual}')
    return 0


def cmd_entries(arguments):
    image = Path(arguments.image).read_bytes()
    if len(image) != IMAGE_BYTES:
        raise ValueError('image must be 4096 bytes')
    for address in VCALLMS_ENTRIES:
        if address % 8 or address >= UPLOAD_BYTES:
            raise ValueError(f'entry 0x{address:03X} is not an 8-byte address in the upload')
        if address + 8 > len(image):
            raise ValueError(f'entry 0x{address:03X} is outside the image')
    text = ''.join(f'{address:04X}\n' for address in VCALLMS_ENTRIES)
    with open(arguments.out, 'x', encoding='utf-8', newline='\n') as handle:
        handle.write(text)
    list_sha = hashlib.sha256(text.encode('ascii')).hexdigest()
    sidecar = {'policy': 'vcallms', 'entries': len(VCALLMS_ENTRIES), 'entry_list_sha256': list_sha,
               'image_sha256': hashlib.sha256(image).hexdigest(),
               'rule': 'the 14 entries of docs/VU0-MICROCODE-MAP.md (slice 160 includes the two fall-through suffixes)'}
    Path(str(arguments.out) + '.json').write_text(json.dumps(sidecar, indent=2) + '\n', encoding='utf-8')
    print(f'vu0-entries: {len(VCALLMS_ENTRIES)} vcallms entries, sha256={list_sha}')
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    build = commands.add_parser('build')
    build.add_argument('--elf', required=True)
    build.add_argument('--out', required=True, help='fresh output directory')
    build.set_defaults(run=cmd_build)
    check = commands.add_parser('check')
    check.add_argument('--image', required=True)
    check.set_defaults(run=cmd_check)
    entries = commands.add_parser('entries')
    entries.add_argument('--image', required=True)
    entries.add_argument('--out', required=True, help='new entry list file (must not exist)')
    entries.set_defaults(run=cmd_entries)
    arguments = parser.parse_args(argv)
    try:
        return arguments.run(arguments)
    except (OSError, ValueError, KeyError, struct.error) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
