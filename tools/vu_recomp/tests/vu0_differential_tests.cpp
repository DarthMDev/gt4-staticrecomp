// VU0 differential controls (slice 160, TASK-013). For each of the table's VCALLMS entries, the emitted
// VU0 function is compared with the donor exact interpreter run(0, entry) from identical state:
//   - finite-valued and raw-bit initial state, 1000 seeds each (every Vu0Regs field and the 4 KiB data memory);
//   - a budget sweep: small budgets force BudgetExhausted and the exact resume;
//   - a negative control: the donor fast path interpret(0, pc) does not write MAC and status for this
//     image (no flag reader), so it must differ from the exact path somewhere. This shows why hand-offs
//     use run(0, pc) and not interpret(0, pc).
//
// Needs the VU0 AOT capsule (GT4_VU0_GENERATED_DIR) and the pinned image (argument or GT4_VU0_IMAGE).
// Without them it prints a skip reason and passes.
//
// usage: gt4_vu0_differential_tests [vu0_image.bin]

#include "differential_support.hpp"
#include "program_identity.hpp"
#include "ps2/system.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace ps2::vu {
void vu0aot_set_enabled(bool enabled);
const gt4::vugen::ProgramEntry *vu0aot_find_program(std::uint32_t entry);
void vu0aot_run_compiled(const gt4::vugen::ProgramEntry &program, std::uint32_t budget);
}

namespace gt4::vugen::vu0 {
extern const ProgramEntry kPrograms[];
extern const std::uint32_t kProgramCount;
}

