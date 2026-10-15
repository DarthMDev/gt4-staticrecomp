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

// An end bit in the delay slot of a branch. The donor runs the branch target once more
// and then ends, so the emitter must refuse the program rather than run past the end.
Fixture end_on_branch_delay_fixture() {
    Fixture f{"refused_end_on_branch_delay", blank_image(), {0}, true};
    put(f.image, 0x00, lower_branch(kOpcodeBranch, 0, 0, 2), kUpperVnop);  // branch to 0x18
    put(f.image, 0x08, lower_iaddiu(1, 1, 1), kUpperVnop | kEndBit);       // delay slot with the end bit
    put(f.image, 0x10, lower_iaddiu(2, 1, 2), kUpperVnop);
    put(f.image, 0x18, lower_iaddiu(3, 1, 3), kUpperVnop | kEndBit);
    return f;
}

// Lower word of an instruction that changes no register: VI0 is discarded.
std::uint32_t lower_vnop_word() { return lower_iaddiu(0, 0, 0); }
// Lower MAC readers and clip/status readers, decoded by lower() in vurecomp.cpp.
std::uint32_t lower_mac_and(unsigned it, unsigned is) { return (0x1Au << 25) | ((it & 15u) << 16) | ((is & 15u) << 11); }
std::uint32_t lower_clip_and(std::uint32_t mask24) { return (0x12u << 25) | (mask24 & 0xFFFFFFu); }
std::uint32_t lower_clip_read(unsigned it) { return (0x1Cu << 25) | ((it & 15u) << 16); }
std::uint32_t lower_status_and(unsigned it, std::uint32_t imm11) { return (0x16u << 25) | ((it & 15u) << 16) | (imm11 & 0x7FFu); }
// Upper CLIP: funct 0x3F with the index field 7 gives index 31 (see upper() in vurecomp.cpp).
std::uint32_t upper_clip(unsigned fs, unsigned ft) { return upper_vector(0x3Fu, 0, 7, fs, ft, false); }

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

// MAC readers 1 to 5 instructions after a flag-setting write, and a second write
// that hides the first from a reader 5 instructions later. Writer and reader in one
// instruction: the reader sees the value before that instruction's own write.
Fixture mac_distance_fixture() {
    Fixture f{"mac_reader_distances", blank_image(), {0}, false};
    put(f.image, 0x00, lower_iaddiu(1, 0, 3), kUpperVnop);
    put(f.image, 0x08, lower_vnop_word(), upper_vector(kFunctAdd, kDestAll, 3, 1, 1, false));           // W1
    put(f.image, 0x10, lower_mac_and(2, 1), kUpperVnop);                                                  // distance 1
    put(f.image, 0x18, lower_mac_and(3, 1), kUpperVnop);                                                  // distance 2
    put(f.image, 0x20, lower_mac_and(4, 1), kUpperVnop);                                                  // distance 3
    put(f.image, 0x28, lower_mac_and(5, 1), kUpperVnop);                                                  // distance 4: sees W1
    put(f.image, 0x30, lower_vnop_word(), upper_vector(kFunctMul, kDestAll, 4, 3, 1, false));           // W2
    put(f.image, 0x38, lower_mac_and(6, 1), kUpperVnop);                                                  // distance 1 from W2
    put(f.image, 0x40, lower_mac_and(7, 1), kUpperVnop);                                                  // distance 2
    put(f.image, 0x48, lower_mac_and(8, 1), kUpperVnop);                                                  // distance 3
    put(f.image, 0x50, lower_mac_and(9, 1), kUpperVnop);                                                  // distance 4 from W2
    put(f.image, 0x58, lower_mac_and(10, 1), upper_vector(kFunctSub, kDestAll, 5, 1, 1, false));        // distance 5: sees W2
    put(f.image, 0x60, lower_mac_and(11, 1), kUpperVnop | kEndBit);                                       // reader and W3 in one instruction
    put(f.image, 0x68, lower_iaddiu(12, 1, 2), upper_vector(kFunctMadd, kDestAll, 6, 1, 1, false));    // W3, delay slot
    return f;
}

// Writes on both arms of a branch, a reader after the join, and a delay-slot write.
// Exit state must be the value of whichever arm ran last.
Fixture mac_branch_fixture(const char *name, std::uint32_t opcode) {
    Fixture f{name, blank_image(), {0}, false};
    put(f.image, 0x00, lower_iaddiu(1, 0, 1), kUpperVnop);
    put(f.image, 0x08, lower_branch(opcode, 0, 0, 2), kUpperVnop);                                       // IBEQ or IBNE on vi0 and vi0: always or never taken
    put(f.image, 0x10, lower_vnop_word(), upper_vector(kFunctAdd, kDestAll, 2, 1, 1, false));           // delay slot write
    put(f.image, 0x18, lower_vnop_word(), upper_vector(kFunctMul, kDestAll, 3, 1, 1, false));           // fall-through write
    put(f.image, 0x20, lower_mac_and(2, 1), upper_vector(kFunctSub, kDestAll, 4, 1, 1, false));        // join: reader and write
    put(f.image, 0x28, lower_mac_and(3, 1), kUpperVnop | kEndBit);
    put(f.image, 0x30, lower_iaddiu(5, 1, 6), kUpperVnop);
    return f;
}

