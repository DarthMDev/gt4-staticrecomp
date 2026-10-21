"""TASK-080 P2 ELF patch table and the re-translated EE library profile "aspect" (decision 0071, slice217), and the
TASK-086 step 1 progressive rows (decision 0072, slice232, profile "progressive").

Some race HUD constants are immediates in the game's code. The translator turns an immediate (`lui at,imm`,
`addiu rt,zero,imm`) into a C++ constant, and every call after the only dispatched entry (RaceDisplay::render) is a
direct jal, so no runtime hook can reach them. Each one is patched in a derivative of the reconstructed ELF into a
load of the same register from the host-filled HUD parameter block (native/display_aspect.hpp, kHudParameterBlock):

    old: lui at,0x3F40          new: lw at,0x7F00(zero)
    old: addiu t0,zero,640      new: lw t0,0x7F04(zero)

One instruction for one instruction, so the translator's analysis (functions, entries, labels) is unchanged and only
the units holding a patched word change. The host writes the original constants into the block at 16:9, so the
patched code computes exactly what the original did; at another ratio it writes the values for that ratio.

TASK-086 step 1 uses the same mechanism for the video mode of menus and races: the constants that make
`MOption.applyMenuDisplayMode` ask for 480i (and the race "normal" index path, which asks for the same tuple) become
loads from the progressive display words 0x7F08..0x7F17 (native/progressive_display.hpp), as do the two field
halvings of the UI backdrop/capture passes. The host fills them from GT4_PROGRESSIVE: the progressive values when on,
the original constants when off (the original behaviour, byte for byte).

Commands (`--profile aspect` is the default; `--profile progressive` = PATCHES + PROGRESSIVE_PATCHES):
  check --elf PATH                 verify every old word in an ELF (the pinned reconstructed ELF), print the table
  produce --assets-root A --output private/slice217-ee-aspect-producer
                                   fresh producer: the verified ELF (scripts/ee_producer.py prepare), the patched
                                   derivative SCUS_973.28.<profile>.elf, translation with the pinned accepted seed set
                                   (slice208), receipt <profile>-generation.json
  record-library --output <same>   after building game-build/ps2game.lib (tools/ee_producer, clang-cl 23.1.2): checks
                                   the object inventory against the slice208 library (only the patched units differ)
                                   and writes the profile's pin (docs/inputs/slice217-ee-libraries.json for "aspect",
                                   docs/inputs/slice232-ee-libraries.json for "progressive")
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
    # The block value that reproduces the original instruction: at 16:9 for the aspect rows, with GT4_PROGRESSIVE=0
    # for the progressive rows.
    value_16_9: int
    function: str
    purpose: str
    # TASK-086: the block value with GT4_PROGRESSIVE=1 (None for the aspect rows).
    value_progressive: int | None = None


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

# TASK-086 step 1 (slice232; decision 0072): progressive 480p menus and races, switchable at run time. Every row is one
# `lw` of the same register from the progressive display words, which the host fills from GT4_PROGRESSIVE (on: the
# progressive value; off: the original constant). docs/decisions/0072 lists the same words;
# tests/python/test_progressive_display.py checks them against the pinned ELF and native/progressive_display.hpp.
PROGRESSIVE_BLOCK = 0x00007F08  # native/progressive_display.hpp kProgressiveBlock
PROGRESSIVE_PATCHES = (
    Patch(0x00436750, 0x24050001, 0x8C057F08, 0x08, 1,
          'MOption.applyMenuDisplayMode (func_00436740)',
          'menu frame buffer format: addiu a1,zero,1 -> lw a1,0x7F08(zero); func_001074D0 psm 1 (CT24) off, '
          '2 (CT16, as the 480p Try pop-up) on', 2),
    Patch(0x00436754, 0x0000302D, 0x8C067F0C, 0x0C, 0,
          'MOption.applyMenuDisplayMode (func_00436740)',
          'menu CRT row: daddu a2,zero,zero -> lw a2,0x7F0C(zero); row 0 (default row 1, 480i field) off, '
          'row 6 (480p, 640x448 frames) on', 6),
    Patch(0x00436700, 0x24050001, 0x8C057F08, 0x08, 1,
          'MOption.applyRaceDisplayMode (func_00436640), race display index 0 ("normal")',
          'race psm for index 0: addiu a1,zero,1 -> lw a1,0x7F08(zero); with 0x00436708 the normal index requests '
          'exactly what the menus request, so a menu-race switch is no CRT change', 2),
    Patch(0x00436708, 0x0000302D, 0x8C067F0C, 0x0C, 0,
          'MOption.applyRaceDisplayMode (func_00436640), shared by index 0 and indexes above 2',
          'race CRT row: daddu a2,zero,zero -> lw a2,0x7F0C(zero); row 0 off, row 6 (480p) on', 6),
    Patch(0x00436680, 0x24050001, 0x8C057F08, 0x08, 1,
          'MOption.applyRaceDisplayMode (func_00436640), indexes above 2 (branch delay slot)',
          'psm of the unknown-index path: addiu a1,zero,1 -> lw a1,0x7F08(zero); keeps that path equal to index 0',
          2),
    Patch(0x00218240, 0x3C013F00, 0x8C017F10, 0x10, 0x3F000000,
          'func_00218170 (mColorWindow__virtual_92 backdrop pass)',
          'field halving of the window rectangle: lui at,0x3F00 (0.5) -> lw at,0x7F10(zero); 0.5 off (224-line '
          'field buffers), 1.0 on (448-line frames, viewport type 0 draws UI units 1:1)', 0x3F800000),
    Patch(0x00217EA0, 0x3C013F00, 0x8C017F10, 0x10, 0x3F000000,
          'mRenderContextPS2__virtual_67 (0x00217D68), capture rectangle',
          'field halving of the captured rectangle (when self+0x1D9C == 0): lui at,0x3F00 -> lw at,0x7F10(zero); '
          '0.5 off, 1.0 on', 0x3F800000),
    Patch(0x00217EF8, 0x24040001, 0x8C047F14, 0x14, 1,
          'mRenderContextPS2__virtual_67 (0x00217D68), draw-back viewport',
          'viewport type of the draw back into the frame (func_004A2A10 argument, branch delay slot): addiu '
          'a0,zero,1 -> lw a0,0x7F14(zero); 1 (half-height field viewport) off, 0 (full frame) on', 0),
    Patch(0x003DA488, 0x24050001, 0x8C057F08, 0x08, 1,
          'race monitor state change (code after 0x003DA468, RaceMonitor area)',
          'psm of its 480i field request func_001074D0(state, 1, 1, 640, 448): addiu a1,zero,1 -> lw a1,0x7F08(zero)',
          2),
    Patch(0x003DA490, 0x24060001, 0x8C067F1C, 0x1C, 1,
          'race monitor state change (code after 0x003DA468, RaceMonitor area)',
          'its explicit CRT row 1 (480i field): addiu a2,zero,1 -> lw a2,0x7F1C(zero); row 1 off, row 6 on (its row-2 '
          'request at 0x003DA4EC stays for step 2)', 6),
)


@dataclass(frozen=True)
class Rewrite:
    """A word of a small, reviewed code change that is not a constant load (TASK-086 boot path). The off behaviour of
    each group is argued in docs/decisions/0072 and checked by the headless off runs (frames byte-identical to
    slice218)."""
    address: int
    old_word: int
    new_word: int
    function: str
    purpose: str
    block_offset: int | None = None  # the block word the new instruction reads, if any


# TASK-086 step 1 boot path (decision 0072). The menus after boot keep the boot mode until the first scene, so the boot
# itself must build the 480p layout when progressive is on. func_001062E8 (state init, called once from func_00107730
# with a1 = 1) tail-jumps to func_001063A8 (480i field layout + CRT row 1) when a1 != 0; its a1 == 0 path (a call of
# func_00106710, a 512-wide field layout) is never taken from that caller. The rewrite turns the branch condition into
# the block word 0x7F18 (0 = off and also "not filled yet": the original path; 1 = on) and the unused path's call into
# the game's own 480p initialiser func_00106DA8(state, 2, 0x50, -1, -1) (the debug menu's 480p mode: 640x448 CT16
# frames, CRT interlace 0 / mode 0x50 / ffmd 1). Its arguments are set on the shared path, where func_001063A8 ignores
# them: a2 = 0x50 in place of an mtc1 whose only use was a store of the 1.0f bits, a3 = -1 in place of v1 = -1 (the
# store of -1 to state+0x30 now uses a3). The call keeps its return point 0x00106368, so the translator's entries are
# unchanged. func_00107788, called right after, set the CRT to the constants (1, 2, 1) that func_001063A8 had just set
# (an equal call, so func_001060F8 returned early); it now re-applies the state's own values: the same call off, and
# the 480p mode kept on.
BOOT_REWRITES = (
    Rewrite(0x001062FC, 0x00A0802D, 0x8C107F18, 'func_001062E8 (display state init)',
            'daddu s0,a1,zero -> lw s0,0x7F18(zero): the layout choice comes from the block word (0 field layout, the '
            'original path; 1 the 480p path); the original a1 is always 1 here and s0 only feeds the branch', 0x18),
    Rewrite(0x0010631C, 0x44810000, 0x24060050, 'func_001062E8 (display state init)',
            'mtc1 at,f0 -> addiu a2,zero,0x50: a2 = 0x50 for the 480p path (func_001063A8 ignores a2); f0 is not '
            'needed because 0x00106330 stores the 1.0f bits from at directly'),
    Rewrite(0x00106324, 0x2403FFFF, 0x2407FFFF, 'func_001062E8 (display state init)',
            'addiu v1,zero,-1 -> addiu a3,zero,-1: width -1 (= 640) for the 480p path; func_001063A8 ignores a3'),
    Rewrite(0x0010632C, 0xAE230030, 0xAE270030, 'func_001062E8 (display state init)',
            'sw v1,48(s1) -> sw a3,48(s1): the same -1 stored to state+0x30'),
    Rewrite(0x00106330, 0xE6200014, 0xAE210014, 'func_001062E8 (display state init)',
            'swc1 f0,20(s1) -> sw at,20(s1): the same 1.0f bits (0x3F800000) stored to state+0x14'),
    Rewrite(0x00106344, 0x12000006, 0x16000006, 'func_001062E8 (display state init)',
            'beq s0,zero,0x00106360 -> bne s0,zero,0x00106360: the 480p path when the block word is 1'),
    Rewrite(0x00106360, 0x0C0419C4, 0x0C041B6A, 'func_001062E8, unused a1 == 0 path',
            'jal 0x00106710 -> jal 0x00106DA8: the 480p initialiser of the game (a0 = state from 0x00106310)'),
    Rewrite(0x00106364, 0x0220202D, 0x24050002, 'func_001062E8, unused a1 == 0 path',
            'daddu a0,s1,zero -> addiu a1,zero,2 (delay slot): psm 2 (CT16); a0 already holds the state'),
    Rewrite(0x001077A4, 0x24050001, 0x8E050000, 'func_00107788 (boot CRT set)',
            'addiu a1,zero,1 -> lw a1,0(s0): interlace from the state (s0 = 0x006184D0); 1 off, 0 on'),
    Rewrite(0x001077A8, 0x24070001, 0x8E070008, 'func_00107788 (boot CRT set)',
            'addiu a3,zero,1 -> lw a3,8(s0): ffmd from the state; 1 both'),
    Rewrite(0x001077B4, 0x24060002, 0x8E060004, 'func_00107788 (boot CRT set)',
            'addiu a2,zero,2 -> lw a2,4(s0): mode from the state; 2 off, 0x50 on'),
    # func_00100A40 applies the display mode object at 0x006D6708 (mode 1-5, set by func_00100BB8; a call with mode 2
    # comes during boot and from func_003D8A60). Mode 2 is the 640 field layout through func_001063A8, which would undo
    # the 480p boot. Its case (6 words, the last one alignment padding) now reads the boot layout word and goes to the
    # mode 4 case (func_00106DA8(state, 2, 0x50, -1, -1)) when it is 1; with 0 it makes the original call.
    Rewrite(0x00100AA0, 0x3C040062, 0x8C027F18, 'func_00100A40 (display mode object), mode 2 case',
            'lui a0,0x0062 -> lw v0,0x7F18(zero): the boot layout word (v0 is dead here)', 0x18),
    Rewrite(0x00100AA4, 0x0C0418EA, 0x1440000A, 'func_00100A40, mode 2 case',
            'jal 0x001063A8 -> bne v0,zero,0x00100AD0: on, the mode 4 case (480p); its delay slot is the next row'),
    Rewrite(0x00100AA8, 0x248484D0, 0x3C040062, 'func_00100A40, mode 2 case',
            'addiu a0,a0,-31536 -> lui a0,0x0062 (delay slot; the mode 4 case sets a0 again)'),
    Rewrite(0x00100AAC, 0x10000019, 0x0C0418EA, 'func_00100A40, mode 2 case',
            'beq zero,zero,0x00100B14 -> jal 0x001063A8: off, the original call'),
    Rewrite(0x00100AB0, 0x00000000, 0x248484D0, 'func_00100A40, mode 2 case',
            'nop -> addiu a0,a0,-31536 (delay slot): a0 = 0x006184D0 as before'),
    Rewrite(0x00100AB4, 0x00000000, 0x10000017, 'func_00100A40, mode 2 case (alignment padding before the mode 3 case)',
            'nop -> beq zero,zero,0x00100B14: the original exit; its delay slot is the first word of the mode 3 case (lui a0,0x0062), '
            'which the exit path overwrites (addiu a0,zero,-1 at 0x00100B18)'),
)
BOOT_LAYOUT_VALUES = (0, 1)  # block word 0x7F18: (off, on)

LUI = 0x0F
ADDIU = 0x09
LW = 0x23
SPECIAL = 0x00
DADDU = 0x2D


def decode(word):
    return {'opcode': word >> 26, 'rs': (word >> 21) & 31, 'rt': (word >> 16) & 31, 'rd': (word >> 11) & 31,
            'imm': word & 0xFFFF}


def is_daddu_zero(word):
    """`daddu rd,zero,zero`: the compiler's way to load the constant 0."""
    fields = decode(word)
    return fields['opcode'] == SPECIAL and word & 0x7FF == DADDU and fields['rs'] == 0 and fields['rt'] == 0


