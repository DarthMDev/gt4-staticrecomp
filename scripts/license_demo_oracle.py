"""License demo cipher oracle for R5900 FPU models (TASK-017).

GT4 decrypts each replay/license demo in place with an FPU-keyed stream cipher
(func_00579E00, reached from func_0044D278; loader at 0x001100D0). The key stream
is the bit pattern of a chain of single-precision mul.s and div.s results, so the
plaintext is only right when every mul.s/div.s result is bit-exact with the R5900.
The game stores a CRC of the plaintext in the key word; on hardware that CRC must
pass for every demo, which makes the 592 demos on the disc a hardware-free test of
mul.s/div.s bit-exactness (roughly 250 chained operations per file before the
state saturates).

Cipher (Confirmed from the translated units, unit_011E.cpp L_00579E00, and the ELF):
  f0 = (key & 0xFFFFFF) | 0x3F000000
  per cipher byte c:  s = bits(f0); out = c ^ s ^ (s >> 8) ^ (s >> 16)  (low byte)
                      f1 = mul.s(FT[c], f0)            fs = FT[c], ft = f0 (0x00579EB8)
                      nb = T8[s & 255] << 16 | T8[(s >> 8) & 255] << 8 | T8[(s >> 16) & 255] | 0x3F000000
                      f0 = div.s(f1, nb)               (0x00579EDC)
  check: (crc32(out) & 0xFFFFFF) | out[0] << 24 == key  (the game ignores the result)

Tables (Confirmed against the reconstructed ELF; derived here so no ELF is needed):
  FT at 0x00655888: FT[i] = 0x3F800000 + (2i + 1) * 0x4010 + (i >> 5)
  T8 at 0x00655C90: 8-bit bit reversal
  CRC at 0x00655358: the standard reflected CRC-32 table (polynomial 0xEDB88320)

The oracle reads the disc through the pinned gt4_volume reader and keeps all game
data in memory; it never writes decrypted data. `--search` runs a CRC-confirmed
beam search that does not depend on any division model (each step may take the
chop quotient or one ulp above it); it recovers hardware-exact plaintext chains and
reports how often each candidate model agrees with them.
"""

from __future__ import annotations

import argparse
import heapq
import json
import math
from pathlib import Path
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ASSETS_ROOT = Path("C:/Antigravity/gt4-staticrecomp")
ISO_NAME = "Gran Turismo 4 (USA) (v2.00).iso"
DEMO_PREFIX = "replay/license/"

SIGN = 0x80000000
ABS = 0x7FFFFFFF


def ft_entry(index: int) -> int:
    return 0x3F800000 + (2 * index + 1) * 0x4010 + (index >> 5)


FT = tuple(ft_entry(index) for index in range(256))
T8 = tuple(int(f"{index:08b}"[::-1], 2) for index in range(256))


def crc_table() -> tuple[int, ...]:
    table = []
    for index in range(256):
        value = index
        for _ in range(8):
            value = (value >> 1) ^ 0xEDB88320 if value & 1 else value >> 1
        table.append(value)
    return tuple(table)


CRC_TABLE = crc_table()


# --- R5900 single-precision helpers (no denormals, no infinities, saturate) ---

def exponent(x: int) -> int:
    return (x >> 23) & 0xFF


def significand(x: int) -> int:
    return (x & 0x7FFFFF) | 0x800000


def pack(sign: int, biased_exponent: int, mantissa: int) -> int:
    if biased_exponent > 255:
        return sign | ABS
    if biased_exponent <= 0:
        return sign
    return sign | (biased_exponent << 23) | (mantissa & 0x7FFFFF)


def normalize_product(sign: int, product: int, biased_exponent: int) -> int:
    """Truncates a 47/48-bit significand product to 24 bits (round toward zero)."""
    if product >> 47:
        return pack(sign, biased_exponent + 1, product >> 24)
    return pack(sign, biased_exponent, product >> 23)


