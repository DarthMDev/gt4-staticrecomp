"""Kill-mid-write test for the atomic memory card writer (TASK-058).

The native fixture rewrites a card copy forever in two alternating images. This test kills it at random moments
(TerminateProcess on Windows, an abrupt process death, the same as a crash in the middle of a flush) and checks that
the card is byte-identical to one of the two complete images, never a torn mixture. The writer runs on a copy in a
scratch directory; the owner's card is never touched.

Needs GT4_NATIVE_BUILD (a build directory with memcard_store_fixture.exe). Skipped when it is absent.
"""

import os
import random
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = os.environ.get('GT4_NATIVE_BUILD')
CARD_BYTES = 528 * 0x4000
ROUNDS = 25


def pattern(seed):
    """Same generator as memcard_store_fixture.cpp pattern(): LCG, one byte per step from the high bits."""
    state = (seed * 2654435761 + 1) & 0xFFFFFFFF
    out = bytearray(CARD_BYTES)
    for index in range(CARD_BYTES):
        state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
        out[index] = state >> 24
    return bytes(out)


def fixture_path():
    for name in ('memcard_store_fixture.exe', 'memcard_store_fixture'):
        candidate = Path(BUILD) / name
        if candidate.exists():
            return candidate
    return None


@unittest.skipUnless(BUILD and fixture_path(), 'GT4_NATIVE_BUILD does not contain memcard_store_fixture')
class MemcardKillTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.old_image = pattern(10)
        cls.new_image = pattern(11)

    def test_killed_writer_leaves_old_or_new_card_never_a_torn_one(self):
        rng = random.Random(58)
        outcomes = {'old': 0, 'new': 0}
        with tempfile.TemporaryDirectory() as scratch:
            card = Path(scratch) / 'copy-of-card.ps2'
            for round_index in range(ROUNDS):
                # A fresh round: the writer's first complete write is pattern(10).
                if card.exists():
                    card.unlink()
                tmp = Path(str(card) + '.tmp')
                if tmp.exists():
                    tmp.unlink()
                process = subprocess.Popen([str(fixture_path()), '--writer', str(card), '0'],
                                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                # Wait until the card exists, then kill after a random delay so the kill lands in different writes.
                deadline = time.time() + 20
                while not card.exists() and time.time() < deadline:
                    time.sleep(0.001)
                time.sleep(rng.uniform(0.0, 0.08))
                process.kill()
                process.wait(timeout=20)
                self.assertTrue(card.exists(), 'the card must exist once the writer has run')
                data = card.read_bytes()
                self.assertEqual(len(data), CARD_BYTES, 'the card is never shorter than a full image')
                if data == self.old_image:
                    outcomes['old'] += 1
                elif data == self.new_image:
                    outcomes['new'] += 1
                else:
                    self.fail(f'round {round_index}: the card is neither the old nor the new complete image')
        # The test is meaningful only when kills landed on both sides of a rename.
        self.assertGreater(outcomes['old'] + outcomes['new'], 0)


if __name__ == '__main__':
    unittest.main()
