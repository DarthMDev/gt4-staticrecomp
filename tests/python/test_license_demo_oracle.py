"""Controls for the license demo cipher oracle (TASK-017).

Synthetic data only, except the last class, which reads the pinned disc when it is
present and skips cleanly otherwise. Nothing here writes game data.
"""

from pathlib import Path
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import license_demo_oracle as oracle


class TableTests(unittest.TestCase):
    def test_tables_match_the_elf_values(self):
        # Spot values read from the reconstructed ELF at 0x00655888 / 0x00655C90 / 0x00655358.
        self.assertEqual(oracle.FT[0], 0x3F804010)
        self.assertEqual(oracle.FT[1], 0x3F80C030)
        self.assertEqual(oracle.FT[32], 0x3F904411)
        self.assertEqual(oracle.FT[255], 0x3FFFDFF7)
        self.assertEqual(bytes(oracle.T8[:8]), bytes.fromhex("008040c020a060e0"))
        self.assertEqual(oracle.CRC_TABLE[1], 0x77073096)
        data = bytes(range(256)) * 3
        self.assertEqual(oracle.crc32(data), zlib.crc32(data))


class HardwareVectorTests(unittest.TestCase):
    """Vectors from ps2autotests tests/cpu/ee_fpu/muldiv.expected (ISC, real PS2 output)."""

    def test_booth_multiplier_matches_operand_order_vectors(self):
        for dropped in (1, 8, 16, 22):
            mul = oracle.make_mul_booth(dropped)
            self.assertEqual(mul(0x3F800000, 0x3FFFFFFF), 0x3FFFFFFE)
            self.assertEqual(mul(0x3FFFFFFF, 0x3F800000), 0x3FFFFFFF)
            self.assertEqual(mul(0x3F800000, 0x7FFFFFFF), 0x7FFFFFFE)
            self.assertEqual(mul(0x40400000, 0x40400000), 0x41100000)
            self.assertEqual(mul(0x7FFFFFFF, 0x7FFFFFFF), 0x7FFFFFFF)
        # Exact chop (donor fp::mul) misses the operand-order vector.
        self.assertEqual(oracle.mul_chop(0x3F800000, 0x3FFFFFFF), 0x3FFFFFFF)
        self.assertEqual(oracle.make_mul_booth(23)(0x3FFFFFFF, 0x3F800000), 0x3FFFFFFE)

    def test_divide_vectors(self):
        self.assertEqual(oracle.div_chop(0x3FFFFFFF, 0x3F800000), 0x3FFFFFFF)
        # Hardware: div 1.0, 0x3FFFFFFF = 0x3F000001; exact chop gives 0x3F000000.
        self.assertEqual(oracle.div_chop(0x3F800000, 0x3FFFFFFF), 0x3F000000)
        self.assertEqual(oracle.div_nearest(0x3F800000, 0x3FFFFFFF), 0x3F000001)
        for div in oracle.DIV_MODELS.values():
            self.assertEqual(div(0x3F800000, 0x00000000), 0x7FFFFFFF)
            self.assertEqual(div(0x00000000, 0x3F800000), 0x00000000)
            self.assertEqual(div(0x7FFFFFFF, 0x7FFFFFFF), 0x3F800000)
            self.assertEqual(div(0x40400000, 0x3F800000), 0x40400000)

    def test_candidate_divides_stay_within_one_ulp_of_chop(self):
        state = 0x12345678
        for _ in range(3000):
            state = (state * 1103515245 + 12345) & 0xFFFFFFFF
            a = 0x3F800000 | (state & 0x7FFFFF)
            state = (state * 1103515245 + 12345) & 0xFFFFFFFF
            b = 0x3F000000 | (state & 0xFFFFFF)
            chop = oracle.div_chop(a, b)
            for name, div in oracle.DIV_MODELS.items():
                with self.subTest(name=name, a=hex(a), b=hex(b)):
                    self.assertIn(div(a, b) - chop, (0, 1))


