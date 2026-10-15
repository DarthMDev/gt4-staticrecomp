//@section includes
#include "vu_recomp_contract.hpp"
#include "program_identity.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace gt4::vugen::vu0 {
extern const ProgramEntry kPrograms[];
extern const std::uint32_t kProgramCount;
}
//@section helpers
// VU0 AOT overlay (slice 160). A generated VU0 program replaces run(0, entry) only when its key,
// entry, image-wide flag and status bits and identity digest match the live VU0 code image. The
// exact path is the donor run(0, pc): it always writes MAC, status and clip, which the EE reads with
// CFC2. The interpreter hand-offs from generated code use that exact path, never run_fast.
bool g_vu0_aot_enabled = true;

namespace vu0aot {
struct Counters {
    std::uint64_t compiled = 0;
    std::uint64_t interpreted = 0;
    std::uint64_t budget_resumes = 0;
    std::uint64_t handoffs = 0;
    std::uint64_t identity_rejects = 0;
    std::uint64_t verified = 0;
    std::uint64_t mismatches = 0;
};
Counters g_counters;
std::uint64_t g_verify_every = 0;
std::uint64_t g_verify_seen = 0;
bool g_verify_parsed = false;
// True while an exact hand-off runs run(0, pc): the gate must not re-enter the table.
bool g_suppressed = false;
// True while a verification runs: nested calls do not start another verification.
bool g_in_verify = false;
struct Slot {
    std::uint32_t generation = 0;
    const gt4::vugen::ProgramEntry *program = nullptr;
};
Slot g_slots[512];
}  // namespace vu0aot

void vu0aot_parse_environment() {
    if (vu0aot::g_verify_parsed) return;
    vu0aot::g_verify_parsed = true;
    if (const char *value = std::getenv("GT4_VU0_VERIFY")) vu0aot::g_verify_every = std::strtoull(value, nullptr, 10);
}

// Looks up the generated VU0 program for an entry. Cached per entry and per code generation (code_changed(0)).
const gt4::vugen::ProgramEntry *vu0aot_find_program(std::uint32_t entry) {
    const std::uint32_t address = entry & 4095u;
    vu0aot::Slot &slot = vu0aot::g_slots[address >> 3];
    if (slot.generation == g_generation[0]) return slot.program;
    slot.generation = g_generation[0];
    slot.program = nullptr;
    // VU0 exact policy: the emitter forces the flag and status bits on, so the identity uses them too.
    const std::vector<std::uint32_t> pcs = reachable(g_vu0_code, 4095u, address);
    const std::uint64_t key = program_key(g_vu0_code, pcs, address);
    for (std::size_t i = 0; i < gt4::vugen::vu0::kProgramCount; ++i) {
        const gt4::vugen::ProgramEntry &candidate = gt4::vugen::vu0::kPrograms[i];
        if (candidate.key != key || candidate.entry != address) continue;
        if (!candidate.image_flags_used || !candidate.image_status_used ||
            gt4::vurecomp::program_identity_digest(g_vu0_code, pcs, address, true, true, "GT4-VU0-PROGRAM-IDENTITY-1") != candidate.identity_sha256) {
            ++vu0aot::g_counters.identity_rejects;
            continue;
        }
        slot.program = &candidate;
        break;
    }
    return slot.program;
}

// Exact interpreter hand-off from generated code: run(0, pc) with the gate off, as the donor runs every VU0 call.
void vu0aot_exact_run(std::uint32_t pc) {
    ++vu0aot::g_counters.handoffs;
    const bool suppressed_before = vu0aot::g_suppressed;
    vu0aot::g_suppressed = true;
    run(0, pc);
    vu0aot::g_suppressed = suppressed_before;
}

