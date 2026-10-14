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

    def test_missing_or_duplicate_site_rejected(self):
        for source in ('no match', '    run_fast(unit, pc);\n    run_fast(unit, pc);'):
            with self.assertRaisesRegex(ValueError, 'one qualified'):
                producer.derive('vu_micro.cpp', source)