def destination(word):
    """The register an original constant-load instruction writes."""
    return decode(word)['rd'] if is_daddu_zero(word) else decode(word)['rt']


def encode_lw(rt, offset):
    if not 0 <= offset < 0x8000 or offset % 4:
        raise ValueError('lw offset(zero) must be a non-negative aligned 15-bit offset')
    return (LW << 26) | (0 << 21) | (rt << 16) | offset


def original_value(word):
    """The 32-bit value the original instruction puts in its register (lui: imm << 16; addiu rt,zero,imm: imm;
    daddu rd,zero,zero: 0)."""
    fields = decode(word)
    if is_daddu_zero(word) and fields['rd'] != 0:
        return 0
    if fields['opcode'] == LUI and fields['rs'] == 0:
        return (fields['imm'] << 16) & 0xFFFFFFFF
    if fields['opcode'] == ADDIU and fields['rs'] == 0:
        imm = fields['imm']
        return (imm - 0x10000 if imm & 0x8000 else imm) & 0xFFFFFFFF
    raise ValueError(f'unsupported original instruction 0x{word:08X}')


def check_table(patches=PATCHES):
    """The table is self-consistent: new word = lw of the same register from the block; the block value that
    reproduces the original (16:9, or progressive off) = the original value. Aspect rows own their block word;
    progressive rows may share one when they agree on both values."""
    words = {}
    addresses = set()
    for patch in patches:
        if patch.address in addresses:
            raise ValueError(f'0x{patch.address:08X}: patched twice')
        addresses.add(patch.address)
        if isinstance(patch, Rewrite):
            if patch.old_word == patch.new_word:
                raise ValueError(f'0x{patch.address:08X}: rewrite changes nothing')
            if patch.block_offset is not None:
                if decode(patch.new_word)['opcode'] != LW or decode(patch.new_word)['rs'] != 0 or \
                        decode(patch.new_word)['imm'] != PARAMETER_BLOCK + patch.block_offset:
                    raise ValueError(f'0x{patch.address:08X}: rewrite does not load its block word')
                if words.get(patch.block_offset, BOOT_LAYOUT_VALUES) != BOOT_LAYOUT_VALUES:
                    raise ValueError('two patches share a block word')
                words[patch.block_offset] = BOOT_LAYOUT_VALUES
            continue
        if patch.new_word != encode_lw(destination(patch.old_word), PARAMETER_BLOCK + patch.block_offset):
            raise ValueError(f'0x{patch.address:08X}: new word is not lw rt,block+offset(zero)')
        if original_value(patch.old_word) != patch.value_16_9:
            raise ValueError(f'0x{patch.address:08X}: 16:9 block value differs from the original constant')
        values = (patch.value_16_9, patch.value_progressive)
        if patch.block_offset in words:
            if patch.value_progressive is None or words[patch.block_offset] != values:
                raise ValueError('two patches share a block word')
        words[patch.block_offset] = values
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


