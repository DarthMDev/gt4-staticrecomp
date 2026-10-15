// Shared harness for differential runs of emitted VU1 functions against the donor
// interpreter. Both runs start from identical state, use the same 16 KiB code
// image, and compare every Vu0Regs field, VU1 data memory and XGKICK bytes.
//
// Termination. Before each run tpc holds kTpcSentinel. The interpreter and the
// emitted code both write tpc only when the program ends, so a run that still
// holds the sentinel stopped on the instruction cap. Two non-terminating runs are
// counted as capped and are not compared state for state; a terminating run
// that differs from a non-terminating one is a failure.
#pragma once

#include "gif_capture.hpp"
#include "vu_recomp_contract.hpp"
#include "vurecomp.hpp"

#include "hw.hpp"
#include "vu_micro.hpp"
#include "ps2/vu_ops.hpp"

#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#ifdef GT4_VU1_AOT
namespace ps2::vu {
void aot_set_enabled(bool enabled);  // scripts/vu1_aot_overlay.inc
}
#endif

namespace gt4::vutest {

// The donor entry point ps2::vu::run must reach the interpreter for reference runs.
// Under the AOT overlay that is an explicit switch, not an environment variable.
inline void use_interpreter_reference() {
#ifdef GT4_VU1_AOT
    ps2::vu::aot_set_enabled(false);
#endif
}

constexpr std::uint32_t kTpcSentinel = 0xFFFFFFFFu;
constexpr std::size_t kCodeBytes = 16384;

struct Counters {
    int checks = 0;
    int failures = 0;
    int compared = 0;
    int capped = 0;
    int resumed = 0;
};

inline void expect(Counters &counters, bool condition, const std::string &message) {
    ++counters.checks;
    if (condition) return;
    ++counters.failures;
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
}

// Small deterministic generator for initial state.
class Lcg {
public:
    explicit Lcg(std::uint32_t seed) : state_(seed * 2654435761u + 0x9E3779B9u) {}
    std::uint32_t next() {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

private:
    std::uint32_t state_;
};

// Moderate finite values, so that most arithmetic is ordinary (no NaN paths).
inline std::uint32_t finite_bits(Lcg &rng) {
    const float value = static_cast<float>(static_cast<std::int32_t>(rng.next() % 20001u) - 10000) / 100.0f;
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

struct SeedMode {
    std::uint32_t seed;
    bool raw_bits;  // true: every word is raw random bits, possibly NaN or denormal
};

inline ps2::Vu0Regs make_state(SeedMode mode) {
    Lcg rng(mode.seed);
    const auto word = [&]() { return mode.raw_bits ? rng.next() : finite_bits(rng); };
    ps2::Vu0Regs state{};
    for (int reg = 1; reg < 32; ++reg)
        for (int lane = 0; lane < 4; ++lane) state.vf[reg].w[lane] = word();
    state.vf[0].w[3] = 0x3F800000u;
    for (int lane = 0; lane < 4; ++lane) state.acc.w[lane] = word();
    for (int reg = 1; reg < 16; ++reg) state.vi[reg] = static_cast<std::uint16_t>(rng.next());
    state.vi[0] = 0;
    state.q = word();
    state.p = word();
    state.i = word();
    state.r = rng.next() & 0x7FFFFFu;
    state.mac = rng.next() & 0xFFFFu;
    state.status = rng.next() & 0xFFFu;
    state.clip = rng.next() & 0xFFFFFFu;
    state.cmsar0 = 0;
    state.tpc = 0;
    return state;
}

inline std::vector<std::uint8_t> make_memory(SeedMode mode) {
    Lcg rng(mode.seed ^ 0x5A5A5A5Au);
    std::vector<std::uint8_t> memory(16384);
    for (auto &byte : memory) byte = static_cast<std::uint8_t>(rng.next() >> 24);
    return memory;
}

// Returns the first differing field, or an empty string.
// VU1 status is compared only when the image can observe it (see image_status_used).
inline std::string first_difference(const ps2::Vu0Regs &a, const ps2::Vu0Regs &b, bool compare_status = true) {
    for (int reg = 0; reg < 32; ++reg)
        if (std::memcmp(&a.vf[reg], &b.vf[reg], 16) != 0) return "vf" + std::to_string(reg);
    if (std::memcmp(&a.acc, &b.acc, 16) != 0) return "acc";
    if (std::memcmp(a.vi, b.vi, sizeof a.vi) != 0) return "vi";
    if (a.q != b.q) return "q";
    if (a.p != b.p) return "p";
    if (a.i != b.i) return "i";
    if (a.r != b.r) return "r";
    if (a.mac != b.mac) return "mac";
    if (compare_status && a.status != b.status) return "status";
    if (a.clip != b.clip) return "clip";
    if (a.cmsar0 != b.cmsar0) return "cmsar0";
    if (a.tpc != b.tpc) return "tpc";
    return {};
}

struct Outcome {
    ps2::Vu0Regs regs{};
    std::vector<std::uint8_t> memory;
    std::vector<std::uint8_t> kicks;
    bool completed = false;
};

inline void load_state(const std::uint8_t *code, const ps2::Vu0Regs &state, const std::vector<std::uint8_t> &memory) {
    std::memcpy(ps2::g_vu1_code, code, kCodeBytes);
    ps2::vu::code_changed(1);
    ps2::vu::regs(1) = state;
    ps2::vu::regs(1).tpc = kTpcSentinel;
    std::memcpy(ps2::g_vu1_mem, memory.data(), memory.size());
    reset_kick_capture();
}

inline Outcome capture() {
    Outcome outcome;
    outcome.regs = ps2::vu::regs(1);
    outcome.memory.assign(ps2::g_vu1_mem, ps2::g_vu1_mem + sizeof ps2::g_vu1_mem);
    outcome.kicks = kick_bytes();
    outcome.completed = outcome.regs.tpc != kTpcSentinel;
    return outcome;
}

// Donor interpreter from entry.
inline Outcome run_reference(const std::uint8_t *code, std::uint32_t entry, const ps2::Vu0Regs &state, const std::vector<std::uint8_t> &memory) {
    load_state(code, state, memory);
    ps2::vu::run(1, entry);
    return capture();
}

// Emitted function from entry under a budget. On BudgetExhausted the donor
// interpreter continues from resume_pc, as the runtime contract requires.
inline Outcome run_generated(gt4::vugen::ProgramFunction function, const std::uint8_t *code, const ps2::Vu0Regs &state, const std::vector<std::uint8_t> &memory,
                             std::uint32_t budget, bool &resumed) {
    load_state(code, state, memory);
    gt4::vugen::RunContext context{budget, 0};
    const gt4::vugen::RunStatus status = function(ps2::vu::regs(1), ps2::g_vu1_mem, context);
    resumed = status == gt4::vugen::RunStatus::BudgetExhausted;
    if (resumed) ps2::vu::interpret(1, context.resume_pc);
    return capture();
}

// Compares one reference and one generated outcome. Returns true when compared.
inline bool compare_outcomes(const Outcome &expected, const Outcome &actual, const std::string &context, Counters &counters, bool compare_status = true) {
    // The interpreter is the reference. If it hit its instruction cap, the case is
    // capped. The emitted code charges an upper bound, so it can exhaust earlier and
    // resume the interpreter under a second cap; that total may exceed one cap and is
    // not compared. A reference that ends while the emitted code does not is a failure.
    if (!expected.completed) {
        ++counters.capped;
        return false;
    }
    expect(counters, actual.completed, context + ": emitted code did not end where the reference did");
    const std::string difference = first_difference(expected.regs, actual.regs, compare_status);
    expect(counters, difference.empty(), context + ": register state differs at " + difference);
    expect(counters, expected.memory == actual.memory, context + ": VU1 data memory differs");
    expect(counters, expected.kicks == actual.kicks, context + std::format(": XGKICK bytes differ ({} vs {})", expected.kicks.size(), actual.kicks.size()));
    ++counters.compared;
    return true;
}

// Full differential check for one program and one seed. Status is compared only
// when the image reads status: otherwise the emitted code does not keep it and no
// VU1 instruction can observe it.
inline void check_program(const std::uint8_t *code, std::uint32_t entry, gt4::vugen::ProgramFunction function, SeedMode mode, std::uint32_t budget,
                          const std::string &label, Counters &counters) {
    const ps2::Vu0Regs state = make_state(mode);
    const std::vector<std::uint8_t> memory = make_memory(mode);
    const Outcome expected = run_reference(code, entry, state, memory);
    bool resumed = false;
    const Outcome actual = run_generated(function, code, state, memory, budget, resumed);
    if (resumed) ++counters.resumed;
    const bool compare_status = gt4::vurecomp::image_status_used(code, kCodeBytes);
    compare_outcomes(expected, actual, std::format("{} entry 0x{:04X} seed {}{} budget {}", label, entry, mode.seed, mode.raw_bits ? " raw" : "", budget), counters,
                     compare_status);
}

// Two programs run back to back from one state, as a VU1 call sequence does. The
// second program reads the MAC and clip left by the first.
inline void check_chain(const std::uint8_t *code, std::uint32_t first_entry, gt4::vugen::ProgramFunction first, std::uint32_t second_entry,
                        gt4::vugen::ProgramFunction second, SeedMode mode, const std::string &label, Counters &counters) {
    const ps2::Vu0Regs state = make_state(mode);
    const std::vector<std::uint8_t> memory = make_memory(mode);
    load_state(code, state, memory);
    ps2::vu::run(1, first_entry);
    ps2::vu::run(1, second_entry);
    const Outcome expected = capture();
    load_state(code, state, memory);
    gt4::vugen::RunContext context{gt4::vugen::kInterpreterInstructionBudget, 0};
    first(ps2::vu::regs(1), ps2::g_vu1_mem, context);
    gt4::vugen::RunContext second_context{gt4::vugen::kInterpreterInstructionBudget, 0};
    const bool resumed = second(ps2::vu::regs(1), ps2::g_vu1_mem, second_context) == gt4::vugen::RunStatus::BudgetExhausted;
    if (resumed) ps2::vu::interpret(1, second_context.resume_pc);
    const Outcome actual = capture();
    compare_outcomes(expected, actual, std::format("{} chain 0x{:04X} then 0x{:04X} seed {}", label, first_entry, second_entry, mode.seed), counters,
                     gt4::vurecomp::image_status_used(code, kCodeBytes));
}

}