class CipherTests(unittest.TestCase):
    plain = (b"\x00" * 64 + b"RaceLicense" + bytes(range(40)) + b"\x00" * 400)

    def test_round_trip_and_model_sensitivity(self):
        key, cipher = oracle.encrypt(self.plain, oracle.mul_chop, oracle.DIV_MODELS["srt2-cs-t3"])
        self.assertEqual(key >> 24, self.plain[0])
        decrypted = oracle.decrypt(cipher, key, oracle.mul_chop, oracle.DIV_MODELS["srt2-cs-t3"])
        self.assertEqual(decrypted, self.plain)
        self.assertTrue(oracle.crc_ok(decrypted, key))
        # One ulp differences early in the chain change the key stream: chop fails the CRC.
        self.assertFalse(oracle.crc_ok(oracle.decrypt(cipher, key, oracle.mul_chop, oracle.div_chop), key))

    def test_state_saturates_into_the_period_two_stream(self):
        key, cipher = oracle.encrypt(self.plain, oracle.mul_chop, oracle.div_chop)
        state = oracle.initial_state(key)
        for value in cipher:
            state = oracle.div_chop(oracle.mul_chop(oracle.FT[value], state), oracle.divisor_of(state))
        self.assertIn(state, (0x7FFFFFFF, 0x7F800000))
        for value in range(256):
            following = oracle.div_chop(oracle.mul_chop(oracle.FT[value], 0x7FFFFFFF), 0x3FFFFFFF)
            self.assertEqual(following, 0x7F800000)

    def test_search_recovers_a_model_free_chain(self):
        hardware_stand_in = oracle.DIV_MODELS["srt2-cs-t3"]
        # Zero-heavy plaintext, like the demo bodies, so the byte prior can steer the beam.
        plain = bytes([7]) + bytes(31) + b"GT4M" + bytes(600)
        key, cipher = oracle.encrypt(plain, oracle.mul_chop, hardware_stand_in)
        states = oracle.search_chain(cipher, key, 300)
        self.assertIsNotNone(states)
        # The confirmed chain decrypts to the plaintext and agrees with the stand-in step by step.
        for _, _, divisor, product, quotient in oracle.chain_samples(cipher, key, states):
            self.assertEqual(hardware_stand_in(product, divisor), quotient)
        self.assertGreater(len(states), 200)


class LayoutTests(unittest.TestCase):
    @staticmethod
    def body(count=0, frames=0x5BF, size=0x10E, padding=2):
        head = bytes(oracle.MANAGER_OFFSET) + struct.pack("<3I", count, frames, size)
        return head + bytes(size - 8 + padding)

    def test_single_recorder_layout(self):
        good = self.body()
        self.assertEqual(len(good), 0x1BB8)  # replay/license/usl0b0001 body size
        self.assertTrue(oracle.layout_sane(good))
        self.assertFalse(oracle.layout_sane(self.body(count=0x7F7F7F7F)))
        self.assertFalse(oracle.layout_sane(self.body(frames=0)))
        self.assertFalse(oracle.layout_sane(good + bytes(4)))
        self.assertFalse(oracle.layout_sane(good[:0x1AB0]))

    def test_compare_plain(self):
        truth = self.body()
        wrong = bytearray(truth)
        wrong[5] ^= 1
        for index in range(oracle.MANAGER_OFFSET + 12, len(wrong)):
            wrong[index] ^= 0x7F
        result = oracle.compare_plain(bytes(wrong), truth)
        self.assertEqual(result["first_diff"], 5)
        self.assertEqual(result["payload_bytes"], len(truth) - oracle.MANAGER_OFFSET)
        self.assertEqual(result["payload_wrong"], len(truth) - oracle.MANAGER_OFFSET - 12)


ISO = oracle.DEFAULT_ASSETS_ROOT / oracle.ISO_NAME


@unittest.skipUnless(ISO.is_file(), "pinned GT4 disc not present")
class DiscTests(unittest.TestCase):
    def test_all_license_demos_and_current_model_status(self):
        demos = oracle.load_demos(oracle.DEFAULT_ASSETS_ROOT)
        self.assertEqual(len(demos), 592)
        for name, key, body in demos:
            # The first plaintext byte does not depend on any FPU result.
            self.assertEqual(body[0] ^ oracle.keystream_byte(oracle.initial_state(key)), key >> 24, name)
        # Recorded status: the donor semantics (exact chop) pass the game CRC on no demo.
        sample = demos[::74]
        passed = sum(oracle.crc_ok(oracle.decrypt(body, key, oracle.mul_chop, oracle.div_chop), key)
                     for _, key, body in sample)
        self.assertEqual(passed, 0)


if __name__ == "__main__":
    unittest.main()