def verify_old_words(elf, patches=PATCHES):
    check_table(patches)
    for patch in patches:
        observed = word_at(elf, patch.address)
        if observed != patch.old_word:
            raise ValueError(f'0x{patch.address:08X}: ELF word 0x{observed:08X}, table old word 0x{patch.old_word:08X}')
    return True


def patch_elf(elf, patches=PATCHES):
    """The patched derivative; refuses unless every old word matches."""
    verify_old_words(elf, patches)
    patched = bytearray(elf)
    for patch in patches:
        struct.pack_into('<I', patched, file_offset(elf, patch.address), patch.new_word)
    return bytes(patched)


def table_rows(patches=PATCHES):
    rows = []
    for p in patches:
        if isinstance(p, Rewrite):
            row = {'address': f'0x{p.address:08X}', 'old_word': f'0x{p.old_word:08X}', 'new_word': f'0x{p.new_word:08X}',
                   'kind': 'rewrite', 'function': p.function, 'purpose': p.purpose}
            if p.block_offset is not None:
                row['block_word'] = f'0x{PARAMETER_BLOCK + p.block_offset:08X}'
                row['value_original'] = f'0x{BOOT_LAYOUT_VALUES[0]:08X}'
                row['value_progressive'] = f'0x{BOOT_LAYOUT_VALUES[1]:08X}'
            rows.append(row)
            continue
        row = {'address': f'0x{p.address:08X}', 'old_word': f'0x{p.old_word:08X}', 'new_word': f'0x{p.new_word:08X}',
               'block_word': f'0x{PARAMETER_BLOCK + p.block_offset:08X}', 'function': p.function, 'purpose': p.purpose}
        if p.value_progressive is None:
            row['value_16_9'] = f'0x{p.value_16_9:08X}'
        else:
            row['value_original'] = f'0x{p.value_16_9:08X}'
            row['value_progressive'] = f'0x{p.value_progressive:08X}'
        rows.append(row)
    return rows


