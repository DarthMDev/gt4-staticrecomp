#include "vu_fixtures.hpp"

#include <cstring>

namespace gt4::vurecomp::fixtures {
namespace {

constexpr std::size_t kFixtureImageBytes = 16384;
constexpr std::uint32_t kEndBit = 0x40000000u;
constexpr std::uint32_t kOpcodeIaddiu = 0x08u;
constexpr std::uint32_t kOpcodeBranch = 0x20u;
constexpr std::uint32_t kOpcodeIbeq = 0x28u;
constexpr std::uint32_t kOpcodeIbne = 0x29u;
constexpr std::uint32_t kUnknownOpcode = 0x02u;
constexpr std::uint32_t kFunctAdd = 0x28u;
constexpr std::uint32_t kFunctMadd = 0x29u;
constexpr std::uint32_t kFunctMul = 0x2Au;
constexpr std::uint32_t kFunctSub = 0x2Cu;
constexpr std::uint32_t kUnknownUpperFunct = 0x30u;
constexpr unsigned kDestAll = 0xFu;

// Writes one instruction (lower then upper word) at a byte offset.
void put(std::vector<std::uint8_t> &image, std::uint32_t pc, std::uint32_t lower, std::uint32_t upper) {
    for (int i = 0; i < 4; ++i) image[pc + i] = static_cast<std::uint8_t>(lower >> (i * 8));
    for (int i = 0; i < 4; ++i) image[pc + 4 + i] = static_cast<std::uint8_t>(upper >> (i * 8));
}

std::vector<std::uint8_t> blank_image() { return std::vector<std::uint8_t>(kFixtureImageBytes, 0); }

// Loop: vi2 counts 4 iterations; each pass adds and multiplies, then a delay
// slot updates vi5 and vf5. Entry 0 also derives jump target 32 (constant load
// into vi2 from vi0), which is the E-bit tail.
Fixture loop_fixture() {
    Fixture f{"loop_vadd_vmul", blank_image(), {0}, false};
    put(f.image, 0x00, lower_iaddiu(2, 0, 4), kUpperVnop);
    put(f.image, 0x08, lower_iaddiu(2, 2, 0x7FFF), upper_vector(kFunctAdd, kDestAll, 3, 1, 2, false));
    put(f.image, 0x10, lower_branch(kOpcodeIbne, 0, 2, -2), upper_vector(kFunctMul, kDestAll, 4, 3, 1, false));
    put(f.image, 0x18, lower_iaddiu(5, 1, 1), upper_vector(kFunctSub, kDestAll, 5, 2, 1, false));
    put(f.image, 0x20, lower_iaddiu(6, 1, 7), kUpperVnop | kEndBit);
    put(f.image, 0x28, lower_iaddiu(7, 1, 9), upper_vector(kFunctMadd, kDestAll, 6, 1, 2, false));
    return f;
}

// Forward unconditional branch with a delay slot. The skipped instruction at
// 0x18 must not run. Entry 0 derives jump target 24 from the vi0-based load.
Fixture forward_delay_fixture() {
    Fixture f{"forward_delay_slot", blank_image(), {0}, false};
    put(f.image, 0x00, lower_iaddiu(1, 0, 3), kUpperVnop);
    put(f.image, 0x08, lower_branch(kOpcodeBranch, 0, 0, 2), upper_vector(kFunctAdd, kDestAll, 6, 1, 1, false));
    put(f.image, 0x10, lower_iaddiu(3, 1, 5), upper_vector(kFunctMul, kDestAll, 7, 1, 1, false));
    put(f.image, 0x18, lower_iaddiu(4, 1, 9), upper_vector(kFunctAdd, kDestAll, 8, 1, 1, false));
    put(f.image, 0x20, lower_iaddiu(5, 1, 11), kUpperVnop | kEndBit);
    put(f.image, 0x28, lower_iaddiu(6, 1, 13), kUpperVnop);
    return f;
}

// The instruction after an E bit still runs, then the program ends.
Fixture end_bit_fixture() {
    Fixture f{"end_bit_tail", blank_image(), {0}, false};
    put(f.image, 0x00, lower_iaddiu(1, 1, 7), kUpperVnop | kEndBit);
    put(f.image, 0x08, lower_iaddiu(2, 1, 3), upper_vector(kFunctAdd, kDestAll, 3, 1, 2, false));
    return f;
}

// An unknown upper encoding exits to the interpreter at 0x08.
Fixture unknown_upper_fixture() {
    Fixture f{"unknown_upper_nop", blank_image(), {0}, false};
    put(f.image, 0x00, lower_iaddiu(1, 1, 2), upper_vector(kFunctAdd, kDestAll, 2, 1, 1, false));
    put(f.image, 0x08, lower_iaddiu(2, 1, 5), kUnknownUpperFunct);
    put(f.image, 0x10, lower_iaddiu(3, 1, 4), kUpperVnop | kEndBit);
    put(f.image, 0x18, lower_iaddiu(4, 1, 6), kUpperVnop);
    return f;
}

// An unknown lower opcode exits to the interpreter at 0x08.
Fixture unknown_lower_fixture() {
    Fixture f{"unknown_lower_exit", blank_image(), {0}, false};
    put(f.image, 0x00, lower_iaddiu(1, 1, 2), upper_vector(kFunctAdd, kDestAll, 2, 1, 1, false));
    put(f.image, 0x08, lower_unknown(), kUpperVnop);
    put(f.image, 0x10, lower_iaddiu(3, 1, 4), kUpperVnop | kEndBit);
    put(f.image, 0x18, lower_iaddiu(4, 1, 6), kUpperVnop);
    return f;
}

// The unknown lower opcode sits in a delay slot. The emitter must refuse it:
// the exit cannot carry the pending branch.
Fixture delay_slot_fallback_fixture() {
    Fixture f{"refused_delay_slot_exit", blank_image(), {0}, true};
    put(f.image, 0x00, lower_branch(kOpcodeBranch, 0, 0, 1), kUpperVnop);
    put(f.image, 0x08, lower_unknown(), kUpperVnop);
    put(f.image, 0x10, lower_iaddiu(3, 1, 4), kUpperVnop | kEndBit);
    put(f.image, 0x18, lower_iaddiu(4, 1, 6), kUpperVnop | kEndBit);
    return f;
}

}

std::uint32_t lower_iaddiu(unsigned it, unsigned is, std::uint32_t imm15) {
    return (kOpcodeIaddiu << 25) | (((imm15 >> 11) & 0xFu) << 21) | ((it & 15u) << 16) | ((is & 15u) << 11) | (imm15 & 0x7FFu);
}

std::uint32_t lower_branch(std::uint32_t opcode, unsigned it, unsigned is, std::int32_t offset_in_instructions) {
    const std::uint32_t imm11 = static_cast<std::uint32_t>(offset_in_instructions) & 0x7FFu;
    if (opcode == kOpcodeBranch) return (kOpcodeBranch << 25) | imm11;
    if (opcode == kOpcodeIbeq || opcode == kOpcodeIbne) return (opcode << 25) | ((it & 15u) << 16) | ((is & 15u) << 11) | imm11;
    return (opcode << 25) | ((it & 15u) << 16) | ((is & 15u) << 11) | imm11;
}

std::uint32_t lower_unknown() { return (kUnknownOpcode << 25) | 0x123u; }

std::uint32_t upper_vector(std::uint32_t funct, unsigned dest, unsigned fd, unsigned fs, unsigned ft, bool end_bit) {
    return (end_bit ? kEndBit : 0u) | ((dest & 15u) << 21) | ((ft & 31u) << 16) | ((fs & 31u) << 11) | ((fd & 31u) << 6) | (funct & 63u);
}

std::vector<Fixture> build_fixtures() {
    std::vector<Fixture> fixtures;
    fixtures.push_back(loop_fixture());
    fixtures.push_back(forward_delay_fixture());
    fixtures.push_back(end_bit_fixture());
    fixtures.push_back(unknown_upper_fixture());
    fixtures.push_back(unknown_lower_fixture());
    fixtures.push_back(delay_slot_fallback_fixture());
    return fixtures;
}

}
