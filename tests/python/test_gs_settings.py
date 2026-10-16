"""TASK-039: the GS settings table, gt4-gs.ini generation and allow-list (docs/GS-SETTINGS.md).

Pure checks: they read docs/launcher/gs-settings.toml and need no game build, assets or GPU.
"""
import copy
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import external_runtime_probe as probe  # noqa: E402
import gs_settings  # noqa: E402
import play_performance as performance  # noqa: E402

# The TASK-036 default INI (Renderer Vulkan, 3x, blending High, deinterlace Automatic, 16:9, vsync), byte for byte
# as the pre-TASK-039 writer produced it. Written with the platform newline by the file writer.
LEGACY_DEFAULT = ('; gt4-gs.ini written by scripts/external_runtime_probe.py (TASK-036 minimal settings)\n'
                  '[EmuCore/GS]\n'
                  'Renderer = 14\n'
                  'upscale_multiplier = 3\n'
                  'accurate_blending_unit = 3\n'
                  'deinterlace_mode = 0\n'
                  'AspectRatio = 16:9\n'
                  'VsyncEnable = true\n'
                  'DisableMailboxPresentation = true\n')
LEGACY_CACHE = LEGACY_DEFAULT + '\n[Folders]\nCache = C:/cache\n'
LEGACY_HEADLESS = (LEGACY_DEFAULT + '\n[GT4GS]\nSurfaceless = true\nDumpDir = C:/run/gs-frames\n'
                   'DumpEvery = 100\n')
REQUIRED = ('id', 'label', 'help', 'page', 'section', 'key', 'type', 'default', 'visibility', 'written', 'status')


def rows():
    return gs_settings.load_table()


def by_id():
    return gs_settings.rows_by_id(rows())


class TableSchemaTests(unittest.TestCase):
    def test_table_loads_and_has_one_row_per_setting(self):
        table = rows()
        self.assertEqual(len(table), 46)
        self.assertEqual(len({row['id'] for row in table}), 46)

    def test_every_row_has_the_documented_columns(self):
        for row in rows():
            for field in REQUIRED:
                self.assertIn(field, row, row['id'])
            self.assertIn(row['page'], gs_settings.PAGES)
            self.assertIn(row['type'], gs_settings.TYPES)
            self.assertIn(row['visibility'], gs_settings.VISIBILITIES)
            self.assertIn(row['written'], gs_settings.WRITTEN)
            self.assertRegex(row['status'], gs_settings.STATUS_PATTERN.pattern)

    def test_settable_rows_name_their_ini_key(self):
        for row in rows():
            if row['status'] == 'ok' and row['written'] != 'never':
                self.assertTrue(row['section'] and row['key'], row['id'])

    def test_ini_keys_are_unique(self):
        pairs = [(row['section'], row['key']) for row in rows() if row['section'] and row['key']]
        self.assertEqual(len(pairs), len(set(pairs)))

    def test_launcher_only_readback_row(self):
        row = by_id()['gt4_readback']
        self.assertEqual(row['written'], 'never')
        self.assertEqual(row['env'], 'GT4_GS_READBACK')
        self.assertEqual(row['default'], 'accurate')
        self.assertEqual([choice['name'] for choice in row['choices']], ['accurate', 'unsynchronized'])
        self.assertEqual(row['status'], 'pending-TASK-037')

    def test_validator_refuses_bad_tables(self):
        good = {'schema_version': 1, 'setting': copy.deepcopy(rows())}
        self.assertEqual(gs_settings.table_errors(good), [])
        cases = {
            'wrong schema': lambda data: data.update(schema_version=2),
            'duplicate id': lambda data: data['setting'].append(copy.deepcopy(data['setting'][0])),
            'bad page': lambda data: data['setting'][0].update(page='Audio'),
            'bad type': lambda data: data['setting'][0].update(type='double'),
            'bad status': lambda data: data['setting'][0].update(status='maybe'),
            'settable row without key': lambda data: data['setting'][0].update(key=''),
            'duplicate INI key': lambda data: data['setting'][1].update(
                section=data['setting'][0]['section'], key=data['setting'][0]['key']),
            'int range inverted': lambda data: data['setting'][1].update(min=9, max=8),
            'default outside range': lambda data: data['setting'][1].update(default='9'),
            'always row not ok': lambda data: data['setting'][2].update(written='always'),
            'path row set by user': lambda data: next(
                row.update(written='when-set') for row in data['setting'] if row['type'] == 'path'),
            'bool default': lambda data: next(
                row.update(default='yes') for row in data['setting'] if row['type'] == 'bool'),
            'launcher-only without env': lambda data: next(
                row.pop('env') for row in data['setting'] if row['written'] == 'never'),
        }
        for name, break_table in cases.items():
            data = copy.deepcopy(good)
            break_table(data)
            self.assertTrue(gs_settings.table_errors(data), name)


