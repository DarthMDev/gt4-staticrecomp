"""Checks the derived crash-safe card flush against the pinned donor sio2.cpp (TASK-025, Part A).

Needs the private donor checkout: set GT4_ASSETS_ROOT to the checkout that holds
private/references/ps2-web-recomp (default: this repository). Skipped when it is absent.
"""

import importlib.util
import os
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ASSETS_ROOT = Path(os.environ.get('GT4_ASSETS_ROOT', ROOT)).resolve()
DONOR_SIO2 = ASSETS_ROOT / 'private/references/ps2-web-recomp/runtime/src/iop/sio2.cpp'

spec = importlib.util.spec_from_file_location('prepare_memcard_flush', ROOT / 'scripts/prepare_memcard_flush.py')
flush = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flush)


@unittest.skipUnless(DONOR_SIO2.exists(), 'donor checkout with private/references is not present')
class MemcardFlushOverlayTest(unittest.TestCase):
    def setUp(self):
        self.source = flush.read_donor(ASSETS_ROOT)
        self.derived = flush.apply_overlay(self.source)

    def test_overlay_reverses_to_the_pinned_donor_text(self):
        self.assertEqual(flush.remove_overlay(self.derived), self.source)

    def test_flush_writes_through_the_atomic_store(self):
        self.assertIn('#include "memcard_store.hpp"', self.derived)
        self.assertIn('gt4::memcard::write_card_atomically(g_options.memcard_path, c.data)', self.derived)
        self.assertNotIn('std::ofstream out(g_options.memcard_path', self.derived)

    def test_failed_write_stays_dirty_and_retries(self):
        self.assertIn('c.quiet = 0;', self.derived)
        self.assertIn('void flush_cards_now() {', self.derived)
        self.assertIn('if (g_card.present && g_card.dirty) write_card_image();', self.derived)

    def test_derived_source_has_no_crlf_and_one_flush_definition(self):
        self.assertNotIn('\r', self.derived)
        self.assertEqual(self.derived.count('void flush_cards() {'), 1)

    def test_output_must_stay_inside_private(self):
        with tempfile.TemporaryDirectory() as scratch:
            with self.assertRaises(ValueError):
                flush.prepare(Path(scratch) / 'out', ASSETS_ROOT)

    def test_prepare_writes_the_derived_file(self):
        output = ASSETS_ROOT / 'private' / 'slice180-saves-test-scratch'
        try:
            flush.prepare(output, ASSETS_ROOT)
            self.assertEqual((output / 'sio2.cpp').read_text(encoding='utf-8'), self.derived)
        finally:
            for path in sorted(output.glob('*'), reverse=True):
                path.unlink()
            if output.exists():
                output.rmdir()


if __name__ == '__main__':
    unittest.main()
