//@section includes
#include "vu_recomp_contract.hpp"
#include "program_identity.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace gt4::vugen::generated {
extern const ProgramEntry kPrograms[];
extern const std::uint32_t kProgramCount;
}
//@section kick_sink
// XGKICK bytes are captured here during VU1 verification. While a sink is set, the
// real GIF transfer is suppressed and the caller replays the interpreter's chunks.
struct KickChunk {
    std::vector<std::uint8_t> bytes;
    std::uint32_t qwc;
};
std::vector<KickChunk> *g_kick_sink = nullptr;
//@section xgkick_hook
    if (g_kick_sink) g_kick_sink->push_back(KickChunk{std::vector<std::uint8_t>(buffer, buffer + count * 16u), count});
    else gif::transfer(1, buffer, count);
//@section helpers
// VU1 AOT overlay. A generated program replaces the interpreter only when its key,
// entry, image-wide flag bit and identity digest all match the live VU1 image.
bool g_aot_enabled = true;

namespace vu1aot {
struct Counters {
    std::uint64_t compiled = 0;
    std::uint64_t interpreted = 0;
    std::uint64_t budget_resumes = 0;
    std::uint64_t identity_rejects = 0;
    std::uint64_t verified = 0;
    std::uint64_t mismatches = 0;
};
Counters g_counters;
std::uint64_t g_verify_every = 0;
std::uint64_t g_verify_seen = 0;
bool g_verify_parsed = false;
// True while a verification runs. Nested calls (jump-register dispatch re-enters the
// runtime) run without starting another verification, which would restore state under the outer one.
bool g_in_verify = false;
struct Slot {
    std::uint32_t generation = 0;
    const gt4::vugen::ProgramEntry *program = nullptr;
};
Slot g_slots[2048];
}  // namespace vu1aot

void aot_parse_environment() {
    if (vu1aot::g_verify_parsed) return;
    vu1aot::g_verify_parsed = true;
    if (const char *value = std::getenv("GT4_VU1_VERIFY")) vu1aot::g_verify_every = std::strtoull(value, nullptr, 10);
}

// Looks up the generated program for a VU1 entry. The result is cached per entry
// and per code generation, so the reachable scan and digest run once per change.
const gt4::vugen::ProgramEntry *aot_find_program(std::uint32_t entry) {
    const std::uint32_t address = entry & 16383u;
    vu1aot::Slot &slot = vu1aot::g_slots[address >> 3];
    if (slot.generation == g_generation[1]) return slot.program;
    slot.generation = g_generation[1];
    slot.program = nullptr;
    if (g_scan[1]) scan_flags(1, g_vu1_code, 16384);
    const bool flags_used = g_flags_used[1];
    const std::vector<std::uint32_t> pcs = reachable(g_vu1_code, 16383u, address);
    const std::uint64_t key = program_key(g_vu1_code, pcs, address);
    for (std::size_t i = 0; i < gt4::vugen::generated::kProgramCount; ++i) {
        const gt4::vugen::ProgramEntry &candidate = gt4::vugen::generated::kPrograms[i];
        if (candidate.key != key || candidate.entry != address) continue;
        if (candidate.image_flags_used != flags_used ||
            gt4::vurecomp::program_identity_digest(g_vu1_code, pcs, address, flags_used) != candidate.identity_sha256) {
            ++vu1aot::g_counters.identity_rejects;
            continue;
        }
        slot.program = &candidate;
        break;
    }
    return slot.program;
}

void aot_run_interpreter_from(std::uint32_t pc) {
    gt4::perf::Scope scope(gt4::perf::vu_interpreter_category(1));
    run_fast(1, pc);
}

// Runs the emitted function. BudgetExhausted leaves state committed at resume_pc,
// and the interpreter continues from there (runtime contract, vu1_aot.hpp).
void aot_run_compiled(const gt4::vugen::ProgramEntry &program) {
    gt4::vugen::RunContext context{gt4::vugen::kInterpreterInstructionBudget, 0};
    if (program.function(g_vu1, g_vu1_mem, context) == gt4::vugen::RunStatus::BudgetExhausted) {
        ++vu1aot::g_counters.budget_resumes;
        aot_run_interpreter_from(context.resume_pc);
    }
}

// Returns the first differing Vu0Regs field, or an empty string.
std::string aot_first_regs_difference(const Vu0Regs &a, const Vu0Regs &b) {
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

bool aot_same_kicks(const std::vector<KickChunk> &a, const std::vector<KickChunk> &b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].qwc != b[i].qwc || a[i].bytes != b[i].bytes) return false;
    return true;
}

// Writes the pre-call state of one mismatch: entry, Vu0Regs, VU1 memory, VU1 code image.
void aot_dump_mismatch(const Vu0Regs &before, const std::vector<std::uint8_t> &memory_before, std::uint32_t entry,
                       const Vu0Regs &compiled, const Vu0Regs &interpreted) {
    char name[64];
    std::snprintf(name, sizeof name, "vu1-mismatch-%04X.dump", entry);
    std::FILE *file = std::fopen(name, "wb");
    if (!file) return;
    const std::uint32_t header[3] = {entry, g_top, g_itop};
    std::fwrite(header, sizeof header, 1, file);
    std::fwrite(&before, sizeof before, 1, file);
    std::fwrite(&compiled, sizeof compiled, 1, file);
    std::fwrite(&interpreted, sizeof interpreted, 1, file);
    std::fwrite(memory_before.data(), 1, memory_before.size(), file);
    std::fwrite(g_vu1_code, 1, 16384, file);
    std::fclose(file);
}

