#define _CRT_SECURE_NO_WARNINGS
// Pure fixture for the R5900 round-toward-zero reference (fpu_chop.hpp). No window, device or
// guest data is touched. It checks hand-computed chop results where host round-to-nearest
// differs, and sweeps random normal operands: every chop result must lie between zero and the
// nearest result, at most one ulp away, and the sweep must contain cases where they differ.
#include "fpu_chop.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {

using gt4::fpu_chop::Result;

void require(bool value, const std::string &label) {
  if (!value) throw std::runtime_error(label);
}

std::uint32_t bits_of(float value) {
  std::uint32_t bits;
  std::memcpy(&bits, &value, sizeof bits);
  return bits;
}

float float_of(std::uint32_t bits) {
  float value;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

// Deterministic 64-bit LCG so the sweep is reproducible.
struct Lcg {
  std::uint64_t state;
  std::uint32_t next() {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<std::uint32_t>(state >> 33);
  }
};

// Random normal float with biased exponent in [low_exponent, high_exponent].
std::uint32_t random_normal(Lcg &rng, std::uint32_t low_exponent, std::uint32_t high_exponent) {
  const std::uint32_t exponent = low_exponent + rng.next() % (high_exponent - low_exponent + 1);
  const std::uint32_t sign = (rng.next() & 1u) << 31;
  const std::uint32_t mantissa = rng.next() & 0x7FFFFFu;
  return sign | (exponent << 23) | mantissa;
}

void test_hand_computed_chop_results() {
  // 1/3 = 0x3EAAAAAA.AA... in 24 bits: chop keeps ...AA, nearest rounds up to ...AB.
  const std::uint32_t one = bits_of(1.0f), three = bits_of(3.0f), two = bits_of(2.0f);
  require(gt4::fpu_chop::div(one, three).bits == 0x3EAAAAAAu, "chop 1/3 must be 0x3EAAAAAA");
  require(bits_of(1.0f / 3.0f) == 0x3EAAAAABu, "host nearest 1/3 must be 0x3EAAAAAB (discriminator)");
  // 2/3 = 0x3F2AAAAA.AA... chop keeps ...AA, nearest rounds up.
  require(gt4::fpu_chop::div(two, three).bits == 0x3F2AAAAAu, "chop 2/3 must be 0x3F2AAAAA");
  require(bits_of(2.0f / 3.0f) == 0x3F2AAAABu, "host nearest 2/3 must be 0x3F2AAAAB (discriminator)");
  // 1 + 1.5 ulp: exact sum is between 1+1ulp and 1+2ulp; chop keeps 1+1ulp, nearest-even picks 1+2ulp.
  require(gt4::fpu_chop::add(one, bits_of(1.5f * 0x1p-23f)).bits == 0x3F800001u,
          "chop 1 + 1.5ulp must be 0x3F800001");
  require(bits_of(1.0f + 1.5f * 0x1p-23f) == 0x3F800002u,
          "host nearest for 1 + 1.5ulp must be 0x3F800002 (discriminator)");
  // Subtraction of a close value keeps the chopped magnitude: 1.0 - 2^-24 = 1 - 0.5ulp(below 1).
  require(gt4::fpu_chop::sub(one, bits_of(0x1p-24f)).bits == 0x3F7FFFFFu,
          "chop 1 - 2^-24 must be 0x3F7FFFFF");
  // Denormal inputs flush to zero; overflow saturates to the largest finite magnitude.
  require(gt4::fpu_chop::mul(0x00400000u, two).bits == 0u, "denormal input must flush to +0");
  require(gt4::fpu_chop::mul(0x7F000000u, 0x7F000000u).bits == 0x7F7FFFFFu,
          "overflow must saturate to 0x7F7FFFFF");
  require(gt4::fpu_chop::div(one, 0u).invalid, "division by zero must set invalid");
  require(gt4::fpu_chop::div(one, 0u).bits == 0x7F7FFFFFu, "division by zero must give max finite");
}

struct SweepCounts {
  int cases = 0;
  int differing = 0;
};

// Compares an oracle result with the host nearest result for the same operation.
void check_against_nearest(std::uint32_t chop_bits, float nearest, const char *op, SweepCounts &counts) {
  const std::uint32_t nearest_bits = bits_of(nearest);
  ++counts.cases;
  if (chop_bits == nearest_bits) return;
  ++counts.differing;
  const std::uint32_t chop_magnitude = chop_bits & gt4::fpu_chop::kAbs;
  const std::uint32_t nearest_magnitude = nearest_bits & gt4::fpu_chop::kAbs;
  require((chop_bits & gt4::fpu_chop::kSign) == (nearest_bits & gt4::fpu_chop::kSign),
          std::string(op) + ": sign differs from nearest");
  require(chop_magnitude <= nearest_magnitude,
          std::string(op) + ": chop magnitude above nearest magnitude");
  require(nearest_magnitude - chop_magnitude <= 1u,
          std::string(op) + ": chop more than one ulp below nearest");
}

void test_random_sweep() {
  Lcg rng{0x9E3779B97F4A7C15ull};
  SweepCounts add_counts, sub_counts, mul_counts, div_counts, sqrt_counts;
  for (int i = 0; i < 200000; ++i) {
    const std::uint32_t a = random_normal(rng, 110, 140);
    const std::uint32_t b = random_normal(rng, 110, 140);
    const volatile float fa = float_of(a), fb = float_of(b);
    check_against_nearest(gt4::fpu_chop::add(a, b).bits, fa + fb, "add", add_counts);
    check_against_nearest(gt4::fpu_chop::sub(a, b).bits, fa - fb, "sub", sub_counts);
    check_against_nearest(gt4::fpu_chop::mul(a, b).bits, fa * fb, "mul", mul_counts);
    check_against_nearest(gt4::fpu_chop::div(a, b).bits, fa / fb, "div", div_counts);
    const volatile float positive = float_of(a & gt4::fpu_chop::kAbs);
    check_against_nearest(gt4::fpu_chop::sqrt(a & gt4::fpu_chop::kAbs).bits, __builtin_sqrtf(positive),
                          "sqrt", sqrt_counts);
  }
  // The sweep is only meaningful if nearest and chop actually differ for each operation.
  const SweepCounts all[] = {add_counts, sub_counts, mul_counts, div_counts, sqrt_counts};
  const char *names[] = {"add", "sub", "mul", "div", "sqrt"};
  for (int i = 0; i < 5; ++i) {
    std::printf("[fpu_chop] %-4s cases=%d chop_differs_from_nearest=%d\n", names[i], all[i].cases,
                all[i].differing);
    require(all[i].differing > 0, std::string(names[i]) + ": no discriminating vectors");
  }
}

}  // namespace

int main() {
  try {
    test_hand_computed_chop_results();
    test_random_sweep();
    std::printf("fpu_chop_fixture: PASS\n");
    return 0;
  } catch (const std::exception &error) {
    std::printf("fpu_chop_fixture: FAIL: %s\n", error.what());
    return 1;
  }
}
