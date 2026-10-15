"""Authored source/control capsules; no private game input is consumed."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import prepare_perf_runtime as producer


class PerformanceDerivationTests(unittest.TestCase):
    def fixture(self, root):
        donor = root / 'private/references/ps2-web-recomp/runtime/src'
        donor.mkdir(parents=True)
        pins = {}
        for name, sites in producer.PATCHES.items():
            source = '\n'.join(before for before, after in sites)
            data = source.encode()
            (donor / name).write_bytes(data)
            pins[name] = hashlib.sha256(data).hexdigest()
        return donor, pins

    def test_fresh_capsule_inverse_and_receipt(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            donor, pins = self.fixture(root)
            with patch.object(producer, 'ROOT', root), patch.object(producer, 'PINS', pins):
                output = root / 'private/perf'
                receipt = producer.prepare(output)
                self.assertEqual(set(p.name for p in output.iterdir()),
                                 {'system.cpp', 'vu_micro.cpp', 'vif.cpp', 'runtime.json'})
                self.assertEqual(receipt, json.loads((output / 'runtime.json').read_text()))
                for name in pins:
                    text = (output / name).read_text().split('#include "perf_probe.hpp"\n', 1)[1]
                    for before, after in reversed(producer.PATCHES[name]): text = text.replace(after, before)
                    self.assertEqual(text, (donor / name).read_text())
                with self.assertRaisesRegex(ValueError, 'Preserve'):
                    producer.prepare(output)

    def test_bad_pin_and_nonprivate_paths_leave_no_output(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            donor, pins = self.fixture(root)
            with patch.object(producer, 'ROOT', root), patch.object(producer, 'PINS', pins):
                for output in (root / 'public', root / 'private', root / 'private/nested/perf'):
                    with self.assertRaises(ValueError): producer.prepare(output)
                (donor / 'system.cpp').write_bytes(b'mutated')
                with self.assertRaisesRegex(ValueError, 'differs'): producer.prepare(root / 'private/perf')
                self.assertFalse((root / 'private/perf').exists())

    def test_unit_split_is_explicit_and_only_replaces_the_vu_timer(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            donor, pins = self.fixture(root)
            with patch.object(producer, 'ROOT', root), patch.object(producer, 'PINS', pins):
                legacy = root / 'private/legacy'
                split = root / 'private/split'
                old_receipt = producer.prepare(legacy)
                new_receipt = producer.prepare(split, split_vu=True)
            self.assertNotIn('vu_scope_profile', old_receipt)
            self.assertEqual(new_receipt['vu_scope_profile'], 'unit_split_run_fast_only')
            for name in ('system.cpp', 'vif.cpp'):
                self.assertEqual((legacy/name).read_bytes(), (split/name).read_bytes())
            old = (legacy/'vu_micro.cpp').read_text()
            new = (split/'vu_micro.cpp').read_text()
            self.assertEqual(new.count('gt4::perf::vu_interpreter_category(unit)'), 1)
            self.assertEqual(new.replace('gt4::perf::vu_interpreter_category(unit)',
                                         'gt4::perf::Category::vu_interpret'), old)

    def test_missing_or_duplicate_site_rejected(self):
        for source in ('no match', '    run_fast(unit, pc);\n    run_fast(unit, pc);'):
            with self.assertRaisesRegex(ValueError, 'one qualified'):
                producer.derive('vu_micro.cpp', source)


class DefaultDerivationPinTests(unittest.TestCase):
    """Default and split derivations must stay byte-identical after the VU1 AOT option was added."""
    PINNED = {
        'default': {'system.cpp': '7ca58519c08cba6461ee782127ff04c0206fdbdc5b548f5ddf0e1175f0f7fb40',
                    'vu_micro.cpp': 'da9f08b5f3e6fd9e671f9d628b216218da218f84a179f225c796e9f74ab2628d',
                    'vif.cpp': '6449989a2a39c58990beea1d06aba9f61238c53329adf73d1859e54177db5223'},
        'split': {'system.cpp': '7ca58519c08cba6461ee782127ff04c0206fdbdc5b548f5ddf0e1175f0f7fb40',
                  'vu_micro.cpp': 'fedbd1a5e77673d9a493eb000aed28479d99e9abcc6fa1931fbf96c24680902e',
                  'vif.cpp': '6449989a2a39c58990beea1d06aba9f61238c53329adf73d1859e54177db5223'},
    }

    def derived(self, **options):
        donor = Path(producer.ROOT) / 'private/references/ps2-web-recomp/runtime/src'
        if not donor.exists():
            self.skipTest('donor checkout is not present')
        result = {}
        for name in producer.PINS:
            text = (donor / name).read_bytes().decode('utf-8').replace('\r\n', '\n')
            result[name] = hashlib.sha256(producer.derive(name, text, **options).encode('utf-8')).hexdigest()
        return result

    def test_default_and_split_match_recorded_hashes(self):
        default = self.derived()
        split = self.derived(split_vu=True)
        self.assertEqual(split['vu_micro.cpp'], self.PINNED['split']['vu_micro.cpp'])
        self.assertEqual(split['system.cpp'], self.PINNED['split']['system.cpp'])
        self.assertEqual(split['vif.cpp'], self.PINNED['split']['vif.cpp'])
        self.assertEqual(default['system.cpp'], self.PINNED['default']['system.cpp'])
        self.assertEqual(default['vif.cpp'], self.PINNED['default']['vif.cpp'])
        self.assertEqual(default['vu_micro.cpp'], self.PINNED['default']['vu_micro.cpp'])
        self.assertNotEqual(default['vu_micro.cpp'], split['vu_micro.cpp'])

    def test_aot_option_changes_only_vu_micro_and_is_reversible(self):
        base = self.derived(split_vu=True)
        aot = self.derived(split_vu=True, vu1_aot=True)
        self.assertEqual(aot['system.cpp'], base['system.cpp'])
        self.assertEqual(aot['vif.cpp'], base['vif.cpp'])
        self.assertNotEqual(aot['vu_micro.cpp'], base['vu_micro.cpp'])

    def test_aot_overlay_sections_cover_every_anchor(self):
        sections = producer.overlay_sections(producer.OVERLAY_PATH.read_text(encoding='utf-8'))
        self.assertEqual(set(sections), set(producer.OVERLAY_ANCHORS))
        for name, text in sections.items():
            self.assertTrue(text.strip(), name)


class GsCrtOverrideTests(unittest.TestCase):
    """Slice156 hw.cpp hook: one anchored hook in gs_set_crt; default derivations unchanged."""
    DONOR_HW = ('void other() {}\n'
                'void gs_set_crt(std::uint32_t interlace, std::uint32_t mode, std::uint32_t field) {\n'
                '    g_crt[0] = interlace;\n'
                '}\n')

    def test_hook_is_added_only_to_gs_set_crt_and_reverses_exactly(self):
        derived = producer.derive_crt(self.DONOR_HW)
        self.assertTrue(derived.startswith(producer.CRT_PREFIX))
        body = derived[len(producer.CRT_PREFIX):]
        self.assertEqual(body.count('apply_crt_override'), 1)
        self.assertEqual(body.replace(producer.CRT_REPLACEMENT, producer.CRT_ANCHOR), self.DONOR_HW)

    def test_missing_or_duplicate_anchor_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'one qualified'):
            producer.derive_crt('no gs_set_crt here')
        with self.assertRaisesRegex(ValueError, 'one qualified'):
            producer.derive_crt(self.DONOR_HW + self.DONOR_HW)

    def test_override_capsule_adds_hw_and_default_capsule_does_not(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            donor, pins = PerformanceDerivationTests().fixture(root)
            (donor / 'hw.cpp').write_bytes(self.DONOR_HW.encode())
            crt_pins = {'hw.cpp': hashlib.sha256(self.DONOR_HW.encode()).hexdigest()}
            with patch.object(producer, 'ROOT', root), patch.object(producer, 'PINS', pins), \
                    patch.object(producer, 'CRT_PINS', crt_pins):
                default = producer.prepare(root / 'private/default')
                self.assertNotIn('hw.cpp', {p.name for p in (root / 'private/default').iterdir()})
                self.assertNotIn('gs_crt_override_profile', default)
                receipt = producer.prepare(root / 'private/crt', gs_crt_override=True)
            self.assertEqual({p.name for p in (root / 'private/crt').iterdir()},
                             {'system.cpp', 'vu_micro.cpp', 'vif.cpp', 'hw.cpp', 'runtime.json'})
            self.assertEqual(receipt['gs_crt_override_profile'], 'overlay-v1')
            self.assertEqual(receipt['source_sha256']['hw.cpp'],
                             hashlib.sha256((root / 'private/crt/hw.cpp').read_bytes()).hexdigest())
            for name in ('system.cpp', 'vu_micro.cpp', 'vif.cpp'):
                self.assertEqual((root / 'private/crt' / name).read_bytes(),
                                 (root / 'private/default' / name).read_bytes())

    def test_changed_hw_source_is_rejected_without_output(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            donor, pins = PerformanceDerivationTests().fixture(root)
            (donor / 'hw.cpp').write_bytes(b'mutated hw')
            with patch.object(producer, 'ROOT', root), patch.object(producer, 'PINS', pins), \
                    patch.object(producer, 'CRT_PINS', {'hw.cpp': hashlib.sha256(self.DONOR_HW.encode()).hexdigest()}):
                with self.assertRaisesRegex(ValueError, 'differs'):
                    producer.prepare(root / 'private/crt', gs_crt_override=True)
            self.assertFalse((root / 'private/crt').exists())


class Slice156CapsuleTests(unittest.TestCase):
    """Real-file controls; skipped when the private capsules or donor are not present."""

    def test_slice156_unchanged_sources_are_byte_identical_to_slice155(self):
        old = producer.ROOT / 'private/slice155-vu1-full-runtime'
        new = producer.ROOT / 'private/slice156-quickfix-runtime'
        if not (old / 'runtime.json').exists() or not (new / 'runtime.json').exists():
            self.skipTest('slice155 or slice156 capsule not present')
        for name in ('system.cpp', 'vu_micro.cpp', 'vif.cpp'):
            self.assertEqual((old / name).read_bytes(), (new / name).read_bytes(), name)
        self.assertTrue((new / 'hw.cpp').exists())

    def test_default_derivation_reproduces_slice155_capsule(self):
        old = producer.ROOT / 'private/slice155-vu1-full-runtime'
        donor = producer.ROOT / 'private/references/ps2-web-recomp/runtime/src'
        if not (old / 'runtime.json').exists() or not donor.exists():
            self.skipTest('slice155 capsule or donor not present')
        for name in ('system.cpp', 'vu_micro.cpp', 'vif.cpp'):
            text = (donor / name).read_bytes().decode('utf-8').replace('\r\n', '\n')
            derived = producer.derive(name, text, split_vu=True, vu1_aot=True)
            self.assertEqual(derived.encode('utf-8'), (old / name).read_bytes(), name)