// Sampled differential check: AOT first, then the interpreter from the same state.
// The interpreter result stands. Compared: all Vu0Regs fields, VU1 memory, XGKICK.
void aot_verify(const gt4::vugen::ProgramEntry &program, std::uint32_t entry) {
    // Inside an interpreter chain, interpret() only queues its target. The emitted leg must run
    // dispatch targets itself, so clear chaining for it and restore the chain afterwards.
    const bool chaining_before = g_chaining;
    const std::int64_t chained_before = g_chained;
    g_chaining = false;
    vu1aot::g_in_verify = true;
    const Vu0Regs before = g_vu1;
    const std::vector<std::uint8_t> memory_before(g_vu1_mem, g_vu1_mem + sizeof g_vu1_mem);
    std::vector<KickChunk> compiled_kicks, interpreted_kicks;

    g_kick_sink = &compiled_kicks;
    {
        gt4::perf::Scope scope(gt4::perf::Category::vu1_aot);
        aot_run_compiled(program);
    }
    g_kick_sink = nullptr;
    g_chaining = chaining_before;
    g_chained = chained_before;
    const Vu0Regs compiled_regs = g_vu1;
    const std::vector<std::uint8_t> compiled_memory(g_vu1_mem, g_vu1_mem + sizeof g_vu1_mem);

    g_vu1 = before;
    std::memcpy(g_vu1_mem, memory_before.data(), memory_before.size());
    g_kick_sink = &interpreted_kicks;
    aot_run_interpreter_from(entry);
    g_kick_sink = nullptr;

    ++vu1aot::g_counters.verified;
    std::string difference = aot_first_regs_difference(compiled_regs, g_vu1);
    if (difference.empty() && std::memcmp(compiled_memory.data(), g_vu1_mem, compiled_memory.size()) != 0) difference = "memory";
    if (difference.empty() && !aot_same_kicks(compiled_kicks, interpreted_kicks)) difference = "xgkick";
    if (!difference.empty()) {
        ++vu1aot::g_counters.mismatches;
        if (vu1aot::g_counters.mismatches <= 8)
            std::fprintf(stderr, "[vu1-verify] mismatch entry 0x%04X field %s\n", entry, difference.c_str());
    }
    // The first mismatch's pre-state goes to the working directory for offline replay.
    if (!difference.empty() && vu1aot::g_counters.mismatches == 1) aot_dump_mismatch(before, memory_before, entry, compiled_regs, g_vu1);
    for (const KickChunk &chunk : interpreted_kicks) gif::transfer(1, chunk.bytes.data(), chunk.qwc);
    vu1aot::g_in_verify = false;
}

// Test seam: when disabled, run_entry always interprets (reference runs in differential tests).
void aot_set_enabled(bool enabled) { g_aot_enabled = enabled; }

// Counter line for the performance report (native/perf_probe.cpp).
void aot_report(std::FILE *out) {
    const vu1aot::Counters &c = vu1aot::g_counters;
    std::fprintf(out, "[perf] vu1_aot compiled_calls=%llu interpreted_calls=%llu budget_resumes=%llu identity_rejects=%llu verified=%llu verify_mismatches=%llu\n",
                 static_cast<unsigned long long>(c.compiled), static_cast<unsigned long long>(c.interpreted),
                 static_cast<unsigned long long>(c.budget_resumes), static_cast<unsigned long long>(c.identity_rejects),
                 static_cast<unsigned long long>(c.verified), static_cast<unsigned long long>(c.mismatches));
}

// Returns true when the call was handled by a generated program.
bool aot_run_if_registered(std::uint32_t entry) {
    aot_parse_environment();
    const gt4::vugen::ProgramEntry *program = aot_find_program(entry);
    if (!program) {
        ++vu1aot::g_counters.interpreted;
        return false;
    }
    ++vu1aot::g_counters.compiled;
    // Periodic counter line: every GT4_VU1_REPORT_EVERY compiled calls, or every 4096 with GT4_PERF.
    static const std::uint64_t report_every = [] {
        const char *period = std::getenv("GT4_VU1_REPORT_EVERY");
        if (period) return std::strtoull(period, nullptr, 10);
        return std::getenv("GT4_PERF") ? std::uint64_t{4096} : std::uint64_t{0};
    }();
    if (report_every && vu1aot::g_counters.compiled % report_every == 0) aot_report(stderr);
    if (!vu1aot::g_in_verify && vu1aot::g_verify_every && ++vu1aot::g_verify_seen % vu1aot::g_verify_every == 0) {
        aot_verify(*program, entry & 16383u);
        return true;
    }
    gt4::perf::Scope scope(gt4::perf::Category::vu1_aot);
    aot_run_compiled(*program);
    return true;
}


//@section run_gate
    if (unit == 1 && !interpret_only && g_aot_enabled && aot_run_if_registered(pc)) return;
