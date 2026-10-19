#pragma once

// Exact round-toward-zero (chop) reference for the R5900 single-precision FPU.
//
// The R5900 FPU rounds toward zero, flushes denormal inputs and results to zero, and
// saturates overflow to the largest finite magnitude. The donor helpers in
// ps2/fpu.hpp implement the same policy with truncating shifts, but div, sqrt and rsqrt
// go through host double precision and then truncate, which can differ from a true
// chop by one ulp when the double rounding crosses a 24-bit boundary.
//
// This header computes each operation with exact integer arithmetic (no host float
// rounding involved). It is a test oracle and a candidate replacement for those helpers.
// It does not change any running behavior unless a caller includes it.
//
// Limits: add and sub are exact when the operands' exponents differ by at most 60
// binades; larger gaps set `exact` to false in the result.

#include <cstdint>

namespace gt4::fpu_chop {

inline constexpr std::uint32_t kSign = 0x80000000u;
inline constexpr std::uint32_t kAbs = 0x7FFFFFFFu;
inline constexpr std::uint32_t kMaxFinite = 0x7F7FFFFFu;

struct Result {
  std::uint32_t bits;
  bool exact;   // false only when the operation was outside the exact range documented above
  bool invalid; // division by zero
};

struct Unpacked {
  bool negative;
  bool zero;               // zero, or denormal input (flushed)
  std::uint64_t mantissa;  // 24-bit significand with the hidden bit set (0 when zero)
  int exponent2;           // value = mantissa * 2^exponent2
};

inline Unpacked unpack(std::uint32_t x) {
  Unpacked result{};
  result.negative = (x & kSign) != 0;
  const std::uint32_t biased = (x >> 23) & 0xFFu;
  if (biased == 0) {
    result.zero = true;
    return result;
  }
  result.mantissa = (x & 0x7FFFFFu) | 0x800000u;
  result.exponent2 = static_cast<int>(biased) - 150;
  return result;
}

// Normalize magnitude * 2^exponent2 to a 24-bit significand by truncation (chop).
inline std::uint32_t pack_chop(bool negative, unsigned __int128 magnitude, int exponent2) {
  if (magnitude == 0) return negative ? kSign : 0u;
  int bit_length = 0;
  for (unsigned __int128 probe = magnitude; probe != 0; probe >>= 1) ++bit_length;
  int shift = bit_length - 24;
  if (shift > 0) {
    magnitude >>= shift;  // truncation toward zero of the magnitude
  } else if (shift < 0) {
    magnitude <<= -shift;
  }
  exponent2 += shift;
  // value = magnitude * 2^exponent2 with magnitude in [2^23, 2^24); biased exponent:
  const int biased = exponent2 + 23 + 127;
  if (biased <= 0) return negative ? kSign : 0u;  // flush to zero
  if (biased >= 255) return (negative ? kSign : 0u) | kMaxFinite;  // saturate
  return (negative ? kSign : 0u) | (static_cast<std::uint32_t>(biased) << 23) |
         (static_cast<std::uint32_t>(magnitude) & 0x7FFFFFu);
}

inline Result mul(std::uint32_t a, std::uint32_t b) {
  const Unpacked x = unpack(a), y = unpack(b);
  const bool negative = x.negative != y.negative;
  if (x.zero || y.zero) return {negative ? kSign : 0u, true, false};
  const unsigned __int128 product = static_cast<unsigned __int128>(x.mantissa) * y.mantissa;
  return {pack_chop(negative, product, x.exponent2 + y.exponent2), true, false};
}

inline Result div(std::uint32_t a, std::uint32_t b) {
  const Unpacked x = unpack(a), y = unpack(b);
  const bool negative = x.negative != y.negative;
  if (y.zero) return {(negative ? kSign : 0u) | kMaxFinite, true, true};
  if (x.zero) return {negative ? kSign : 0u, true, false};
  // floor(ma * 2^40 / mb) lies in (2^39, 2^41), so it keeps at least 24 significant bits.
  // floor(floor(z) / 2^j) equals floor(z / 2^j), so truncating the quotient afterwards is an
  // exact chop. The 64-bit division avoids a 128-bit divide helper (not linked on MSVC).
  const std::uint64_t numerator = x.mantissa << 40;
  const std::uint64_t quotient = numerator / y.mantissa;
  return {pack_chop(negative, quotient, x.exponent2 - y.exponent2 - 40), true, false};
}

inline unsigned __int128 integer_sqrt(unsigned __int128 value) {
  if (value == 0) return 0;
  unsigned __int128 root = 0;
  unsigned __int128 bit = static_cast<unsigned __int128>(1) << 126;
  while (bit > value) bit >>= 2;
  while (bit != 0) {
    if (value >= root + bit) {
      value -= root + bit;
      root = (root >> 1) + bit;
    } else {
      root >>= 1;
    }
    bit >>= 2;
  }
  return root;  // floor(sqrt(value))
}

// The donor helper and (Hypothesis, unverified against hardware) the R5900 take the magnitude
// of the operand, so negative inputs are not special-cased to zero.
inline Result sqrt(std::uint32_t a) {
  const Unpacked x = unpack(a);
  if (x.zero) return {0u, true, false};
  // value = m * 2^e. Scale by 2^50 (even), and make the exponent even by an extra doubling.
  unsigned __int128 radicand = static_cast<unsigned __int128>(x.mantissa) << 50;
  int exponent2 = x.exponent2 - 50;
  if (exponent2 & 1) {
    radicand <<= 1;
    exponent2 -= 1;
  }
  // sqrt(value) = isqrt(radicand) * 2^(exponent2 / 2); floor of the root is an exact chop.
  return {pack_chop(false, integer_sqrt(radicand), exponent2 / 2), true, false};
}

inline Result add(std::uint32_t a, std::uint32_t b) {
  const Unpacked x = unpack(a), y = unpack(b);
  if (x.zero && y.zero) return {(x.negative && y.negative) ? kSign : 0u, true, false};
  if (x.zero) return {b, true, false};  // b is normal here, so it is returned unchanged
  if (y.zero) return {a, true, false};
  const int low = x.exponent2 < y.exponent2 ? x.exponent2 : y.exponent2;
  const int gap = (x.exponent2 > y.exponent2 ? x.exponent2 - y.exponent2 : y.exponent2 - x.exponent2);
  if (gap > 60) return {0u, false, false};  // outside the exact range (not used by fixtures)
  // Widen before shifting: 24 significand bits plus a 60-bit alignment shift exceed 64 bits.
  const __int128 xa = static_cast<__int128>(x.mantissa) << (x.exponent2 - low);
  const __int128 yb = static_cast<__int128>(y.mantissa) << (y.exponent2 - low);
  const __int128 sum = (x.negative ? -xa : xa) + (y.negative ? -yb : yb);
  if (sum == 0) return {0u, true, false};  // exact cancellation gives +0 under round-to-zero
  const bool negative = sum < 0;
  const unsigned __int128 magnitude = static_cast<unsigned __int128>(negative ? -sum : sum);
  return {pack_chop(negative, magnitude, low), true, false};
}

inline Result sub(std::uint32_t a, std::uint32_t b) { return add(a, b ^ kSign); }

}  // namespace gt4::fpu_chop
