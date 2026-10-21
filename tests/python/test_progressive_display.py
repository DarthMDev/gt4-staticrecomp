"""TASK-086 step 1 (slice232): progressive 480p menus and races.

Checked here:
- the progressive rows of the ELF patch table (scripts/aspect_elf_patch.py PROGRESSIVE_PATCHES): old words against the
  pinned reconstructed ELF, new words = one `lw` of the same register from the progressive display words, the
  original values = the original constants, and the same numbers in native/progressive_display.hpp and decision 0072;
- the library profile "progressive" (pin, CMake), the probe's GT4_PROGRESSIVE validation and record;
- the launcher row and its variable;
- the CRT gate (scripts/progressive_gate.py) on synthetic runs.
"""
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'tests/python'))
import aspect_elf_patch as table  # noqa: E402
import external_runtime_probe as probe  # noqa: E402
import progressive_gate as gate  # noqa: E402
import test_external_runtime_probe as probe_tests  # noqa: E402

ASSETS = Path(os.environ.get('GT4_ASSETS_ROOT', 'C:/Antigravity/gt4-staticrecomp'))
ELF = ASSETS / 'private/reconstructed/SCUS_973.28.elf'
MANIFEST = ROOT / 'docs/inputs/slice232-ee-libraries.json'
HEADER = ROOT / 'native/progressive_display.hpp'


def source(relative):
    return (ROOT / relative).read_text(encoding='utf-8')


def header_constant(name):
    match = re.search(rf'inline constexpr std::uint32_t {name} = (0x[0-9A-Fa-f]+|\d+)u;', HEADER.read_text(encoding='utf-8'))
    if not match:
        raise AssertionError(f'{name} not found in native/progressive_display.hpp')
    return int(match.group(1), 0)


