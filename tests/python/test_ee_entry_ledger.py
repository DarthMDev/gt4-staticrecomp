"""Synthetic controls for the read-only EE entry ledger (TASK-014 stage B, Phase 1).

Every detector is checked with hand-encoded MIPS words, so no game data is needed. The smoke test
at the end reads the reconstructed ELF and the generated units when they exist and is skipped otherwise.
"""

import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))

import ee_entry_ledger as ledger  # noqa: E402

BASE = 0x00100000
JR_RA = 0x03E00008
NOP = 0x00000000


def itype(op, rs, rt, immediate):
    return (op << 26) | (rs << 21) | (rt << 16) | (immediate & 0xFFFF)


def rtype(funct, rs, rt, rd, sa=0):
    return (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | funct


def addiu(rt, rs, immediate):
    return itype(0x09, rs, rt, immediate)


def lui(rt, immediate):
    return itype(0x0F, 0, rt, immediate)


def ori(rt, rs, immediate):
    return itype(0x0D, rs, rt, immediate)


def beq(rs, rt, offset_words):
    return itype(0x04, rs, rt, offset_words)


def jump(op, target):
    return (op << 26) | ((target >> 2) & 0x03FFFFFF)


SP, RA, T0, T1, A0 = 29, 31, 8, 9, 4


def words_to_bytes(words):
    return b''.join(struct.pack('<I', word) for word in words)


def make_elf(segments):
    """ELF32 little-endian MIPS with the given PT_LOAD segments: (vaddr, flags, body) tuples."""
    header_size = 52
    program_size = 32 * len(segments)
    offset = header_size + program_size
    program_headers = b''
    bodies = b''
    for vaddr, flags, body in segments:
        program_headers += struct.pack('<8I', 1, offset + len(bodies), vaddr, vaddr, len(body), len(body),
                                       flags, 16)
        bodies += body
    header = b'\x7fELF\x01\x01\x01' + b'\0' * 9
    header += struct.pack('<HHIIIIIHHHHHH', 2, 8, 1, segments[0][0], header_size, 0, 0x20924001,
                          header_size, 32, len(segments), 0, 0, 0)
    return header + program_headers + bodies


def image_of(segments):
    return ledger.Image(ledger.load_segments(make_elf(segments)))


def text_segment(words, base=BASE):
    """Executable segment padded with zero words to 64 words, so targets a few words ahead stay in text."""
    padded = list(words) + [NOP] * max(0, 64 - len(words))
    return (base, 5, words_to_bytes(padded))


class SegmentParsing(unittest.TestCase):
    def test_keeps_executable_and_data_segments(self):
        image = image_of([text_segment([JR_RA, NOP]), (0x00600000, 6, words_to_bytes([0x1234]))])
        self.assertTrue(image.is_text(BASE))
        self.assertFalse(image.is_text(0x00600000))
        self.assertEqual(image.word(0x00600000), 0x1234)

    def test_rejects_non_mips_input(self):
        with self.assertRaises(ValueError):
            ledger.load_segments(b'\x7fELF\x02\x01\x01' + b'\0' * 100)

    def test_rejects_missing_executable_segment(self):
        with self.assertRaises(ValueError):
            ledger.load_segments(make_elf([(0x00600000, 6, words_to_bytes([0]))]))


class FallthroughDetector(unittest.TestCase):
    def test_stack_frame_after_return_is_found(self):
        image = image_of([text_segment([JR_RA, NOP, addiu(SP, SP, -16), addiu(2, 0, 1)])])
        hits, no_frame = ledger.detect_fallthrough(image, ledger.scan_control_flow(image))
        self.assertEqual(hits, {BASE + 8: f'after jump at 0x{BASE:08X}'})
        self.assertEqual(no_frame, 0)

    def test_branch_target_excludes_candidate(self):
        # the beq at +12 branches back to +8, so +8 is local code, not a function start
        words = [JR_RA, NOP, addiu(SP, SP, -16), beq(0, 0, -2), NOP, addiu(2, 0, 1)]
        image = image_of([text_segment(words)])
        hits, _ = ledger.detect_fallthrough(image, ledger.scan_control_flow(image))
        self.assertNotIn(BASE + 8, hits)

    def test_jump_without_stack_frame_is_counted_not_found(self):
        image = image_of([text_segment([jump(0x02, BASE + 16), NOP, addiu(2, 0, 1), NOP, NOP])])
        hits, no_frame = ledger.detect_fallthrough(image, ledger.scan_control_flow(image))
        self.assertEqual(hits, {})
        self.assertEqual(no_frame, 1)

    def test_daddiu_prologue_counts(self):
        image = image_of([text_segment([JR_RA, NOP, itype(0x19, SP, SP, -48)])])
        hits, _ = ledger.detect_fallthrough(image, ledger.scan_control_flow(image))
        self.assertIn(BASE + 8, hits)


class DataWordDetector(unittest.TestCase):
    def test_vtable_slot_pointing_at_text_is_found(self):
        vtable = 0x00600000
        image = image_of([text_segment([NOP, JR_RA]), (vtable, 6, words_to_bytes([0, BASE + 4, 0]))])
        self.assertEqual(set(ledger.detect_datawords(image)), {BASE + 4})

    def test_zero_target_and_out_of_text_values_are_ignored(self):
        vtable = 0x00600000
        image = image_of([text_segment([NOP, NOP]),
                          (vtable, 6, words_to_bytes([BASE + 4, 0x00900000, 0x00200002]))])
        self.assertEqual(ledger.detect_datawords(image), {})

    def test_unaligned_value_is_not_a_function_start(self):
        image = image_of([text_segment([NOP, JR_RA]), (0x00600000, 6, words_to_bytes([BASE + 2]))])
        self.assertEqual(ledger.detect_datawords(image), {})


class LuiPairDetector(unittest.TestCase):
    def test_lui_addiu_builds_text_address(self):
        image = image_of([text_segment([lui(T0, 0x0010), addiu(T0, T0, 0x0040), NOP, JR_RA])])
        self.assertEqual(ledger.detect_luipairs(image), {0x00100040: f'lui at 0x{BASE:08X}'})

    def test_negative_addiu_low_half_adjusts_high_half(self):
        # hi=0x0011, lo=0xFFF0 (-16): 0x00110000 - 16 = 0x0010FFF0
        image = image_of([text_segment([lui(T0, 0x0011), addiu(T0, T0, 0xFFF0), NOP, JR_RA], base=0x0010FF00)])
        self.assertIn(0x0010FFF0, ledger.detect_luipairs(image))

    def test_ori_uses_unsigned_low_half(self):
        image = image_of([text_segment([lui(T0, 0x0010), ori(T0, T0, 0x8000), NOP, JR_RA], base=0x00108000)])
        self.assertIn(0x00108000, ledger.detect_luipairs(image))

    def test_intervening_write_to_register_stops_the_scan(self):
        overwrite = addiu(T0, 0, 5)
        image = image_of([text_segment([lui(T0, 0x0010), overwrite, addiu(T0, T0, 0x0040), NOP])])
        self.assertEqual(ledger.detect_luipairs(image), {})

    def test_window_limits_the_search(self):
        filler = [addiu(T1, 0, 1)] * 5
        image = image_of([text_segment([lui(T0, 0x0010)] + filler + [addiu(T0, T0, 0x0040)])])
        self.assertEqual(ledger.detect_luipairs(image), {})
        self.assertIn(0x00100040, ledger.detect_luipairs(image, window=40))


class ControlFlowAndNames(unittest.TestCase):
    def test_translated_entries_come_from_case_labels(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / 'unit_0000.cpp').write_text(
                'switch (entry) {\n    case 1u: goto L_00100000;\n    case 2u: goto L_00100004;\n'
                '    case 0u: goto L_00100008;\n}\n', encoding='utf-8')
            self.assertEqual(ledger.translated_entries(folder), {0x00100000, 0x00100004})

    def test_empty_translation_is_an_error(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                ledger.translated_entries(Path(directory))

    def test_confidence_rules(self):
        self.assertEqual(ledger.confidence_of({'fallthrough'}, True, 0x27BDFFF0), 'high')
        self.assertEqual(ledger.confidence_of({'luipair', 'dataword'}, True, 0x27BDFFF0), 'high')
        self.assertEqual(ledger.confidence_of({'decomp'}, False, 0x24420001), 'medium')
        self.assertEqual(ledger.confidence_of({'luipair'}, True, 0x27BDFFF0), 'medium')
        self.assertEqual(ledger.confidence_of({'dataword'}, False, 0x24420001), 'low')
        self.assertEqual(ledger.confidence_of({'dataword'}, False, 0), 'rejected')

    def test_disassembler_covers_prologue_and_return(self):
        self.assertEqual(ledger.disassemble(addiu(SP, SP, -16), BASE), 'addiu $sp,$sp,-16')
        self.assertEqual(ledger.disassemble(JR_RA, BASE), 'jr $ra')
        self.assertEqual(ledger.disassemble(NOP, BASE), 'nop')
        self.assertEqual(ledger.disassemble(rtype(0x2D, 0, A0, 16), BASE), 'daddu $s0,$zero,$a0')

    def test_end_to_end_on_a_synthetic_tree(self):
        text = [JR_RA, NOP, addiu(SP, SP, -16), NOP, JR_RA, NOP,  # +8 new; +16 translated (dataword); +24 new
                addiu(SP, SP, -32), NOP]
        data = (0x00600000, 6, words_to_bytes([BASE + 0x10]))
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory)
            (assets / 'private/reconstructed').mkdir(parents=True)
            (assets / 'private/reconstructed/SCUS_973.28.elf').write_bytes(
                make_elf([text_segment(text), data]))
            generated = assets / 'private/reference-acceleration-study/generated'
            generated.mkdir(parents=True)
            (generated / 'unit_0000.cpp').write_text(f'case 1u: goto L_{BASE + 0x10:08X};\n', encoding='utf-8')
            names = assets / 'names.csv'
            names.write_text('address,name,kind,origin,quality\n'
                             f'0x{BASE + 0x20:08X},Synthetic__func,func,decomp,named\n', encoding='utf-8')
            result = ledger.build_ledger(assets, names)
            rows = {row['address']: row for row in result['rows']}
            self.assertEqual(result['translated_count'], 1)
            self.assertIn(f'0x{BASE + 8:08X}', rows)
            self.assertFalse(rows[f'0x{BASE + 8:08X}']['translated'])
            self.assertEqual(rows[f'0x{BASE + 8:08X}']['confidence'], 'high')
            self.assertIn(f'0x{BASE + 0x10:08X}', rows)
            self.assertTrue(rows[f'0x{BASE + 0x10:08X}']['translated'])
            self.assertIn(f'0x{BASE + 0x20:08X}', rows)
            self.assertIn(f'0x{BASE + 0x18:08X}', rows)
            summary = ledger.summarize(result)
            self.assertEqual(summary['candidates_new'], 3)
            self.assertEqual(summary['decomp_names_outside_text'], 0)


@unittest.skipUnless(os.path.exists(os.environ.get('GT4_ASSETS_ROOT', str(ROOT))) and
                     (Path(os.environ.get('GT4_ASSETS_ROOT', str(ROOT))) / ledger.ELF_RELATIVE).exists() and
                     (Path(os.environ.get('GT4_ASSETS_ROOT', str(ROOT))) / ledger.GENERATED_RELATIVE).exists(),
                     'private ELF and generated units are not in this checkout')
class RealElfSmoke(unittest.TestCase):
    def test_real_ledger_has_entries_and_no_high_without_stack_frame(self):
        assets = Path(os.environ.get('GT4_ASSETS_ROOT', str(ROOT)))
        names = ROOT / ledger.NAMES_RELATIVE
        result = ledger.build_ledger(assets, names)
        summary = ledger.summarize(result)
        self.assertGreater(summary['translated_entries'], 100000)
        self.assertGreater(summary['candidates_total'], 0)
        for row in result['rows']:
            if row['confidence'] == 'high':
                self.assertTrue(row['stack_frame'])
        json.dumps(summary)


if __name__ == '__main__':
    unittest.main()