// Clip writers 1 to 4 instructions before clip readers, and one writer that is
// never read. The exit clip must come from the last writer on each path.
Fixture clip_distance_fixture() {
    Fixture f{"clip_reader_distances", blank_image(), {0}, false};
    put(f.image, 0x00, lower_vnop_word(), upper_clip(1, 2));                                              // C1
    put(f.image, 0x08, lower_clip_and(0xFFFFFFu), kUpperVnop);                                            // distance 1
    put(f.image, 0x10, lower_clip_and(0x0000FFu), kUpperVnop);                                            // distance 2: sees C1
    put(f.image, 0x18, lower_clip_read(2), kUpperVnop);                                                   // distance 3
    put(f.image, 0x20, lower_vnop_word(), upper_clip(3, 4));                                              // C2
    put(f.image, 0x28, lower_clip_read(3), kUpperVnop);                                                   // distance 1 from C2
    put(f.image, 0x30, lower_clip_read(4), kUpperVnop);                                                   // distance 2
    put(f.image, 0x38, lower_clip_read(5), kUpperVnop);                                                   // distance 3
    put(f.image, 0x40, lower_clip_read(6), kUpperVnop | kEndBit);                                         // distance 4: sees C2
    put(f.image, 0x48, lower_iaddiu(7, 1, 3), upper_clip(5, 6));                                          // C3, delay slot of E
    return f;
}

// A loop with flag writes in the body and on the branch, a MAC reader after the loop,
// and a write after the end bit. The end bit is not on a branch delay slot: a taken
// branch there leaves the program in the interpreter at the branch target. Budget
// exits inside the loop must leave the same MAC as the donor interpreter does.
Fixture mac_loop_budget_fixture() {
    Fixture f{"mac_loop_budget", blank_image(), {0}, false};
    put(f.image, 0x00, lower_iaddiu(2, 0, 4), kUpperVnop);
    put(f.image, 0x08, lower_iaddiu(2, 2, 0x7FFF), upper_vector(kFunctAdd, kDestAll, 3, 1, 2, false));   // W1 in the loop body
    put(f.image, 0x10, lower_branch(kOpcodeIbne, 0, 2, -2), upper_vector(kFunctMul, kDestAll, 4, 3, 1, false));  // W2 on the branch
    put(f.image, 0x18, lower_vnop_word(), upper_vector(kFunctAdd, kDestAll, 6, 1, 1, false));           // W4, delay slot of the branch
    put(f.image, 0x20, lower_mac_and(5, 1), kUpperVnop | kEndBit);                                       // reader after the loop, end bit
    put(f.image, 0x28, lower_iaddiu(6, 1, 7), upper_vector(kFunctSub, kDestAll, 5, 1, 1, false));       // W3, delay slot of the end
    return f;
}

// Status-using image: FSAND reads the sticky status, so status is kept exactly.
Fixture status_reader_fixture() {
    Fixture f{"status_observed", blank_image(), {0}, false};
    put(f.image, 0x00, lower_iaddiu(1, 0, 3), kUpperVnop);
    put(f.image, 0x08, lower_vnop_word(), upper_vector(kFunctAdd, kDestAll, 2, 1, 1, false));
    put(f.image, 0x10, lower_status_and(2, 0x0FFu), kUpperVnop);
    put(f.image, 0x18, lower_mac_and(3, 1), upper_vector(kFunctMul, kDestAll, 4, 1, 1, false));
    put(f.image, 0x20, lower_vnop_word(), kUpperVnop | kEndBit);
    put(f.image, 0x28, lower_iaddiu(5, 1, 6), kUpperVnop);
    return f;
}

// Two programs in one image. Entry 0 writes flags and ends; entry 0x40 reads them
// first thing. Each is a separate recorded entry, so it is checked alone and in a chain.
Fixture cross_program_fixture() {
    Fixture f{"cross_program_mac", blank_image(), {0x00, 0x40}, false};
    put(f.image, 0x00, lower_iaddiu(1, 0, 3), upper_vector(kFunctAdd, kDestAll, 2, 1, 1, false));
    put(f.image, 0x08, lower_vnop_word(), upper_vector(kFunctMul, kDestAll, 3, 1, 1, false) | kEndBit);
    put(f.image, 0x10, lower_iaddiu(4, 0, 5), kUpperVnop);
    put(f.image, 0x40, lower_mac_and(5, 1), kUpperVnop);
    put(f.image, 0x48, lower_clip_read(6), upper_vector(kFunctSub, kDestAll, 4, 1, 1, false));
    put(f.image, 0x50, lower_vnop_word(), kUpperVnop | kEndBit);
    put(f.image, 0x58, lower_iaddiu(7, 1, 2), kUpperVnop);
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
    fixtures.push_back(mac_distance_fixture());
    fixtures.push_back(mac_branch_fixture("mac_branch_taken", kOpcodeIbeq));
    fixtures.push_back(mac_branch_fixture("mac_branch_fallthrough", kOpcodeIbne));
    fixtures.push_back(clip_distance_fixture());
    fixtures.push_back(mac_loop_budget_fixture());
    fixtures.push_back(end_on_branch_delay_fixture());
    fixtures.push_back(status_reader_fixture());
    fixtures.push_back(cross_program_fixture());
    return fixtures;
}

}