class ProgressiveTableTests(unittest.TestCase):
    def test_rows(self):
        self.assertTrue(table.check_table(table.PROGRESSIVE_PATCHES))
        self.assertTrue(table.check_table(table.PATCHES + table.PROGRESSIVE_PATCHES))
        self.assertEqual([hex(p.address) for p in table.PROGRESSIVE_PATCHES],
                         ['0x436750', '0x436754', '0x436700', '0x436708', '0x436680', '0x218240', '0x217ea0',
                          '0x217ef8', '0x3da488', '0x3da490'])
        self.assertEqual(table.PROFILES['progressive'].patches,
                         table.PATCHES + table.PROGRESSIVE_PATCHES + table.BOOT_REWRITES)
        self.assertEqual(table.PROFILES['aspect'].patches, table.PATCHES, 'the slice217 table is unchanged')
        self.assertTrue(table.check_table(table.PROFILES['progressive'].patches))

    def test_boot_rewrites(self):
        rows = {p.address: p for p in table.BOOT_REWRITES}
        self.assertEqual(sorted(hex(a) for a in rows),
                         sorted(['0x1062fc', '0x10631c', '0x106324', '0x10632c', '0x106330', '0x106344', '0x106360',
                                 '0x106364', '0x1077a4', '0x1077a8', '0x1077b4', '0x100aa0', '0x100aa4', '0x100aa8',
                                 '0x100aac', '0x100ab0', '0x100ab4']))
        # func_00100A40 mode 2: on, the mode 4 case (480p); off, the original call and exit.
        self.assertEqual(rows[0x00100AA0].block_offset, header_constant('kBootLayoutOffset'))
        self.assertEqual(rows[0x00100AA4].new_word, 0x1440000A, 'bne v0,zero,0x00100AD0')
        self.assertEqual(rows[0x00100AAC].new_word, rows[0x00100AA4].old_word, 'the original jal 0x001063A8')
        self.assertEqual(rows[0x00100AB4].new_word, 0x10000017, 'beq zero,zero,0x00100B14')
        self.assertEqual((rows[0x00100AB0].old_word, rows[0x00100AB4].old_word), (0, 0), 'nop and padding')
        # The layout choice: s0 = the block word 0x7F18 (0 = the original path, also before the host fills it), and
        # the branch now goes to the 480p path when it is not zero.
        boot = rows[0x001062FC]
        self.assertEqual(boot.block_offset, header_constant('kBootLayoutOffset'))
        self.assertEqual(table.decode(boot.new_word)['rt'], 16)
        self.assertEqual(table.BOOT_LAYOUT_VALUES,
                         (header_constant('kOriginalBootLayout'), header_constant('kProgressiveBootLayout')))
        self.assertEqual(table.BOOT_LAYOUT_VALUES[0], 0, 'zero (not filled yet) is the original boot')
        self.assertEqual(rows[0x00106344].old_word >> 26, 0x04, 'beq')
        self.assertEqual(rows[0x00106344].new_word >> 26, 0x05, 'bne')
        self.assertEqual(rows[0x00106344].old_word & 0x03FFFFFF, rows[0x00106344].new_word & 0x03FFFFFF, 'operands')
        # The unused path calls the game's 480p initialiser instead of func_00106710 and keeps its return point.
        self.assertEqual(rows[0x00106360].new_word, 0x0C000000 | (0x00106DA8 >> 2))
        self.assertEqual(rows[0x00106360].old_word, 0x0C000000 | (0x00106710 >> 2))
        # The stores keep their values: the 1.0f bits from at, -1 from a3 instead of v1.
        self.assertEqual(table.decode(rows[0x00106330].new_word)['rt'], 1)
        self.assertEqual(rows[0x00106324].new_word & 0xFFFF, rows[0x00106324].old_word & 0xFFFF)
        self.assertEqual(table.decode(rows[0x0010632C].new_word)['rt'], table.decode(rows[0x00106324].new_word)['rt'])
        # func_00107788 re-applies the state's CRT words (interlace +0, mode +4, ffmd +8) through s0.
        for address, offset in ((0x001077A4, 0), (0x001077B4, 4), (0x001077A8, 8)):
            fields = table.decode(rows[address].new_word)
            self.assertEqual((fields['opcode'], fields['rs'], fields['imm']), (table.LW, 16, offset))
            self.assertEqual(fields['rt'], table.decode(rows[address].old_word)['rt'])

    def test_rewrite_rules(self):
        same = table.Rewrite(0x00106360, 0x0C0419C4, 0x0C0419C4, 'x', 'y')
        with self.assertRaises(ValueError):
            table.check_table((same,))
        wrong_block = table.Rewrite(0x001062FC, 0x00A0802D, 0x8C107F18, 'x', 'y', 0x1C)
        with self.assertRaises(ValueError):
            table.check_table((wrong_block,))

    def test_the_menu_words_of_the_study(self):
        # docs/PROGRESSIVE-EVERYWHERE-STUDY.md 3.1 A: 0x436750 addiu a1,zero,1 and 0x436754 daddu a2,zero,zero.
        rows = {p.address: p for p in table.PROGRESSIVE_PATCHES}
        self.assertEqual(rows[0x00436750].old_word, 0x24050001)
        self.assertEqual(rows[0x00436754].old_word, 0x0000302D)
        # On: exactly the study's words would put 2 and 6 in a1 and a2; here they come from the block.
        self.assertEqual((rows[0x00436750].value_progressive, rows[0x00436754].value_progressive), (2, 6))

    def test_new_words_are_loads_of_the_same_register(self):
        for patch_row in table.PROGRESSIVE_PATCHES:
            with self.subTest(address=hex(patch_row.address)):
                new = table.decode(patch_row.new_word)
                self.assertEqual(new['opcode'], table.LW)
                self.assertEqual(new['rs'], 0)
                self.assertEqual(new['rt'], table.destination(patch_row.old_word))
                self.assertEqual(new['imm'], table.PARAMETER_BLOCK + patch_row.block_offset)
                self.assertGreaterEqual(new['imm'], table.PROGRESSIVE_BLOCK, 'never inside the HUD block')
                self.assertIsNotNone(patch_row.value_progressive)

    def test_daddu_zero_is_a_zero_load(self):
        self.assertTrue(table.is_daddu_zero(0x0000302D))
        self.assertEqual(table.destination(0x0000302D), 6)
        self.assertEqual(table.original_value(0x0000302D), 0)
        self.assertFalse(table.is_daddu_zero(0x0080302D), 'daddu a2,a0,zero is not a constant')
        with self.assertRaises(ValueError):
            table.original_value(0x0080302D)

    def test_shared_words_must_agree(self):
        first = table.PROGRESSIVE_PATCHES[0]
        clash = table.Patch(0x00436700, 0x24050001, 0x8C057F08, 0x08, 1, 'x', 'y', 3)
        with self.assertRaises(ValueError):
            table.check_table((first, clash))
        with self.assertRaises(ValueError):
            table.check_table((first, first))

    def test_header_matches_the_table(self):
        self.assertEqual(header_constant('kProgressiveBlock'), table.PROGRESSIVE_BLOCK)
        self.assertEqual(header_constant('kBlockBase'), table.PARAMETER_BLOCK)
        values = {p.block_offset: (p.value_16_9, p.value_progressive) for p in table.PROGRESSIVE_PATCHES}
        values[table.BOOT_REWRITES[0].block_offset] = table.BOOT_LAYOUT_VALUES
        self.assertEqual(len(values), header_constant('kProgressiveWords'))
        expect = {
            'kMenuPsmOffset': ('kOriginalMenuPsm', 'kProgressiveMenuPsm'),
            'kMenuRowOffset': ('kOriginalMenuRow', 'kProgressiveMenuRow'),
            'kFieldHalvingOffset': ('kOriginalFieldHalving', 'kProgressiveFieldHalving'),
            'kDrawBackViewportOffset': ('kOriginalDrawBackViewport', 'kProgressiveDrawBackViewport'),
            'kBootLayoutOffset': ('kOriginalBootLayout', 'kProgressiveBootLayout'),
            'kMonitorRowOffset': ('kOriginalMonitorRow', 'kProgressiveMonitorRow'),
        }
        for offset_name, (off_name, on_name) in expect.items():
            with self.subTest(offset=offset_name):
                self.assertEqual(values[header_constant(offset_name)],
                                 (header_constant(off_name), header_constant(on_name)))

    def test_decision_0072_lists_every_word(self):
        decision = source('docs/decisions/0072-progressive-480p-menus-and-races.md')
        for patch_row in table.PROGRESSIVE_PATCHES + table.BOOT_REWRITES:
            with self.subTest(address=hex(patch_row.address)):
                self.assertIn(f'0x{patch_row.address:08X}', decision)
                self.assertIn(f'0x{patch_row.old_word:08X}', decision)
                self.assertIn(f'0x{patch_row.new_word:08X}', decision)
        self.assertIn('GT4_PROGRESSIVE', decision)

    def test_old_words_match_the_pinned_elf(self):
        if not ELF.exists():
            self.skipTest('reconstructed ELF not present')
        elf = ELF.read_bytes()
        patches = table.PROFILES['progressive'].patches
        self.assertTrue(table.verify_old_words(elf, patches))
        patched = table.patch_elf(elf, patches)
        allowed = set()
        for patch_row in patches:
            start = table.file_offset(elf, patch_row.address)
            allowed.update(range(start, start + 4))
            self.assertEqual(table.word_at(patched, patch_row.address), patch_row.new_word)
        self.assertTrue({i for i in range(len(elf)) if elf[i] != patched[i]} <= allowed, 'only the patched words change')


