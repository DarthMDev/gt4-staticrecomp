"""TASK-080 P2 (slice217): the ELF patch table (scripts/aspect_elf_patch.py), its parameter block in
native/display_aspect.hpp, decision 0071, the re-translated library profile "aspect" and its pins.

Rules checked (decision 0071, guard rail 3): every patched word has its address, old word and new word in the table and
in the decision; the old words match the pinned reconstructed ELF; each new word is one `lw` of the same register from
the HUD parameter block; the block's 16:9 values are exactly the constants the original instructions produced, so a
16:9 player runs the same arithmetic.
"""
import hashlib
import json
import os
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import aspect_elf_patch as table  # noqa: E402

ASSETS = Path(os.environ.get('GT4_ASSETS_ROOT', 'C:/Antigravity/gt4-staticrecomp'))
ELF = ASSETS / 'private/reconstructed/SCUS_973.28.elf'
MANIFEST = ROOT / 'docs/inputs/slice217-ee-libraries.json'


def source(relative):
    return (ROOT / relative).read_text(encoding='utf-8')


def header_constant(name):
    match = re.search(rf'inline constexpr std::uint32_t {name} = (0x[0-9A-Fa-f]+|\d+)u;', source('native/display_aspect.hpp'))
    if not match:
        raise AssertionError(f'{name} not found in native/display_aspect.hpp')
    return int(match.group(1), 0)


class PatchTableTests(unittest.TestCase):
    def test_table_is_self_consistent(self):
        self.assertTrue(table.check_table())
        self.assertEqual([hex(p.address) for p in table.PATCHES], ['0x39f554', '0x33ef98'])

    def test_new_words_are_loads_from_the_block(self):
        for patch in table.PATCHES:
            with self.subTest(address=hex(patch.address)):
                old, new = table.decode(patch.old_word), table.decode(patch.new_word)
                self.assertEqual(new['opcode'], table.LW)
                self.assertEqual(new['rs'], 0, 'base register zero')
                self.assertEqual(new['rt'], old['rt'], 'same destination register')
                self.assertEqual(new['imm'], table.PARAMETER_BLOCK + patch.block_offset)
                self.assertLess(new['imm'], 0x8000, 'reachable as a positive 16-bit offset from zero')

    def test_block_matches_the_host_header(self):
        self.assertEqual(table.PARAMETER_BLOCK, header_constant('kHudParameterBlock'))
        self.assertEqual(len(table.PATCHES), header_constant('kHudParameterWords'))
        by_offset = {p.block_offset: p for p in table.PATCHES}
        self.assertEqual(by_offset[header_constant('kFontScaleOffset')].value_16_9, header_constant('kGameFontScaleBits'))
        self.assertEqual(by_offset[header_constant('kMirrorDivisorOffset')].value_16_9,
                         header_constant('kGameMirrorDivisor'))

    def test_decision_0071_lists_every_word(self):
        decision = source('docs/decisions/0071-native-aspect-ratios-runtime-hooks.md')
        for patch in table.PATCHES:
            with self.subTest(address=hex(patch.address)):
                self.assertIn(f'0x{patch.address:08X}', decision)
                self.assertIn(f'0x{patch.old_word:08X}', decision)
                self.assertIn(f'0x{patch.new_word:08X}', decision)
        self.assertIn('0x00007F00', decision)
        self.assertIn('HUD canvas hook', decision)

    def test_unsupported_original_is_refused(self):
        with self.assertRaises(ValueError):
            table.original_value(0x8C017F00)
        with self.assertRaises(ValueError):
            table.encode_lw(1, 0x8000)

    def test_old_words_match_the_pinned_elf(self):
        if not ELF.exists():
            self.skipTest('reconstructed ELF not present')
        elf = ELF.read_bytes()
        self.assertEqual(hashlib.sha256(elf).hexdigest(), table.ELF_SHA256)
        self.assertTrue(table.verify_old_words(elf))
        patched = table.patch_elf(elf)
        self.assertEqual(len(patched), len(elf))
        differing = [i for i in range(len(elf)) if elf[i] != patched[i]]
        allowed = set()
        for patch in table.PATCHES:
            start = table.file_offset(elf, patch.address)
            allowed.update(range(start, start + 4))
            self.assertEqual(table.word_at(patched, patch.address), patch.new_word)
        self.assertTrue(set(differing) <= allowed, 'only the patched words change')
        corrupted = bytearray(elf)
        corrupted[table.file_offset(elf, table.PATCHES[0].address)] ^= 1
        with self.assertRaises(ValueError):
            table.patch_elf(bytes(corrupted))


class AspectLibraryPinTests(unittest.TestCase):
    def test_manifest_records_the_table_and_the_library(self):
        manifest = json.loads(MANIFEST.read_text(encoding='utf-8'))
        entry = manifest['profiles']['aspect']
        self.assertEqual(entry['path'], 'private/slice217-ee-aspect-producer/game-build/ps2game.lib')
        self.assertEqual(entry['patches'], table.table_rows())
        self.assertEqual(entry['parameter_block'], '0x00007F00')
        self.assertEqual(entry['changed_objects_vs_slice208'], ['unit_008F.cpp.obj', 'unit_00A7.cpp.obj'])
        self.assertEqual(entry['seed_set'], 'accepted')
        library = ASSETS / entry['path']
        if library.exists():
            self.assertEqual(hashlib.sha256(library.read_bytes()).hexdigest(), entry['sha256'])

    def test_cmake_profile(self):
        cmake = source('native/CMakeLists.txt')
        self.assertIn('elseif(GT4_EE_PROFILE STREQUAL "aspect")', cmake)
        self.assertIn('docs/inputs/slice217-ee-libraries.json', cmake)
        self.assertIn('^private/slice217-ee-aspect-producer/game-build/ps2game', cmake)

    def test_mode_pcsx2_uses_the_aspect_library(self):
        manifest = json.loads((ROOT / 'docs/inputs/slice217-pcsx2.json').read_text(encoding='utf-8'))
        entry = json.loads(MANIFEST.read_text(encoding='utf-8'))['profiles']['aspect']
        self.assertEqual(manifest['frozen_game_library']['path'], entry['path'])
        self.assertEqual(manifest['frozen_game_library']['sha256'], entry['sha256'])
        self.assertEqual(manifest['ee_library']['profile'], 'aspect')


if __name__ == '__main__':
    unittest.main()