def mul_chop(a: int, b: int) -> int:
    """Donor ps2/fpu.hpp fp::mul: exact product, truncated."""
    sign = (a ^ b) & SIGN
    if not exponent(a) or not exponent(b):
        return sign
    return normalize_product(sign, significand(a) * significand(b), exponent(a) + exponent(b) - 127)


def booth_product(multiplicand: int, multiplier: int, dropped_columns: int) -> int:
    """Radix-4 Booth array that drops partial-product bits below `dropped_columns`.

    The multiplier (ft operand) is recoded into 13 digits in {-2..2}; a negative
    partial product is the one's complement of |digit| * multiplicand, shifted, plus a
    hot one at the digit's weight. Bits below the dropped column, hot ones included,
    never reach the adder tree. Hardware vector (ps2autotests ee_fpu/muldiv):
    mul.s 1.0, 0x3FFFFFFF = 0x3FFFFFFE and mul.s 0x3FFFFFFF, 1.0 = 0x3FFFFFFF; this
    model reproduces both for 1 <= dropped_columns <= 22 (exact product does not).
    """
    keep = ~((1 << dropped_columns) - 1)
    total = 0
    for digit_index in range(13):
        shift = 2 * digit_index
        low = (multiplier >> (shift - 1)) & 1 if digit_index else 0
        mid = (multiplier >> shift) & 1
        high = (multiplier >> (shift + 1)) & 1
        digit = -2 * high + mid + low
        magnitude = abs(digit) * multiplicand
        if digit < 0:
            total += ((~magnitude) << shift) & keep
            if shift >= dropped_columns:
                total += 1 << shift
        else:
            total += (magnitude << shift) & keep
    return total


def make_mul_booth(dropped_columns: int):
    def mul(a: int, b: int) -> int:
        sign = (a ^ b) & SIGN
        if not exponent(a) or not exponent(b):
            return sign
        product = booth_product(significand(a), significand(b), dropped_columns)
        return normalize_product(sign, product, exponent(a) + exponent(b) - 127)
    return mul


def div_operands(a: int, b: int):
    """Returns (sign, special result or None, dividend, divisor, biased exponent)."""
    sign = (a ^ b) & SIGN
    if not exponent(b):
        return sign, sign | ABS, 0, 0, 0
    if not exponent(a):
        return sign, sign, 0, 0, 0
    return sign, None, significand(a), significand(b), exponent(a) - exponent(b) + 127


def finish_quotient(sign: int, quotient_units: int, biased_exponent: int) -> int:
    """`quotient_units` approximates (dividend / divisor) * 2^24, a value in (2^23, 2^25)."""
    if quotient_units >= 1 << 24:
        return pack(sign, biased_exponent, quotient_units >> 1)
    return pack(sign, biased_exponent - 1, quotient_units)