class ProgressiveLibraryPinTests(unittest.TestCase):
    def test_manifest_records_the_table_and_the_library(self):
        manifest = json.loads(MANIFEST.read_text(encoding='utf-8'))
        self.assertEqual(manifest['slice'], 232)
        entry = manifest['profiles']['progressive']
        self.assertEqual(entry['path'], 'private/slice232-ee-progressive-producer/game-build/ps2game.lib')
        self.assertEqual(entry['patches'], table.table_rows(table.PROFILES['progressive'].patches))
        self.assertEqual(entry['changed_objects_vs_slice208'],
                         ['unit_0000.cpp.obj', 'unit_0001.cpp.obj', 'unit_0045.cpp.obj', 'unit_0046.cpp.obj',
                          'unit_008F.cpp.obj', 'unit_00A7.cpp.obj', 'unit_00B6.cpp.obj', 'unit_00CD.cpp.obj'])
        self.assertEqual(entry['seed_set'], 'accepted')
        library = ASSETS / entry['path']
        if library.exists():
            self.assertEqual(hashlib.sha256(library.read_bytes()).hexdigest(), entry['sha256'])

    def test_cmake_profile(self):
        cmake = source('native/CMakeLists.txt')
        self.assertIn('elseif(GT4_EE_PROFILE STREQUAL "progressive")', cmake)
        self.assertIn('docs/inputs/slice232-ee-libraries.json', cmake)
        self.assertIn('^private/slice232-ee-progressive-producer/game-build/ps2game', cmake)
        self.assertIn('GT4_PROGRESSIVE_LIBRARY=1', cmake)
        self.assertIn('native_progressive_display', source('CMakeLists.txt'))

    def test_mode_pcsx2_uses_the_progressive_library(self):
        self.assertEqual(probe.PERF_PROFILES['pcsx2'], ('slice232-pcsx2.json', 'private/slice232-build/build/gt4vulkan.exe'))
        self.assertEqual(probe.PERF_PROFILES['pcsx2-slice218'],
                         ('slice218-pcsx2.json', 'private/slice218-build/build/gt4vulkan.exe'))
        pin = json.loads((ROOT / 'docs/inputs/slice232-pcsx2.json').read_text(encoding='utf-8'))
        entry = json.loads(MANIFEST.read_text(encoding='utf-8'))['profiles']['progressive']
        self.assertEqual(pin['frozen_game_library']['path'], entry['path'])
        self.assertEqual(pin['frozen_game_library']['sha256'], entry['sha256'])
        self.assertEqual(pin['ee_library']['profile'], 'progressive')
        self.assertIn('slice232-pcsx2.json', probe.VU0_AOT_MANIFESTS)
        self.assertIn('slice232-pcsx2.json', probe.VU1_AOT_MANIFESTS)