@dataclass(frozen=True)
class Profile:
    name: str
    task: str
    slice: int
    patches: tuple
    manifest: str
    patched_elf: str
    receipt: str
    log: str
    producer: str
    qualification: str


PROFILES = {
    'aspect': Profile('aspect', 'TASK-080 P2', 217, PATCHES, LIBRARY_MANIFEST, 'SCUS_973.28.aspect.elf',
                      'aspect-generation.json', 'generation-aspect.log', 'private/slice217-ee-aspect-producer',
                      'slice208 coverage library with the TASK-080 P2 ELF patch table; owner gameplay pending'),
    'progressive': Profile('progressive', 'TASK-086 step 1', 232, PATCHES + PROGRESSIVE_PATCHES + BOOT_REWRITES,
                           'docs/inputs/slice232-ee-libraries.json', 'SCUS_973.28.progressive.elf',
                           'progressive-generation.json', 'generation-progressive.log',
                           'private/slice232-ee-progressive-producer',
                           'slice208 coverage library with the TASK-080 P2 and TASK-086 step 1 ELF patch table; '
                           'owner gameplay pending'),
}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def produce(assets_root, output, profile_name='aspect'):
    profile = PROFILES[profile_name]
    import ee_entries
    import ee_producer
    ee_producer.prepare(assets_root, output)
    output, metadata, study = ee_producer.existing_producer(output)
    original = (output / 'SCUS_973.28.elf').read_bytes()
    if sha256(original) != ELF_SHA256:
        raise ValueError('reconstructed ELF differs from the pinned ELF')
    patched = patch_elf(original, profile.patches)
    patched_path = output / profile.patched_elf
    with patched_path.open('xb') as destination_file:
        destination_file.write(patched)
    seed_file, seeds, pin = ee_entries.seed_set_pin(SEED_SET)
    ee_entries.check_seeds_executable(patched, seeds)
    copied = output / f'entries-{SEED_SET}.txt'
    with copied.open('xb') as destination_file:
        destination_file.write(seed_file.read_bytes())
    artifact = next(item for item in study['artifacts'] if item['path'].endswith('/eerecomp.exe'))
    translator = Path(metadata['assets_root']) / artifact['path']
    ee_producer.verify_identity(translator, {key: artifact[key] for key in ('size_bytes', 'sha256')})
    arguments = [str(translator), str(patched_path), '--out', str(output / 'generated'), '--stats', '--entries',
                 str(copied)]
    log_path = output / profile.log
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
    expected = sorted({unit_holding(sources, patch.address) for patch in profile.patches})
    if changed != expected:
        raise ValueError(f'changed units {changed} differ from the units holding the patched words {expected}')
    result = {'schema_version': 1, 'task': profile.task, 'slice': profile.slice, 'profile': profile.name,
              'elf': {'path': 'SCUS_973.28.elf', 'sha256': sha256(original)},
              'patched_elf': {'path': patched_path.name, 'sha256': sha256(patched), 'size_bytes': len(patched)},
              'parameter_block': f'0x{PARAMETER_BLOCK:08X}', 'patches': table_rows(profile.patches),
              'seed_set': SEED_SET, 'seed_pin': pin, 'translator': ee_producer.identity(translator),
              'arguments': arguments, 'stats': stats, 'resolved_seed_count': len(resolved),
              'generated': records, 'generated_summary': ee_producer.inventory_summary(records),
              'changed_units_vs_slice208': changed,
              'qualification': 'Source generation only; library, runtime and gameplay unqualified'}
    ee_producer.write_json(output / profile.receipt, result)
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