// Runs the emitted function. BudgetExhausted resumes on the exact path at resume_pc; no instruction of
// the resumed segment has run (vu_recomp_contract.hpp).
void vu0aot_run_compiled(const gt4::vugen::ProgramEntry &program, std::uint32_t budget = gt4::vugen::kInterpreterInstructionBudget) {
    gt4::vugen::RunContext context{budget, 0, &vu0aot_exact_run};
    if (program.function(g_cpu.vu0, g_vu0_mem, context) == gt4::vugen::RunStatus::BudgetExhausted) {
        ++vu0aot::g_counters.budget_resumes;
        vu0aot_exact_run(context.resume_pc);
    }
}

// Returns the first differing Vu0Regs field, or an empty string. Every field is compared: VU0 MAC,
// status and clip are EE-visible.
std::string vu0aot_first_regs_difference(const Vu0Regs &a, const Vu0Regs &b) {
    for (int reg = 0; reg < 32; ++reg)
        if (std::memcmp(&a.vf[reg], &b.vf[reg], 16) != 0) return "vf" + std::to_string(reg);
    if (std::memcmp(&a.acc, &b.acc, 16) != 0) return "acc";
    if (std::memcmp(a.vi, b.vi, sizeof a.vi) != 0) return "vi";
    if (a.q != b.q) return "q";
    if (a.p != b.p) return "p";
    if (a.i != b.i) return "i";
    if (a.r != b.r) return "r";
    if (a.mac != b.mac) return "mac";
    if (a.status != b.status) return "status";
    if (a.clip != b.clip) return "clip";
    if (a.cmsar0 != b.cmsar0) return "cmsar0";
    if (a.tpc != b.tpc) return "tpc";
    return {};
}

// Writes the pre-call state of the first mismatch for offline replay: entry, Vu0Regs pair, VU0 memory and code.
void vu0aot_dump_mismatch(const Vu0Regs &before, const std::vector<std::uint8_t> &memory_before, std::uint32_t entry,
                          const Vu0Regs &compiled, const Vu0Regs &interpreted) {
    char name[64];
    std::snprintf(name, sizeof name, "vu0-mismatch-%04X.dump", entry);
    std::FILE *file = std::fopen(name, "wb");
    if (!file) return;
    const std::uint32_t header[1] = {entry};
    std::fwrite(header, sizeof header, 1, file);
    std::fwrite(&before, sizeof before, 1, file);
    std::fwrite(&compiled, sizeof compiled, 1, file);
    std::fwrite(&interpreted, sizeof interpreted, 1, file);
    std::fwrite(memory_before.data(), 1, memory_before.size(), file);
    std::fwrite(g_vu0_code, 1, 4096, file);
    std::fclose(file);
}

// Sampled differential check: the generated program first, then the exact interpreter from the same
// state. The interpreter result stands. Compared: all Vu0Regs fields and VU0 data memory.
void vu0aot_verify(const gt4::vugen::ProgramEntry &program, std::uint32_t entry) {
    vu0aot::g_in_verify = true;
    const Vu0Regs before = g_cpu.vu0;
    const std::vector<std::uint8_t> memory_before(g_vu0_mem, g_vu0_mem + sizeof g_vu0_mem);
    {
        gt4::perf::Scope scope(gt4::perf::Category::vu0_aot);
        vu0aot_run_compiled(program);
    }
    const Vu0Regs compiled_regs = g_cpu.vu0;
    const std::vector<std::uint8_t> compiled_memory(g_vu0_mem, g_vu0_mem + sizeof g_vu0_mem);
    g_cpu.vu0 = before;
    std::memcpy(g_vu0_mem, memory_before.data(), memory_before.size());
    const bool suppressed_before = vu0aot::g_suppressed;
    vu0aot::g_suppressed = true;
    run(0, entry);
    vu0aot::g_suppressed = suppressed_before;

    ++vu0aot::g_counters.verified;
    std::string difference = vu0aot_first_regs_difference(compiled_regs, g_cpu.vu0);
    if (difference.empty() && std::memcmp(compiled_memory.data(), g_vu0_mem, compiled_memory.size()) != 0) difference = "memory";
    if (!difference.empty()) {
        ++vu0aot::g_counters.mismatches;
        if (vu0aot::g_counters.mismatches <= 8) std::fprintf(stderr, "[vu0-verify] mismatch entry 0x%04X field %s\n", entry, difference.c_str());
    }
    if (!difference.empty() && vu0aot::g_counters.mismatches == 1) vu0aot_dump_mismatch(before, memory_before, entry, compiled_regs, g_cpu.vu0);
    vu0aot::g_in_verify = false;
}