class ProgressiveProbeTests(unittest.TestCase):
    def run_probe(self, root, name, environment):
        def execute(arguments, **kwargs):
            kwargs['stdout'].write(b'[ee] stopped after 100 frames, 1 cycles: frame limit reached\n')
            return subprocess.CompletedProcess(arguments, 0)
        with patch.dict(probe.os.environ, environment, clear=True), \
             patch.object(probe.subprocess, 'run', side_effect=execute) as run:
            result = probe.run_probe(root, root / 'private' / name, 100, 1, [])
        return result, run

    def test_unset_passes_the_on_default_and_records_it(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            probe_tests.ProbeTests().make_workspace(root)
            result, run = self.run_probe(root, 'default', {})
            self.assertEqual(result['progressive_environment'], {'GT4_PROGRESSIVE': '1'})
            self.assertEqual(run.call_args.kwargs['env']['GT4_PROGRESSIVE'], '1')

    def test_zero_is_passed_through_and_recorded(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            probe_tests.ProbeTests().make_workspace(root)
            result, run = self.run_probe(root, 'off', {'GT4_PROGRESSIVE': '0'})
            self.assertEqual(result['progressive_environment'], {'GT4_PROGRESSIVE': '0'})
            self.assertEqual(run.call_args.kwargs['env']['GT4_PROGRESSIVE'], '0')

    def test_boot_switch_is_recorded_and_checked(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            probe_tests.ProbeTests().make_workspace(root)
            result, run = self.run_probe(root, 'boot', {'GT4_PROGRESSIVE_BOOT': '1'})
            self.assertEqual(result['progressive_environment'], {'GT4_PROGRESSIVE': '1', 'GT4_PROGRESSIVE_BOOT': '1'})
            self.assertEqual(run.call_args.kwargs['env']['GT4_PROGRESSIVE_BOOT'], '1')
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            probe_tests.ProbeTests().make_workspace(root)
            with patch.dict(probe.os.environ, {'GT4_PROGRESSIVE_BOOT': 'yes'}, clear=True), \
                 patch.object(probe.subprocess, 'run') as run:
                with self.assertRaisesRegex(ValueError, 'GT4_PROGRESSIVE_BOOT must be 0 or 1'):
                    probe.run_probe(root, root / 'private/unknown', 100, 1, [])
                run.assert_not_called()

    def test_unknown_values_refuse_before_any_output(self):
        for value in ('', 'on', 'true', '2'):
            with tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                probe_tests.ProbeTests().make_workspace(root)
                with patch.dict(probe.os.environ, {'GT4_PROGRESSIVE': value}, clear=True), \
                     patch.object(probe.subprocess, 'run') as run:
                    with self.assertRaisesRegex(ValueError, 'GT4_PROGRESSIVE must be 0 or 1'):
                        probe.run_probe(root, root / 'private/unknown', 100, 1, [])
                    run.assert_not_called()
                self.assertFalse((root / 'private/unknown').exists())


class LauncherRowTests(unittest.TestCase):
    def test_settings_row_and_variable(self):
        settings = source('launcher/crates/gt4l-core/src/settings.rs')
        self.assertRegex(settings, r'id: "progressive",\s+page: Page::Graphics,\s+group: "Image",\s+'
                                   r'title: "Progressive 480p",')
        launch = source('launcher/crates/gt4l-core/src/launch.rs')
        self.assertIn('variables.insert("GT4_PROGRESSIVE", flag(settings.is_on("progressive")));', launch)
        self.assertIn('Step { from: 5, apply: settings_5_to_6 }', source('launcher/crates/gt4l-core/src/migrate.rs'))


def write_ppm(path, rows):
    """rows: list of 448 row colours (r, g, b) or row byte strings; width 640."""
    data = bytearray(b'P6\n640 448\n255\n')
    for row in rows:
        data += row if isinstance(row, (bytes, bytearray)) else bytes(row) * 640
    Path(path).write_bytes(bytes(data))


def textured_row(seed):
    return bytes((seed * 7 + x) % 251 for x in range(640 * 3))


class GateTests(unittest.TestCase):
    def make_run(self, folder, log, frames):
        run = Path(folder)
        (run / 'frames').mkdir(parents=True)
        (run / 'stderr-filtered.log').write_text(log, encoding='utf-8')
        for number, kind in frames.items():
            if kind == 'field':
                rows = [textured_row(pair) for pair in range(224) for _ in range(2)]
            elif kind == 'frame':
                rows = [textured_row(line) for line in range(448)]
            else:
                rows = [(0, 0, 0)] * 448
            write_ppm(run / 'frames' / f'frame_{number:05d}.ppm', rows)
        return run

    # The default: 480i boot, the switch at the first race, menus after it at 480p.
    ON_LOG = ('[progressive] display words 0x00007F08 at vblank 1: menu psm 2, menu row 6, field halving 0x3F800000, '
              'draw-back viewport 0, boot layout 0, monitor row 6\n'
              '[gs] SetGsCrt interlace 1 mode 0x2 field 1\n'
              '[aspect] display request at vblank 2: pending 0 mode 1 row 1; photo/movie no\n'
              '[aspect] display request at vblank 300: pending 1 mode 2 row 6; photo/movie no\n'
              '[gs] SetGsCrt interlace 0 mode 0x50 field 1\n')
    # GT4_PROGRESSIVE_BOOT=1 (experimental): the 480p display state right after boot.
    BOOT_LOG = ('[progressive] display words 0x00007F08 at vblank 1: menu psm 2, menu row 6, field halving 0x3F800000, '
                'draw-back viewport 0, boot layout 1, monitor row 6\n'
                '[gs] SetGsCrt interlace 1 mode 0x2 field 1\n'
                '[aspect] display request at vblank 2: pending 0 mode 2 row 6; photo/movie no\n'
                '[gs] SetGsCrt interlace 0 mode 0x50 field 1\n')
    OFF_LOG = ('[progressive] display words 0x00007F08 at vblank 1: menu psm 1, menu row 0, field halving 0x3F000000, '
               'draw-back viewport 1, boot layout 0, monitor row 1\n'
               '[gs] SetGsCrt interlace 1 mode 0x2 field 1\n'
               '[aspect] display request at vblank 2: pending 0 mode 1 row 1; photo/movie no\n')
    # Boot layout 1 asked for, but the switch only at the first race.
    LATE_LOG = ('[progressive] display words 0x00007F08 at vblank 1: menu psm 2, menu row 6, field halving 0x3F800000, '
                'draw-back viewport 0, boot layout 1, monitor row 6\n'
                '[gs] SetGsCrt interlace 1 mode 0x2 field 1\n'
                '[aspect] display request at vblank 7518: pending 1 mode 2 row 6; photo/movie no\n'
                '[gs] SetGsCrt interlace 0 mode 0x50 field 1\n')

    def test_on_fails_when_the_boot_is_not_480p(self):
        with tempfile.TemporaryDirectory() as folder:
            run = self.make_run(folder, self.LATE_LOG, {7700: 'frame'})
            self.assertIn('the boot did not switch to 480p', ' | '.join(gate.check(run, 'on', boot=True)['failures']))
        with tempfile.TemporaryDirectory() as folder:
            run = self.make_run(folder, self.BOOT_LOG, {100: 'field', 200: 'frame'})
            self.assertEqual(gate.check(run, 'on', boot=True)['failures'], [])
            self.assertIn('boot layout / monitor row (2, 6, 0, 6)', ' | '.join(gate.check(run, 'on')['failures']))

    def test_on_passes_with_full_frames_after_the_switch(self):
        with tempfile.TemporaryDirectory() as folder:
            run = self.make_run(folder, self.ON_LOG, {100: 'field', 200: 'black', 500: 'frame', 600: 'frame'})
            summary = gate.check(run, 'on')
            self.assertEqual(summary['failures'], [])
            self.assertEqual(summary['full_frames'], '2 of 2 informative frames after the switch')

    def test_on_fails_on_fields_a_second_crt_change_or_psm_zero(self):
        with tempfile.TemporaryDirectory() as folder:
            run = self.make_run(folder, self.ON_LOG + '[gs] SetGsCrt interlace 1 mode 0x2 field 1\n'
                                '[aspect] display request at vblank 900: pending 1 mode 0 row 0; photo/movie no\n',
                                {500: 'field', 600: 'field'})
            failures = ' | '.join(gate.check(run, 'on')['failures'])
            self.assertIn('CRT changes after the switch', failures)
            self.assertIn('psm 0', failures)
            self.assertIn('full 448-line frames', failures)

    def test_off_passes_with_fields_and_fails_with_480p(self):
        with tempfile.TemporaryDirectory() as folder:
            run = self.make_run(folder, self.OFF_LOG, {100: 'field', 500: 'field'})
            self.assertEqual(gate.check(run, 'off')['failures'], [])
        with tempfile.TemporaryDirectory() as folder:
            run = self.make_run(folder, self.ON_LOG, {500: 'frame'})
            failures = ' | '.join(gate.check(run, 'off')['failures'])
            self.assertIn('original psm 1 / row 0 / boot layout 0 / monitor row 1', failures)
            self.assertIn('progressive CRT mode', failures)
            self.assertIn('line-doubled fields', failures)

    def test_a_build_without_the_words_fails(self):
        with tempfile.TemporaryDirectory() as folder:
            run = self.make_run(folder, '[gs] SetGsCrt interlace 1 mode 0x2 field 1\n', {100: 'field'})
            self.assertIn('no [progressive] display words line', ' | '.join(gate.check(run, 'off')['failures']))

    def test_compare_counts_identical_dumps(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            a = self.make_run(first, self.OFF_LOG, {100: 'field', 200: 'frame'})
            b = self.make_run(second, self.OFF_LOG, {100: 'field', 200: 'field'})
            result = gate.compare(a, b)
            self.assertEqual((result['identical'], result['different']), (1, ['frame_00200.ppm']))

    def test_doubled_fraction_ignores_flat_rows(self):
        rows = [(9, 9, 9)] * 448
        data = b''.join(bytes(row) * 640 for row in rows)
        self.assertEqual(gate.doubled_fraction(640, 448, data), (1.0, 0))


if __name__ == '__main__':
    unittest.main()