class DefaultTests(unittest.TestCase):
    def test_default_ini_is_the_task_036_text(self):
        self.assertEqual(gs_settings.render_ini(rows()), LEGACY_DEFAULT)

    def test_probe_default_is_unchanged(self):
        self.assertEqual(probe.gs_config_text({}), LEGACY_DEFAULT)
        self.assertEqual(probe.gs_config_text({'GT4_GS_SCALE': '3', 'GT4_PRESENT_MODE': 'fifo',
                                               'GT4_GS_ASPECT': '16:9'}), LEGACY_DEFAULT)

    def test_cache_and_headless_sections_are_unchanged(self):
        self.assertEqual(probe.gs_config_text({}, None, 'C:/cache'), LEGACY_CACHE)
        self.assertEqual(probe.gs_config_text({}, Path('C:/run/gs-frames'), None),
                         LEGACY_DEFAULT + '\n[GT4GS]\nSurfaceless = true\nDumpDir = C:\\run\\gs-frames\n'
                         'DumpEvery = 100\n')
        self.assertEqual(gs_settings.render_ini(rows(), {}, {'cache_folder': 'C:/cache',
                                                             'headless_surfaceless': 'true',
                                                             'headless_dump_dir': 'C:/run/gs-frames',
                                                             'headless_dump_every': '100'}),
                         LEGACY_DEFAULT + '\n[Folders]\nCache = C:/cache\n\n[GT4GS]\nSurfaceless = true\n'
                         'DumpDir = C:/run/gs-frames\nDumpEvery = 100\n')

    def test_default_file_bytes_match_the_old_writer(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'gt4-gs.ini'
            gs_settings.write_ini_atomic(target, probe.gs_config_text({}, None, 'C:/cache'))
            # The old writer used Path.write_text, which applies the platform newline (CRLF on Windows).
            self.assertEqual(target.read_bytes(), (LEGACY_CACHE.replace('\n', os.linesep)).encode('utf-8'))


class ValueTests(unittest.TestCase):
    def test_bool_accepts_only_true_or_false(self):
        self.assertEqual(gs_settings.parse_value(by_id()['fxaa'], 'true'), 'true')
        self.assertEqual(gs_settings.parse_value(by_id()['fxaa'], True), 'true')
        for value in ('yes', '1', 'True'):
            with self.assertRaises(gs_settings.SettingsError, msg=value):
                gs_settings.parse_value(by_id()['fxaa'], value)

    def test_int_range_is_checked(self):
        upscale = by_id()['upscale']
        self.assertEqual(gs_settings.parse_value(upscale, '1'), '1')
        self.assertEqual(gs_settings.parse_value(upscale, '08'), '8')
        for value in ('0', '9', '1.5', 'x', ' 3', ''):
            with self.assertRaises(gs_settings.SettingsError, msg=value):
                gs_settings.parse_value(upscale, value)

    def test_enum_accepts_only_listed_settable_names(self):
        aspect = by_id()['aspect']
        self.assertEqual(gs_settings.ini_value(aspect, gs_settings.parse_value(aspect, 'auto')), 'Auto 4:3/3:2')
        self.assertEqual(gs_settings.ini_value(aspect, gs_settings.parse_value(aspect, '4:3')), '4:3')
        with self.assertRaises(gs_settings.SettingsError):
            gs_settings.parse_value(aspect, 'wide')

    def test_tbd_choices_and_rows_are_refused(self):
        renderer = by_id()['renderer']
        with self.assertRaisesRegex(gs_settings.SettingsError, 'not settable yet'):
            gs_settings.parse_value(renderer, 'd3d12')
        with self.assertRaisesRegex(gs_settings.SettingsError, 'not settable yet'):
            gs_settings.parse_value(by_id()['filter'], 'bilinear-ps2')
        with self.assertRaisesRegex(gs_settings.SettingsError, 'not settable yet'):
            gs_settings.parse_value(by_id()['gt4_readback'], 'unsynchronized')

    def test_path_values_are_single_line(self):
        self.assertEqual(gs_settings.parse_value(by_id()['cache_folder'], 'C:/cache'), 'C:/cache')
        with self.assertRaises(gs_settings.SettingsError):
            gs_settings.parse_value(by_id()['cache_folder'], 'C:/a\nCache = x')

    def test_unknown_ids_are_refused(self):
        with self.assertRaisesRegex(gs_settings.SettingsError, "unknown GS setting 'nope'"):
            gs_settings.settings_from_assignments(['nope=1'], rows())
        with self.assertRaisesRegex(gs_settings.SettingsError, 'unknown GS setting'):
            gs_settings.render_ini(rows(), {'nope': '1'})

    def test_launcher_rows_are_not_user_settable(self):
        for identifier in ('gt4_readback', 'cache_folder', 'headless_dump_dir'):
            with self.assertRaisesRegex(gs_settings.SettingsError, 'set by the launcher'):
                gs_settings.settings_from_assignments([f'{identifier}=accurate'], rows())
        with self.assertRaisesRegex(gs_settings.SettingsError, 'not a launcher row'):
            gs_settings.render_ini(rows(), {}, {'upscale': '2'})

    def test_assignment_syntax(self):
        with self.assertRaisesRegex(gs_settings.SettingsError, 'ID=VALUE'):
            gs_settings.settings_from_assignments(['upscale'], rows())
        with self.assertRaisesRegex(gs_settings.SettingsError, 'twice'):
            gs_settings.settings_from_assignments(['upscale=2', 'upscale=3'], rows())


class IniTests(unittest.TestCase):
    def test_user_settings_replace_defaults_and_add_when_set_rows(self):
        settings = gs_settings.settings_from_assignments(['upscale=6', 'fxaa=true', 'aspect=4:3'], rows())
        text = gs_settings.render_ini(rows(), settings)
        self.assertEqual(text.splitlines(), [
            gs_settings.INI_HEADER, '[EmuCore/GS]', 'Renderer = 14', 'upscale_multiplier = 6',
            'accurate_blending_unit = 3', 'deinterlace_mode = 0', 'AspectRatio = 4:3', 'VsyncEnable = true',
            'DisableMailboxPresentation = true', 'FXAA = true'])

    def test_sections_follow_the_fixed_order(self):
        settings = {'game_fixes': 'true', 'upscale': '2'}
        text = gs_settings.render_ini(rows(), settings, {'cache_folder': 'C:/cache'})
        headers = [line for line in text.splitlines() if line.startswith('[')]
        self.assertEqual(headers, ['[EmuCore/GS]', '[EmuCore]', '[Folders]'])

    def test_json_settings_round_trip_and_are_checked(self):
        settings = gs_settings.settings_from_assignments(['upscale=2', 'fxaa=true'], rows())
        text = gs_settings.settings_to_json(settings)
        self.assertEqual(gs_settings.settings_from_json(text, rows()), settings)
        with self.assertRaisesRegex(gs_settings.SettingsError, 'not JSON'):
            gs_settings.settings_from_json('{', rows())
        with self.assertRaisesRegex(gs_settings.SettingsError, 'JSON object'):
            gs_settings.settings_from_json('[1]', rows())
        with self.assertRaisesRegex(gs_settings.SettingsError, 'must be from 1 to 8'):
            gs_settings.settings_from_json('{"upscale": 9}', rows())


class AtomicWriteTests(unittest.TestCase):
    def test_write_replaces_the_file_and_leaves_no_temporary(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'session' / 'gt4-gs.ini'
            gs_settings.write_ini_atomic(target, 'first\n')
            gs_settings.write_ini_atomic(target, 'second\n')
            self.assertEqual(target.read_text(encoding='utf-8'), 'second\n')
            self.assertEqual([path.name for path in target.parent.iterdir()], ['gt4-gs.ini'])

    def test_failed_replace_keeps_the_old_file_and_removes_the_temporary(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'gt4-gs.ini'
            gs_settings.write_ini_atomic(target, 'old\n')
            with patch.object(gs_settings.os, 'replace', side_effect=OSError('locked')):
                with self.assertRaises(OSError):
                    gs_settings.write_ini_atomic(target, 'new\n')
            self.assertEqual(target.read_text(encoding='utf-8'), 'old\n')
            self.assertEqual([path.name for path in Path(temporary).iterdir()], ['gt4-gs.ini'])


class AllowListTests(unittest.TestCase):
    def test_allow_list_is_stable_and_committed(self):
        text = gs_settings.allowlist_text(rows())
        self.assertEqual(text, gs_settings.allowlist_text(rows()))
        self.assertEqual((ROOT / 'docs/launcher/gs-settings-allowlist.json').read_text(encoding='utf-8'), text)

    def test_allow_list_pairs_are_sorted_unique_and_named(self):
        entries = json.loads(gs_settings.allowlist_text(rows()))['entries']
        pairs = [(entry['section'], entry['key']) for entry in entries]
        self.assertEqual(pairs, sorted(pairs))
        self.assertEqual(len(pairs), len(set(pairs)))
        self.assertIn(('EmuCore/GS', 'upscale_multiplier'), pairs)
        self.assertIn(('GT4GS', 'DumpDir'), pairs)
        self.assertNotIn(('', ''), pairs)
        self.assertFalse(any(key == 'GT4_GS_READBACK' for _, key in pairs))


class OfferedTests(unittest.TestCase):
    def test_launcher_offers_only_settable_options(self):
        offered = {row['id'] for row in gs_settings.offered(rows())}
        self.assertTrue({'renderer', 'upscale', 'aspect', 'vsync', 'deinterlace', 'blending', 'fxaa'} <= offered)
        for identifier in ('filter', 'cas_mode', 'cas_sharpness', 'gt4_readback', 'texture_load', 'adapter',
                           'cache_folder', 'headless_dump_dir', 'manual_hacks'):
            self.assertNotIn(identifier, offered, identifier)
        for row in gs_settings.offered(rows()):
            if row['type'] == 'enum':
                self.assertTrue(gs_settings.offered_choices(row), row['id'])
        self.assertEqual(gs_settings.offered_choices(by_id()['renderer']), ['vulkan'])


class PlayPerformanceTests(unittest.TestCase):
    def run_main(self, arguments):
        with patch.object(performance, 'play') as play, \
                patch.object(sys, 'argv', ['play_performance.py', '--assets-root', 'C:/assets'] + arguments):
            code = performance.main()
        return code, play

    def test_no_options_leave_the_settings_variable_unset(self):
        with patch.dict(os.environ, {'GT4_GS_SETTINGS': '{"upscale": 5}'}):
            code, play = self.run_main(['--mode', 'pcsx2'])
            self.assertEqual(code, 0)
            play.assert_called_once()
            self.assertNotIn('GT4_GS_SETTINGS', os.environ)

    def test_gs_setting_is_checked_and_passed_as_json(self):
        with patch.dict(os.environ, {}, clear=False):
            code, play = self.run_main(['--mode', 'pcsx2', '--gs-setting', 'upscale=2',
                                        '--gs-setting', 'fxaa=true'])
            self.assertEqual(code, 0)
            play.assert_called_once()
            self.assertEqual(json.loads(os.environ['GT4_GS_SETTINGS']), {'fxaa': 'true', 'upscale': '2'})

    def test_gs_setting_refusals(self):
        for arguments in (['--mode', 'main', '--gs-setting', 'upscale=2'],
                          ['--mode', 'pcsx2', '--gs-setting', 'nope=1'],
                          ['--mode', 'pcsx2', '--gs-setting', 'upscale=9'],
                          ['--mode', 'pcsx2', '--gs-setting', 'renderer=d3d12'],
                          ['--mode', 'pcsx2', '--gs-setting', 'gt4_readback=accurate'],
                          ['--mode', 'pcsx2', '--gs-setting', 'upscale=2', '--gs-scale', '3']):
            with patch.dict(os.environ, {}, clear=False):
                code, play = self.run_main(arguments)
            self.assertEqual(code, 1, arguments)
            play.assert_not_called()

    def test_gs_scale_is_the_upscale_row(self):
        with patch.dict(os.environ, {}, clear=False):
            code, play = self.run_main(['--mode', 'pcsx2', '--gs-scale', '1'])
            self.assertEqual(code, 0)
            self.assertEqual(os.environ['GT4_GS_SCALE'], '1')
            settings = probe.gs_settings_from_environment(os.environ, rows())
            self.assertEqual(settings['upscale'], '1')


class ProbeEnvironmentTests(unittest.TestCase):
    def test_settings_json_reaches_the_ini(self):
        text = probe.gs_config_text({'GT4_GS_SETTINGS': json.dumps({'upscale': 2, 'fxaa': True})})
        self.assertIn('upscale_multiplier = 2\n', text)
        self.assertIn('[EmuCore/GS]\n', text)
        self.assertIn('FXAA = true\n', text)

    def test_shortcut_and_json_conflicts_are_refused(self):
        with self.assertRaisesRegex(ValueError, 'both set upscale'):
            probe.gs_config_text({'GT4_GS_SCALE': '2', 'GT4_GS_SETTINGS': '{"upscale": 3}'})
        with self.assertRaisesRegex(ValueError, 'GT4_PRESENT_MODE=mailbox'):
            probe.gs_config_text({'GT4_PRESENT_MODE': 'mailbox', 'GT4_GS_SETTINGS': '{"vsync": false}'})
        with self.assertRaisesRegex(ValueError, 'GT4_GS_ASPECT must be one of'):
            probe.gs_config_text({'GT4_GS_ASPECT': 'wide'})
        with self.assertRaisesRegex(ValueError, 'GT4_GS_SCALE must be an integer from 1 to 8'):
            probe.gs_config_text({'GT4_GS_SCALE': '9'})
        with self.assertRaisesRegex(ValueError, 'GT4_PRESENT_MODE must be one of'):
            probe.gs_config_text({'GT4_PRESENT_MODE': 'adaptive'})

    def test_present_mode_maps_to_vsync_and_mailbox(self):
        self.assertIn('VsyncEnable = false\n', probe.gs_config_text({'GT4_PRESENT_MODE': 'immediate'}))
        self.assertIn('DisableMailboxPresentation = false\n', probe.gs_config_text({'GT4_PRESENT_MODE': 'mailbox'}))


if __name__ == '__main__':
    unittest.main()
