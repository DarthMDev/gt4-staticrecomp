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