namespace {

using namespace gt4::vutest;
constexpr std::size_t kVu0Bytes = 4096;
constexpr std::uint32_t kSeeds = 1000;
constexpr std::uint32_t kBudgetSeeds = 64;

struct Vu0Outcome {
    ps2::Vu0Regs regs{};
    std::vector<std::uint8_t> memory;
    bool completed = false;
};

void install(const std::vector<std::uint8_t> &image, const ps2::Vu0Regs &state, const std::vector<std::uint8_t> &memory) {
    std::memcpy(ps2::g_vu0_code, image.data(), kVu0Bytes);
    ps2::vu::code_changed(0);
    ps2::g_cpu.vu0 = state;
    ps2::g_cpu.vu0.tpc = kTpcSentinel;
    std::memcpy(ps2::g_vu0_mem, memory.data(), kVu0Bytes);
}

Vu0Outcome vu0_capture() {
    Vu0Outcome outcome;
    outcome.regs = ps2::g_cpu.vu0;
    outcome.memory.assign(ps2::g_vu0_mem, ps2::g_vu0_mem + kVu0Bytes);
    outcome.completed = outcome.regs.tpc != kTpcSentinel;
    return outcome;
}

Vu0Outcome reference_run(const std::vector<std::uint8_t> &image, std::uint32_t entry, const ps2::Vu0Regs &state, const std::vector<std::uint8_t> &memory) {
    install(image, state, memory);
    ps2::vu::vu0aot_set_enabled(false);
    ps2::vu::run(0, entry);
    ps2::vu::vu0aot_set_enabled(true);
    return vu0_capture();
}

Vu0Outcome generated_run(const std::vector<std::uint8_t> &image, const gt4::vugen::ProgramEntry &program, const ps2::Vu0Regs &state,
                      const std::vector<std::uint8_t> &memory, std::uint32_t budget) {
    install(image, state, memory);
    ps2::vu::vu0aot_run_compiled(program, budget);
    return vu0_capture();
}

// Returns the first difference between two outcomes (every Vu0Regs field, then the data memory).
std::string difference(const Vu0Outcome &a, const Vu0Outcome &b) {
    std::string field = first_difference(a.regs, b.regs, true);
    if (!field.empty()) return field;
    if (a.memory != b.memory) return "memory";
    return {};
}

std::vector<std::uint8_t> read_file(const std::filesystem::path &path, bool &ok) {
    std::ifstream file(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    ok = file && bytes.size() == kVu0Bytes;
    return bytes;
}

}  // namespace

int main(int argc, char **argv) {
    std::string image_path = argc > 1 ? argv[1] : "";
    if (image_path.empty()) {
        if (const char *from_env = std::getenv("GT4_VU0_IMAGE")) image_path = from_env;
    }
    const std::uint32_t program_count = gt4::vugen::vu0::kProgramCount;
    if (image_path.empty() || program_count == 0) {
        std::printf("vu0 differential: SKIPPED, needs the VU0 table (GT4_VU0_GENERATED_DIR) and the pinned image (GT4_VU0_IMAGE)\n");
        return 0;
    }
    bool image_ok = false;
    const std::vector<std::uint8_t> image = read_file(image_path, image_ok);
    if (!image_ok) {
        std::fprintf(stderr, "vu0 differential: cannot read a 4096-byte image from %s\n", image_path.c_str());
        return 1;
    }

    Counters counters;
    std::uint64_t compared = 0, budget_resumes_checked = 0;
    int fast_path_differences = 0;
    for (std::uint32_t i = 0; i < program_count; ++i) {
        const gt4::vugen::ProgramEntry &program_entry = gt4::vugen::vu0::kPrograms[i];
        const std::uint32_t entry = program_entry.entry;
        install(image, make_state(SeedMode{0, false}), make_memory(SeedMode{0, false}));
        const gt4::vugen::ProgramEntry *registered = ps2::vu::vu0aot_find_program(entry);
        expect(counters, registered == &program_entry, std::format("table entry 0x{:03X} is registered for the image", entry));
        if (registered != &program_entry) continue;

        for (int raw = 0; raw < 2; ++raw) {
            for (std::uint32_t seed = 1; seed <= kSeeds; ++seed) {
                const SeedMode mode{seed, raw == 1};
                ps2::Vu0Regs state = make_state(mode);
                std::vector<std::uint8_t> memory = make_memory(mode);
                memory.resize(kVu0Bytes);
                const Vu0Outcome reference = reference_run(image, entry, state, memory);
                if (!reference.completed) {
                    ++counters.capped;
                    continue;
                }
                const Vu0Outcome generated = generated_run(image, program_entry, state, memory, gt4::vugen::kInterpreterInstructionBudget);
                ++compared;
                const std::string field = difference(reference, generated);
                expect(counters, field.empty(), std::format("entry 0x{:03X} {} seed {}: first difference {}", entry, raw ? "raw" : "finite", seed, field));
            }
        }

        // Budget sweep: the generated code stops at a random budget and resumes exactly on the interpreter path.
        for (std::uint32_t seed = 1; seed <= kBudgetSeeds; ++seed) {
            const SeedMode mode{seed * 7919u, false};
            ps2::Vu0Regs state = make_state(mode);
            std::vector<std::uint8_t> memory = make_memory(mode);
            memory.resize(kVu0Bytes);
            const Vu0Outcome reference = reference_run(image, entry, state, memory);
            const std::uint32_t budget = 1u + (seed % 24u);
            const Vu0Outcome generated = generated_run(image, program_entry, state, memory, budget);
            ++budget_resumes_checked;
            const std::string field = difference(reference, generated);
            expect(counters, field.empty(), std::format("entry 0x{:03X} budget {} seed {}: first difference {}", entry, budget, seed, field));
        }

        // Negative control: the donor fast path does not keep MAC and status for this image. A hand-off
        // through interpret(0, pc) would therefore differ from the exact path on some state.
        for (std::uint32_t seed = 1; seed <= kBudgetSeeds; ++seed) {
            const SeedMode mode{seed * 104729u, false};
            ps2::Vu0Regs state = make_state(mode);
            std::vector<std::uint8_t> memory = make_memory(mode);
            memory.resize(kVu0Bytes);
            const Vu0Outcome reference = reference_run(image, entry, state, memory);
            install(image, state, memory);
            ps2::vu::interpret(0, entry);
            const Vu0Outcome fast = vu0_capture();
            if (reference.completed && (fast.regs.mac != reference.regs.mac || fast.regs.status != reference.regs.status)) ++fast_path_differences;
        }
    }
    expect(counters, fast_path_differences > 0, "negative control: interpret(0, pc) differs from run(0, pc) in MAC or status");
    std::printf("vu0 differential: %u entries, %llu finite/raw seeds compared, %llu budget resumes checked, %d capped, fast-path negative control %d differing states, %d checks, %d failures\n",
                program_count, static_cast<unsigned long long>(compared), static_cast<unsigned long long>(budget_resumes_checked), counters.capped,
                fast_path_differences, counters.checks, counters.failures);
    return counters.failures == 0 ? 0 : 1;
}
