// Runtime contract for functions emitted by gt4_vu_recomp.
//
// Every emitted VU1 program has the signature ProgramFunction. It starts at
// its entry PC with the caller's Vu0Regs and VU1 data memory, and ends in one
// of two states:
//
// Completed
//   The program reached its end (E bit) or handed off to the interpreter with
//   ps2::vu::interpret(1, pc). regs.tpc holds the next PC.
//
// BudgetExhausted
//   The step budget ran out before the next backward-control-transfer segment
//   started. No instruction of that segment has executed. Registers, flags,
//   VI, data memory and the pending-branch state are committed at the
//   instruction boundary context.resume_pc; regs.tpc is not written. The caller
//   must continue with ps2::vu::interpret(1, context.resume_pc), which runs the
//   interpreter with its own 1,000,000-instruction cap.
//
// Budget accounting. context.budget starts at kInterpreterInstructionBudget.
// The function charges the budget at entry and at every backward transfer (a
// branch or fallthrough to a PC at or before the transferring instruction).
// Each charge is the number of instructions in the largest forward-only region
// that starts at the target, so the charge is an upper bound on the
// instructions the following segment can execute. Generated code therefore
// never executes more instructions than the interpreter's budget before it
// returns BudgetExhausted. Charges are per loop head, not per instruction.
//
// Known limit. The interpreter's FlagPipe history (in-flight MAC/clip values
// written in the last few instructions) is not transferred on resume. MAC and
// clip reads inside that window can differ from an uninterrupted run.
#pragma once

#include "ps2/vu_ops.hpp"

#include <cstdint>

namespace gt4::vugen {

enum class RunStatus : std::uint32_t {
    Completed = 0,
    BudgetExhausted = 1,
};

struct RunContext {
    std::uint32_t budget = 0;
    std::uint32_t resume_pc = 0;
};

using ProgramFunction = RunStatus (*)(ps2::Vu0Regs &regs, std::uint8_t *vu_memory, RunContext &context);

// Clip word after one CLIP instruction, bit-identical to ps2::vuops::clip_value. The
// donor takes the previous clip word from the register (r.clip), so the clip word is a
// chain through every CLIP instruction. The emitted code passes its own copy of that
// word, and every CLIP instruction must update it, observed or not.
inline std::uint32_t clip_result(std::uint32_t previous_clip, const ps2::Reg128 &s, std::uint32_t w) {
    const std::int32_t lim = ps2::fp::key(w & ps2::fp::kAbs);
    std::uint32_t f = 0;
    for (int i = 0; i < 3; ++i) {
        const std::int32_t x = ps2::fp::key(s.w[i]);
        if (x > lim) f |= 1u << (i * 2);
        if (x < -lim) f |= 2u << (i * 2);
    }
    return ((previous_clip << 6) | f) & 0xFFFFFFu;
}

// MAC word of one flag-setting result over its destination lanes. Same bits as the
// donor flags() in vu_ops.hpp: lane bit 3 - i is zero, bit 7 - i is sign, bit 15 - i
// is overflow-clamped. The emitted code uses it when a MAC value is kept without the
// status update.
inline std::uint32_t mac_of_result(const ps2::Reg128 &value, unsigned dest) {
    std::uint32_t mac = 0;
    for (int i = 0; i < 4; ++i) {
        if (!(dest & (8u >> i))) continue;
        const std::uint32_t bit = 3u - static_cast<std::uint32_t>(i);
        const std::uint32_t x = value.w[i];
        if (!(x & 0x7F800000u)) mac |= 1u << bit;
        if (x & 0x80000000u) mac |= 1u << (bit + 4);
        if ((x & 0x7FFFFFFFu) == 0x7FFFFFFFu) mac |= 1u << (bit + 12);
    }
    return mac;
}

struct ProgramEntry {
    std::uint64_t key;
    std::uint32_t entry;
    // The function is valid only for images whose image-wide flag-use bit equals
    // this value (see image_flags_used in vurecomp.hpp). The registrar must match it.
    bool image_flags_used;
    // The image-wide status-use bit (image_status_used in program_identity.hpp). When false,
    // the emitted code does not maintain VU1 status and the status field is not compared.
    bool image_status_used;
    // Lowercase SHA-256 of the identity stream (program_identity.hpp); checked by the runtime.
    const char *identity_sha256;
    ProgramFunction function;
};

// Same unit and value as the donor interpreter's per-call cap (vu_micro.cpp run_fast).
constexpr std::uint32_t kInterpreterInstructionBudget = 1'000'000u;

}