def div_chop(a: int, b: int) -> int:
    """Exact quotient truncated (donor fp::div behavior on this data)."""
    sign, special, dividend, divisor, biased_exponent = div_operands(a, b)
    if special is not None:
        return special
    return finish_quotient(sign, (dividend << 24) // divisor, biased_exponent)


def div_nearest(a: int, b: int) -> int:
    """Exact quotient rounded to nearest even at the final ulp (IEEE-like, for comparison)."""
    sign, special, dividend, divisor, biased_exponent = div_operands(a, b)
    if special is not None:
        return special
    quotient_at_least_one = dividend >= divisor
    if not quotient_at_least_one:
        biased_exponent -= 1
    quotient, remainder = divmod(dividend << (23 if quotient_at_least_one else 24), divisor)
    if 2 * remainder > divisor or (2 * remainder == divisor and quotient & 1):
        quotient += 1
    if quotient >> 24:
        return pack(sign, biased_exponent + 1, quotient >> 1)
    return pack(sign, biased_exponent, quotient)


def make_div_srt2(estimate_bits: int, plus_threshold: int, zero_threshold: int):
    """Radix-2 SRT, carry-save residual, no final correction (candidate, not exact).

    Divisor d in [1/2, 1), w0 = x / 2, 25 digits in {-1, 0, 1}. Digit selection
    reads y = trunc(2ws) + trunc(2wc) with `estimate_bits` fractional bits:
    q = 1 if y >= plus_threshold, 0 if y >= zero_threshold, else -1 (thresholds
    in units of 2^-estimate_bits). The quotient is not corrected with the final
    remainder sign, so results are the floor or the ceiling of the exact quotient
    at 2^-24, as observed on hardware.
    """
    fraction_bits = 25
    width = fraction_bits + 6
    mask = (1 << width) - 1

    def signed(value: int, bits: int) -> int:
        value &= (1 << bits) - 1
        return value - (1 << bits) if value >> (bits - 1) else value

    def div(a: int, b: int) -> int:
        sign, special, dividend, divisor, biased_exponent = div_operands(a, b)
        if special is not None:
            return special
        d = divisor << (fraction_bits - 24)
        ws, wc, quotient = dividend << (fraction_bits - 25), 0, 0
        estimate_width = 4 + estimate_bits
        for _ in range(25):
            shifted_sum, shifted_carry = (ws << 1) & mask, (wc << 1) & mask
            y = signed(signed(shifted_sum >> (fraction_bits - estimate_bits), estimate_width)
                       + signed(shifted_carry >> (fraction_bits - estimate_bits), estimate_width),
                       estimate_width)
            digit = 1 if y >= plus_threshold else (0 if y >= zero_threshold else -1)
            if digit == 1:
                addend, carry_in = (~d) & mask, 1
            elif digit == -1:
                addend, carry_in = d, 0
            else:
                addend, carry_in = 0, 0
            ws = (shifted_sum ^ shifted_carry ^ addend) & mask
            wc = ((((shifted_sum & shifted_carry) | (shifted_sum & addend) | (shifted_carry & addend)) << 1)
                  | carry_in) & mask
            quotient = 2 * quotient + digit
        return finish_quotient(sign, quotient, biased_exponent)
    return div


def make_div_srt2_lookahead(estimate_bits: int, plus_threshold: int, zero_threshold: int):
    """Radix-2 SRT whose digit estimate reads three unreduced vectors (candidate, not exact).

    The residual is kept as sum + carry + pending (-q d of the previous step, exact two's
    complement); the estimate adds the truncated top bits of all three, then the three
    are compressed and the new -q d becomes the pending vector. Same thresholds and
    no-correction rule as make_div_srt2. Best per-step agreement found in phase 1 (94.1%).
    """
    fraction_bits = 25
    width = fraction_bits + 6
    mask = (1 << width) - 1

    def signed(value: int, bits: int) -> int:
        value &= (1 << bits) - 1
        return value - (1 << bits) if value >> (bits - 1) else value

    def div(a: int, b: int) -> int:
        sign, special, dividend, divisor, biased_exponent = div_operands(a, b)
        if special is not None:
            return special
        d = divisor << (fraction_bits - 24)
        sum_vector, carry_vector, pending, quotient = dividend << (fraction_bits - 25), 0, 0, 0
        estimate_width = 4 + estimate_bits
        shift = fraction_bits - estimate_bits
        for _ in range(25):
            sum_vector, carry_vector, pending = (sum_vector << 1) & mask, (carry_vector << 1) & mask, (pending << 1) & mask
            y = signed(signed(sum_vector >> shift, estimate_width) + signed(carry_vector >> shift, estimate_width)
                       + signed(pending >> shift, estimate_width), estimate_width)
            digit = 1 if y >= plus_threshold else (0 if y >= zero_threshold else -1)
            sum_vector, carry_vector = ((sum_vector ^ carry_vector ^ pending) & mask,
                                        (((sum_vector & carry_vector) | (sum_vector & pending)
                                          | (carry_vector & pending)) << 1) & mask)
            pending = (-digit * d) & mask
            quotient = 2 * quotient + digit
        return finish_quotient(sign, quotient, biased_exponent)
    return div


MUL_MODELS = {
    "chop": mul_chop,
    "booth-t8": make_mul_booth(8),
    "booth-t12": make_mul_booth(12),
    "booth-t16": make_mul_booth(16),
    "booth-t20": make_mul_booth(20),
}
DIV_MODELS = {
    "chop": div_chop,
    "nearest": div_nearest,
    "srt2-cs-t3": make_div_srt2(3, 2, -3),
    "srt2-cs-t1": make_div_srt2(1, 0, -1),
    "srt2-la-t3": make_div_srt2_lookahead(3, 2, -3),
}


# --- overflow clamping variants (TASK-017 round 3) ---------------------------
#
# Modelled from a public prose description only (the PCSX2 wiki lists GT4 "demo races do not work"
# with the workaround EE/IOP clamping mode "Full"); no emulator source was read. A clamp maps a value
# whose exponent field is 255 (a large normal number on the R5900, Inf/NaN in IEEE) to the given
# magnitude with its sign kept. With finite operands and no exponent-255 results, IEEE single
# precision with round toward zero equals the chop models above (denormal results flush to zero in
# both cases here), so "IEEE RZ + clamp" is modelled as chop wrapped by these clamps.

MAX_FINITE = 0x7F7FFFFF
CLAMP_PROFILES = {
    # name: (operand limit or None, result limit or None)
    "none": (None, None),
    "full-7f7f": (MAX_FINITE, MAX_FINITE),
    "operands-7f7f": (MAX_FINITE, None),
    "result-7f7f": (None, MAX_FINITE),
    "full-7fff": (ABS, ABS),
    "operands-7fff": (ABS, None),
    "result-7fff": (None, ABS),
}


def clamp_value(x: int, limit) -> int:
    if limit is None or exponent(x) != 255:
        return x
    return (x & SIGN) | limit


def with_clamp(operation, profile: str):
    """Wraps a mul or div model with the operand and result clamps of a CLAMP_PROFILES entry."""
    operand_limit, result_limit = CLAMP_PROFILES[profile]
    if operand_limit is None and result_limit is None:
        return operation

    def clamped(a: int, b: int) -> int:
        result = operation(clamp_value(a, operand_limit), clamp_value(b, operand_limit))
        return clamp_value(result, result_limit)
    return clamped


# --- cipher ---------------------------------------------------------------

def keystream_byte(state: int) -> int:
    return (state ^ (state >> 8) ^ (state >> 16)) & 0xFF


def divisor_of(state: int) -> int:
    return (T8[state & 0xFF] << 16) | (T8[(state >> 8) & 0xFF] << 8) | T8[(state >> 16) & 0xFF] | 0x3F000000


def initial_state(key: int) -> int:
    return (key & 0xFFFFFF) | 0x3F000000


def crc32(data: bytes) -> int:
    value = 0xFFFFFFFF
    for byte in data:
        value = CRC_TABLE[(value ^ byte) & 0xFF] ^ (value >> 8)
    return value ^ 0xFFFFFFFF


def crc_ok(plain: bytes, key: int) -> bool:
    if not plain:
        return False
    return ((zlib.crc32(plain) & 0xFFFFFF) | (plain[0] << 24)) == key


def decrypt(body: bytes, key: int, mul, div) -> bytes:
    """Decrypts like func_00579E00. Saturated or clamped states (exponent field >= 254) are cached."""
    state = initial_state(key)
    out = bytearray(len(body))
    saturated: dict[tuple[int, int], int] = {}
    for index, cipher in enumerate(body):
        out[index] = (cipher ^ keystream_byte(state)) & 0xFF
        if exponent(state) >= 254:  # saturated or clamped states repeat; cache their successors
            cache_key = (cipher, state)
            following = saturated.get(cache_key)
            if following is None:
                following = div(mul(FT[cipher], state), divisor_of(state))
                saturated[cache_key] = following
            state = following
        else:
            state = div(mul(FT[cipher], state), divisor_of(state))
    return bytes(out)


def encrypt(plain: bytes, mul, div) -> tuple[int, bytes]:
    """Encrypts like func_00579F38 (the game's save-side counterpart); returns (key, cipher)."""
    if not plain:
        raise ValueError("empty plaintext")
    key = (zlib.crc32(plain) & 0xFFFFFF) | (plain[0] << 24)
    state = initial_state(key)
    out = bytearray(len(plain))
    for index, value in enumerate(plain):
        cipher = (value ^ keystream_byte(state)) & 0xFF
        out[index] = cipher
        state = div(mul(FT[cipher], state), divisor_of(state))
    return key, bytes(out)


def split_demo(decoded: bytes):
    offset, size, _, key = struct.unpack_from("<4I", decoded, 0)
    if offset + size > len(decoded):
        raise ValueError("demo header body exceeds the file")
    return key, decoded[offset:offset + size]


def load_demos(assets_root: Path) -> list[tuple[str, int, bytes]]:
    sys.path.insert(0, str(ROOT / "scripts"))
    import gt4_volume as volume

    iso = assets_root / ISO_NAME
    demos = []
    with volume.verified_volume(iso) as stream:
        catalogue = volume.read_catalogue(stream)
        for name in sorted(path for path in catalogue.files if path.startswith(DEMO_PREFIX)):
            record = catalogue.files[name]
            packed = volume.read_at(stream, volume.VOLUME_OFFSET + record.offset, record.stored_size,
                                    volume.VOLUME_OFFSET + volume.VOLUME_SIZE)
            key, body = split_demo(volume.decode_payload(packed, record.kind, record.size))
            demos.append((name, key, body))
    return demos


# --- CRC-confirmed search (model-free ground truth) -------------------------

# Empirical P(result = chop + 1 ulp) by (quotient >= 1, tenth of the chop remainder);
# measured on CRC-confirmed chains (2026-10-16). It only orders the beam.
PLUS_ONE_PRIOR = {
    True: (0.0, 0.0, 0.0, 0.0, 0.0, 0.03, 0.21, 0.57, 0.90, 0.99),
    False: (0.0, 0.0, 0.02, 0.15, 0.43, 0.63, 0.79, 0.93, 0.99, 1.0),
}


def byte_log_prior(value: int) -> float:
    if value == 0:
        return math.log(0.25)
    if 0x20 <= value < 0x7F or value < 0x10 or value == 0xFF:
        return math.log(0.004)
    return math.log(0.0018)


def search_chain(body: bytes, key: int, beam: int):
    """Beam search over chop / chop + 1 ulp div results; returns CRC-confirmed states or None.

    The multiply is taken as chop (Booth truncation with few dropped columns gives the
    same results on this data). Once a path saturates (state 0x7FFFFFFF) the rest of
    the file is deterministic and the CRC is checked directly. Each layer stores
    (score, state, crc, parent index) so memory stays linear in the beam width.
    """
    first = body[0] ^ keystream_byte(initial_state(key))
    layers = [[(0.0, initial_state(key), zlib.crc32(bytes([first])), -1)]]
    tails: dict[int, bytes] = {}
    saturated: dict[tuple[int, int], int] = {}
    best = None  # (score, layer index, entry index)
    for index in range(len(body) - 1):
        candidates = []
        for entry_index, (score, state, crc, _) in enumerate(layers[-1]):
            if state == ABS:
                if index not in tails:
                    rest, tail_state = bytearray(), state
                    for position in range(index, len(body) - 1):
                        cipher = body[position]
                        step = saturated.get((cipher, tail_state))
                        if step is None:
                            step = div_chop(mul_chop(FT[cipher], tail_state), divisor_of(tail_state))
                            saturated[(cipher, tail_state)] = step
                        tail_state = step
                        rest.append(body[position + 1] ^ keystream_byte(tail_state))
                    tails[index] = bytes(rest)
                final = zlib.crc32(tails[index], crc)
                if ((final & 0xFFFFFF) | (first << 24)) == key and (best is None or score > best[0]):
                    best = (score, len(layers) - 1, entry_index)
                continue
            cipher = body[index]
            product = mul_chop(FT[cipher], state)
            divisor = divisor_of(state)
            chop = div_chop(product, divisor)
            dividend_bits, divisor_bits = significand(product), significand(divisor)
            numerator = dividend_bits << (23 if dividend_bits >= divisor_bits else 24)
            remainder_tenth = min(9, (numerator % divisor_bits) * 10 // divisor_bits)
            plus = PLUS_ONE_PRIOR[dividend_bits >= divisor_bits][remainder_tenth]
            plus = min(max(plus, 0.004), 0.996)
            options = [(chop, 1.0 - plus)]
            if chop & ABS not in (0, ABS):
                options.append((chop + 1, plus))
            for result, probability in options:
                plain = body[index + 1] ^ keystream_byte(result)
                candidates.append((score + math.log(probability) + byte_log_prior(plain), result,
                                   zlib.crc32(bytes([plain]), crc), entry_index))
        if not candidates:
            break
        layers.append(heapq.nlargest(beam, candidates, key=lambda entry: entry[0]))
    if best is None:
        return None
    states = []
    layer_index, entry_index = best[1], best[2]
    while layer_index > 0:
        _, state, _, parent = layers[layer_index][entry_index]
        states.append(state)
        layer_index, entry_index = layer_index - 1, parent
    return tuple(reversed(states))


def chain_samples(body: bytes, key: int, states: tuple[int, ...]):
    """Yields (fs, ft, divisor, product_chop, hardware_quotient) for a confirmed chain."""
    state = initial_state(key)
    for index, following in enumerate(states):
        cipher = body[index]
        yield FT[cipher], state, divisor_of(state), mul_chop(FT[cipher], state), following
        state = following


def decrypt_with_states(body: bytes, key: int, states: tuple[int, ...]) -> bytes:
    """Plaintext of a confirmed chain: the given states first, then the deterministic tail."""
    state = initial_state(key)
    out = bytearray(len(body))
    for index, cipher in enumerate(body):
        out[index] = (cipher ^ keystream_byte(state)) & 0xFF
        state = states[index] if index < len(states) else div_chop(mul_chop(FT[cipher], state), divisor_of(state))
    return bytes(out)


# --- command line ------------------------------------------------------------

MANAGER_OFFSET = 0x1AA4  # RaceSolitaire::loadDemoInputs reads the record count n here (body offset)


def layout_sane(plain: bytes) -> bool:
    """True when the Manager stream at +0x1AA4 is the single-Recorder layout the game expects.

    Layout of every CRC-confirmed non-JP demo: u32 n = 0 at +0x1AA4, then a Recorder
    {u32 frames, u32 size, payload} whose end, 0x1AA8 + size rounded up to 4, is the
    body end. The loader trusts n; n = 0x7F7F7F7F is the crash.
    """
    if len(plain) < MANAGER_OFFSET + 12:
        return False
    count, frames, size = struct.unpack_from("<3I", plain, MANAGER_OFFSET)
    return count == 0 and frames > 0 and MANAGER_OFFSET + 4 + ((size + 3) & ~3) == len(plain)


def compare_plain(model_plain: bytes, true_plain: bytes) -> dict:
    """First differing byte and the share of wrong bytes in the record payload."""
    first = next((index for index, (x, y) in enumerate(zip(model_plain, true_plain)) if x != y), None)
    payload = range(MANAGER_OFFSET, len(true_plain))
    wrong = sum(model_plain[index] != true_plain[index] for index in payload)
    return {"first_diff": first, "payload_wrong": wrong, "payload_bytes": len(payload)}


def evaluate(demos, mul_names, div_names, clamp_names=("none",)):
    results = {}
    for clamp_name in clamp_names:
        for mul_name in mul_names:
            for div_name in div_names:
                mul = with_clamp(MUL_MODELS[mul_name], clamp_name)
                div = with_clamp(DIV_MODELS[div_name], clamp_name)
                crc_passed = sane = sane_non_jp = sane_us = 0
                for name, key, body in demos:
                    plain = decrypt(body, key, mul, div)
                    crc_passed += crc_ok(plain, key)
                    if layout_sane(plain):
                        sane += 1
                        sane_non_jp += not name.startswith(DEMO_PREFIX + "jp")
                        sane_us += name.startswith(DEMO_PREFIX + "us")
                label = f"{mul_name}/{div_name}" if clamp_name == "none" else f"{mul_name}/{div_name}+{clamp_name}"
                results[label] = {"crc_passed": crc_passed, "layout_sane": sane, "layout_sane_non_jp": sane_non_jp,
                                  "layout_sane_us": sane_us, "files": len(demos)}
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--assets-root", type=Path, default=DEFAULT_ASSETS_ROOT)
    parser.add_argument("--mul", action="append", choices=sorted(MUL_MODELS))
    parser.add_argument("--div", action="append", choices=sorted(DIV_MODELS))
    parser.add_argument("--clamp", action="append", choices=sorted(CLAMP_PROFILES),
                        help="Overflow clamp profile(s) applied to every mul/div model (default: none)")
    parser.add_argument("--limit", type=int, default=0, help="Only the first N demos (0 = all)")
    parser.add_argument("--search", type=int, default=0, metavar="BEAM",
                        help="Run the CRC-confirmed beam search with this beam width and score div models")
    arguments = parser.parse_args()
    if not (arguments.assets_root / ISO_NAME).is_file():
        print(json.dumps({"skipped": "ISO not found", "iso": str(arguments.assets_root / ISO_NAME)}))
        return 0
    demos = load_demos(arguments.assets_root)
    if arguments.limit:
        demos = demos[:arguments.limit]
    report = {"demos": len(demos),
              "models": evaluate(demos, arguments.mul or ["chop"], arguments.div or sorted(DIV_MODELS),
                                 arguments.clamp or ["none"])}
    if arguments.search:
        solved, total = 0, 0
        agree = {name: 0 for name in DIV_MODELS}
        versus = {name: {"phase_match": 0, "layout_sane": 0, "payload_wrong": 0, "payload_bytes": 0}
                  for name in DIV_MODELS}
        true_sane = 0
        for name, key, body in demos:
            states = search_chain(body, key, arguments.search)
            if states is None:
                continue
            solved += 1
            true_plain = decrypt_with_states(body, key, states)
            true_sane += layout_sane(true_plain)
            for _, _, divisor, product, quotient in chain_samples(body, key, states):
                total += 1
                for div_name, div in DIV_MODELS.items():
                    agree[div_name] += div(product, divisor) == quotient
            for div_name, div in DIV_MODELS.items():
                model_plain = decrypt(body, key, mul_chop, div)
                diff = compare_plain(model_plain, true_plain)
                entry = versus[div_name]
                entry["phase_match"] += model_plain[-2:] == true_plain[-2:]
                entry["layout_sane"] += layout_sane(model_plain)
                entry["payload_wrong"] += diff["payload_wrong"]
                entry["payload_bytes"] += diff["payload_bytes"]
        report["search"] = {"beam": arguments.search, "solved": solved, "true_layout_sane": true_sane,
                            "div_steps": total, "div_model_agreement": agree, "versus_truth": versus}
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
