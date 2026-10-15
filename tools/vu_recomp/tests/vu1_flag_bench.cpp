// Flag-elision micro-benchmark over the recorded VU1 corpus. Every corpus program is
// called from seeded states by two generated tables built from the same records:
// gt4::vugen::baseline (donor flag behaviour, emitted with --no-flag-elision) and
// gt4::vugen::generated (flag elision). Each call uses the same instruction budget, so
// both tables do the same work. A pair must agree on the exit state: status, resume PC,
// registers (VU1 status only when the image reads it) and VU1 memory. Time per call is
// reported for both tables. The benchmark runs outside the game.
//
// Random register states rarely reach the end of a VU1 program (the donor needs a
// budget or a real VIF state for that), so the fixed budget is the workload.
//
// usage: gt4_vu1_flag_bench [iterations_per_pass] [passes] [corpus_dir]
//        (or env GT4_VU_CORPUS_DIR for the corpus; without one the benchmark skips)
//
// Timing covers only states that do not hand off to the donor interpreter (see the
// calibration in main). Every state is still checked for equivalence.

#include "differential_support.hpp"
#include "vurecomp.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>

namespace gt4::vugen::generated {
extern const ProgramEntry kPrograms[];
extern const std::uint32_t kProgramCount;
}

namespace gt4::vugen::baseline {
extern const ProgramEntry kPrograms[];
extern const std::uint32_t kProgramCount;
}

namespace {

namespace fs = std::filesystem;
using namespace gt4::vutest;
using gt4::vugen::ProgramEntry;

// Instructions per call. The generated code charges an upper bound per backward
// transfer, so a call can end early at a loop head with BudgetExhausted.
constexpr std::uint32_t kBenchBudget = 4000u;
constexpr int kSeedsPerProgram = 2;
// Calls slower than this went through the donor interpreter and are not timed.
constexpr double kCalibrationMilliseconds = 1.0;

struct Job {
    std::uint64_t key = 0;
    std::string name;
    const ProgramEntry *elided = nullptr;
    const ProgramEntry *baseline = nullptr;
    const std::uint8_t *code = nullptr;
    SeedMode seed{};
    ps2::Vu0Regs state{};
    std::vector<std::uint8_t> memory;
};

// Exit state of one call.
struct Call {
    gt4::vugen::RunStatus status = gt4::vugen::RunStatus::Completed;
    std::uint32_t resume_pc = 0;
    ps2::Vu0Regs regs{};
    std::vector<std::uint8_t> memory;
};

std::vector<std::vector<std::uint8_t>> load_corpus(const fs::path &directory) {
    std::vector<fs::path> files;
    for (const auto &item : fs::directory_iterator(directory)) {
        if (item.path().extension() == ".bin") files.push_back(item.path());
    }
    std::sort(files.begin(), files.end());
    std::vector<std::vector<std::uint8_t>> images;
    std::set<std::vector<std::uint8_t>> seen;
    for (const fs::path &file : files) {
        std::ifstream stream(file, std::ios::binary);
        std::vector<std::uint8_t> bytes(kCodeBytes);
        stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(kCodeBytes));
        if (stream.gcount() == static_cast<std::streamsize>(kCodeBytes) && seen.insert(bytes).second) images.push_back(std::move(bytes));
    }
    return images;
}

// The table row with the same key and entry as the image, or nullptr.
const ProgramEntry *find_row(const ProgramEntry *table, std::uint32_t count, std::uint64_t key, std::uint32_t entry) {
    for (std::uint32_t i = 0; i < count; ++i)
        if (table[i].key == key && table[i].entry == entry) return &table[i];
    return nullptr;
}

// Restores the job's inputs (code, registers, memory) and runs one call with the fixed budget.
Call run_call(gt4::vugen::ProgramFunction function, const Job &job) {
    std::memcpy(ps2::g_vu1_code, job.code, kCodeBytes);
    ps2::vu::code_changed(1);
    ps2::vu::regs(1) = job.state;
    ps2::vu::regs(1).tpc = kTpcSentinel;
    std::memcpy(ps2::g_vu1_mem, job.memory.data(), job.memory.size());
    reset_kick_capture();
    gt4::vugen::RunContext context{kBenchBudget, 0};
    Call call;
    call.status = function(ps2::vu::regs(1), ps2::g_vu1_mem, context);
    call.resume_pc = context.resume_pc;
    call.regs = ps2::vu::regs(1);
    call.memory.assign(ps2::g_vu1_mem, ps2::g_vu1_mem + sizeof ps2::g_vu1_mem);
    return call;
}

// Each call starts from the job's inputs. Restoring them is not timed, and neither is a
// budget resume: only the generated function is timed. Returns nanoseconds per call.
double time_pass(const std::vector<Job> &jobs, bool elided, std::uint32_t iterations) {
    using clock = std::chrono::steady_clock;
    double total_ns = 0.0;
    for (const Job &job : jobs) {
        const gt4::vugen::ProgramFunction function = elided ? job.elided->function : job.baseline->function;
        for (std::uint32_t i = 0; i < iterations; ++i) {
            std::memcpy(ps2::g_vu1_code, job.code, kCodeBytes);
            ps2::vu::code_changed(1);
            ps2::vu::regs(1) = job.state;
            ps2::vu::regs(1).tpc = kTpcSentinel;
            std::memcpy(ps2::g_vu1_mem, job.memory.data(), job.memory.size());
            gt4::vugen::RunContext context{kBenchBudget, 0};
            const auto start = clock::now();
            (void)function(ps2::vu::regs(1), ps2::g_vu1_mem, context);
            total_ns += std::chrono::duration<double, std::nano>(clock::now() - start).count();
        }
    }
    return total_ns / (static_cast<double>(jobs.size()) * iterations);
}

}