def record_library(output, profile_name='aspect'):
    profile = PROFILES[profile_name]
    import ee_archive
    import ee_producer
    output, metadata, _ = ee_producer.existing_producer(output)
    generation = ee_producer.read_json(output / profile.receipt)
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
    if relative != profile.producer:
        raise ValueError(f'the {profile.name} library must be built in {profile.producer}, not {relative}')
    entry = {'path': f'{relative}/game-build/ps2game.lib', **ee_producer.identity(library),
             'patched_elf_sha256': generation['patched_elf']['sha256'], 'parameter_block': generation['parameter_block'],
             'patches': generation['patches'], 'seed_set': generation['seed_set'],
             'seed_sha256': generation['seed_pin']['sha256'], 'translator_entries': generation['stats']['entries'],
             'source_inventory': generation['generated_summary'], 'object_count': len(objects),
             'object_inventory_without_timestamps': ee_archive.inventory_digest(objects),
             'changed_objects_vs_slice208': changed, 'compiler_receipt': receipt,
             'base_library': f'{BASE_PRODUCER}/game-build/ps2game.lib',
             'qualification': profile.qualification}
    manifest = {'schema_version': 1, 'slice': profile.slice, 'donor_commit': metadata['donor_commit'],
                'profiles': {profile.name: entry}, 'production_pins_modified': False}
    path = ROOT / profile.manifest
    with path.open('x', encoding='utf-8', newline='\n') as destination_file:
        destination_file.write(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
    return entry


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    check = commands.add_parser('check')
    check.add_argument('--elf', type=Path, required=True)
    check.add_argument('--profile', choices=sorted(PROFILES), default=PROFILE)
    prod = commands.add_parser('produce')
    prod.add_argument('--assets-root', type=Path, required=True)
    prod.add_argument('--output', type=Path, required=True)
    prod.add_argument('--profile', choices=sorted(PROFILES), default=PROFILE)
    rec = commands.add_parser('record-library')
    rec.add_argument('--output', type=Path, required=True)
    rec.add_argument('--profile', choices=sorted(PROFILES), default=PROFILE)
    args = parser.parse_args(argv)
    try:
        if args.command == 'check':
            patches = PROFILES[args.profile].patches
            verify_old_words(args.elf.read_bytes(), patches)
            print(json.dumps(table_rows(patches), indent=2))
        elif args.command == 'produce':
            result = produce(args.assets_root, args.output, args.profile)
            print(f"Generated {result['generated_summary']['file_count']} files; changed units "
                  f"{result['changed_units_vs_slice208']}; patched ELF {result['patched_elf']['sha256']}")
        else:
            entry = record_library(args.output, args.profile)
            print(f"Recorded library {entry['sha256']}; changed objects {entry['changed_objects_vs_slice208']}")
        return 0
    except (ValueError, OSError, subprocess.CalledProcessError, KeyError, StopIteration) as error:
        print(f'aspect-elf-patch: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