// Test seam: when disabled, run(0) always interprets (reference runs in differential tests).
void vu0aot_set_enabled(bool enabled) { g_vu0_aot_enabled = enabled; }

// Named counter for the controls: "compiled", "interpreted" or "identity_rejects" (0 for other names).
std::uint64_t vu0aot_counter(const char *name) {
    const vu0aot::Counters &c = vu0aot::g_counters;
    if (std::strcmp(name, "compiled") == 0) return c.compiled;
    if (std::strcmp(name, "interpreted") == 0) return c.interpreted;
    if (std::strcmp(name, "identity_rejects") == 0) return c.identity_rejects;
    return 0;
}

// Counter line for the performance report (native/perf_probe.cpp).
void vu0_aot_report(std::FILE *out) {
    const vu0aot::Counters &c = vu0aot::g_counters;
    std::fprintf(out,
                 "[perf] vu0_aot compiled_calls=%llu interpreted_calls=%llu budget_resumes=%llu handoffs=%llu identity_rejects=%llu verified=%llu verify_mismatches=%llu\n",
                 static_cast<unsigned long long>(c.compiled), static_cast<unsigned long long>(c.interpreted),
                 static_cast<unsigned long long>(c.budget_resumes), static_cast<unsigned long long>(c.handoffs),
                 static_cast<unsigned long long>(c.identity_rejects), static_cast<unsigned long long>(c.verified),
                 static_cast<unsigned long long>(c.mismatches));
}

// Returns true when the call was handled by a generated program. Calls that the exact path must
// keep (VF0 or VI0 not in the architectural state the generated code assumes) are interpreted.
bool vu0aot_run_if_registered(std::uint32_t entry) {
    vu0aot_parse_environment();
    static const bool interpret_only = std::getenv("PS2_VU_INTERPRET") != nullptr;
    const Vu0Regs &state = g_cpu.vu0;
    const bool vf0_normal = state.vf[0].w[0] == 0 && state.vf[0].w[1] == 0 && state.vf[0].w[2] == 0 && state.vf[0].w[3] == fp::kOne;
    const gt4::vugen::ProgramEntry *program = interpret_only || !vf0_normal || state.vi[0] != 0 ? nullptr : vu0aot_find_program(entry);
    if (!program) {
        ++vu0aot::g_counters.interpreted;
        return false;
    }
    ++vu0aot::g_counters.compiled;
    // Periodic counter line: every GT4_VU0_REPORT_EVERY compiled calls, or every 4096 with GT4_PERF.
    static const std::uint64_t report_every = [] {
        const char *period = std::getenv("GT4_VU0_REPORT_EVERY");
        if (period) return std::strtoull(period, nullptr, 10);
        return std::getenv("GT4_PERF") ? std::uint64_t{4096} : std::uint64_t{0};
    }();
    if (report_every && vu0aot::g_counters.compiled % report_every == 0) vu0_aot_report(stderr);
    if (!vu0aot::g_in_verify && vu0aot::g_verify_every && ++vu0aot::g_verify_seen % vu0aot::g_verify_every == 0) {
        vu0aot_verify(*program, entry & 4095u);
        return true;
    }
    gt4::perf::Scope scope(gt4::perf::Category::vu0_aot);
    vu0aot_run_compiled(*program);
    return true;
}
//@section run_gate
    if (n == 0 && g_vu0_aot_enabled && !vu0aot::g_suppressed && vu0aot_run_if_registered(address)) return;