int main(int argc, char **argv) {
    gt4::vutest::use_interpreter_reference();
    const std::uint32_t iterations = argc > 1 ? static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 200u;
    const std::uint32_t passes = argc > 2 ? static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10)) : 3u;
    std::string corpus = argc > 3 ? argv[3] : "";
    if (corpus.empty()) {
        const char *from_env = std::getenv("GT4_VU_CORPUS_DIR");
        if (from_env) corpus = from_env;
    }
    std::error_code directory_error;
    if (corpus.empty() || !fs::is_directory(corpus, directory_error)) {
        std::printf("vu1 flag bench: SKIPPED, no corpus directory (set GT4_VU_CORPUS_DIR)\n");
        return 0;
    }
    const std::uint32_t elided_count = gt4::vugen::generated::kProgramCount;
    const std::uint32_t baseline_count = gt4::vugen::baseline::kProgramCount;
    if (elided_count == 0 || elided_count != baseline_count) {
        std::printf("vu1 flag bench: FAILED, tables differ in size (%u elided, %u baseline)\n", elided_count, baseline_count);
        return 1;
    }

    // The corpus images stay alive for the whole run: jobs point into them.
    static std::vector<std::vector<std::uint8_t>> images;
    images = load_corpus(corpus);
    std::vector<Job> jobs;
    for (std::uint32_t i = 0; i < elided_count; ++i) {
        const ProgramEntry &elided = gt4::vugen::generated::kPrograms[i];
        for (const auto &image : images) {
            const std::uint64_t key = ps2::vu::program_key(image.data(), ps2::vu::reachable(image.data(), 16383u, elided.entry), elided.entry);
            if (key != elided.key) continue;
            if (gt4::vurecomp::image_flags_used(image.data(), kCodeBytes) != elided.image_flags_used) continue;
            if (gt4::vurecomp::image_status_used(image.data(), kCodeBytes) != elided.image_status_used) continue;
            const ProgramEntry *baseline = find_row(gt4::vugen::baseline::kPrograms, baseline_count, key, elided.entry);
            if (!baseline) continue;
            for (int seed = 1; seed <= kSeedsPerProgram; ++seed) {
                Job job;
                job.key = key;
                job.name = std::format("0x{:016X}@0x{:04X}", key, elided.entry);
                job.elided = &elided;
                job.baseline = baseline;
                job.code = image.data();
                job.seed = {static_cast<std::uint32_t>(seed), false};
                job.state = make_state(job.seed);
                job.memory = make_memory(job.seed);
                jobs.push_back(std::move(job));
            }
        }
    }

    // Equivalence: both tables must leave the same exit state from the same inputs.
    gt4::vutest::Counters counters;
    std::size_t budget_exits = 0;
    for (const Job &job : jobs) {
        const Call expected = run_call(job.baseline->function, job);
        const Call actual = run_call(job.elided->function, job);
        const std::string context = std::format("{} seed {}", job.name, job.seed.seed);
        expect(counters, expected.status == actual.status, context + ": run status differs");
        expect(counters, expected.resume_pc == actual.resume_pc, context + ": resume PC differs");
        const std::string difference = first_difference(expected.regs, actual.regs, gt4::vurecomp::image_status_used(job.code, kCodeBytes));
        expect(counters, difference.empty(),
               context + ": register state differs at " + difference +
                   std::format(" (status {} resume 0x{:04X}, mac baseline 0x{:04X} elided 0x{:04X})", static_cast<int>(expected.status), expected.resume_pc,
                               expected.regs.mac, actual.regs.mac));
        expect(counters, expected.memory == actual.memory, context + ": VU1 data memory differs");
        ++counters.compared;
        if (expected.status == gt4::vugen::RunStatus::BudgetExhausted) ++budget_exits;
    }

    // Calibration: a call that hands off to the donor interpreter runs up to its 1,000,000-
    // instruction cap and takes milliseconds, while a generated call under the 4,000-instruction
    // budget takes microseconds. Only states that stay under the threshold are timed.
    using clock = std::chrono::steady_clock;
    std::vector<Job> timing;
    for (const Job &job : jobs) {
        const auto start = clock::now();
        (void)run_call(job.baseline->function, job);
        if (std::chrono::duration<double, std::milli>(clock::now() - start).count() < kCalibrationMilliseconds) timing.push_back(job);
    }
    if (timing.empty()) {
        std::printf("vu1 flag bench: FAILED, no state runs without interpreter hand-off\n");
        return 1;
    }

    double best_baseline = 0.0, best_elided = 0.0;
    for (std::uint32_t pass = 0; pass < passes; ++pass) {
        const double baseline_ns = time_pass(timing, false, iterations);
        const double elided_ns = time_pass(timing, true, iterations);
        best_baseline = pass == 0 ? baseline_ns : std::min(best_baseline, baseline_ns);
        best_elided = pass == 0 ? elided_ns : std::min(best_elided, elided_ns);
    }
    std::printf("vu1 flag bench: %zu calls (%u programs x %d seeds, budget %u), %d compared, %zu budget exits, %d failures\n", jobs.size(), elided_count,
                kSeedsPerProgram, kBenchBudget, counters.compared, budget_exits, counters.failures);
    std::printf("vu1 flag bench: %zu timed calls (native-only programs), baseline %.1f ns/call, elided %.1f ns/call, ratio %.3f (best of %u passes x %u calls)\n",
                timing.size(), best_baseline, best_elided, best_baseline > 0.0 ? best_elided / best_baseline : 0.0, passes, iterations);
    return counters.failures == 0 ? 0 : 1;
}
