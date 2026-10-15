// Differential test on the authored fixtures. Compares every emitted fixture
// program with the donor interpreter over finite and raw seeds, and runs the
// budget controls on the loop fixture.

#include "differential_support.hpp"
#include "vu_fixtures.hpp"

namespace gt4::vugen::generated {
extern const ProgramEntry kPrograms[];
extern const std::uint32_t kProgramCount;
}

namespace {

using gt4::vurecomp::fixtures::Fixture;
using gt4::vugen::RunContext;
using gt4::vugen::RunStatus;
using namespace gt4::vutest;

gt4::vugen::ProgramFunction find_function(const std::vector<std::uint8_t> &code, std::uint32_t entry, std::uint64_t &key_out) {
    const std::uint64_t key = ps2::vu::program_key(code.data(), ps2::vu::reachable(code.data(), 16383u, entry), entry);
    key_out = key;
    for (std::uint32_t i = 0; i < gt4::vugen::generated::kProgramCount; ++i) {
        const gt4::vugen::ProgramEntry &program = gt4::vugen::generated::kPrograms[i];
        if (program.key == key && program.entry == entry) return program.function;
    }
    return nullptr;
}

// Exhaustion must stop before a segment, report its loop head, and let the
// interpreter finish from there with the same final state as the full run.
void check_budget(Counters &counters, const Fixture &fixture, std::uint32_t entry, gt4::vugen::ProgramFunction function, std::uint32_t budget,
                  std::uint32_t expected_resume, SeedMode mode) {
    const std::string context = std::format("{} budget {} seed {}", fixture.name, budget, mode.seed);
    const ps2::Vu0Regs state = make_state(mode);
    const std::vector<std::uint8_t> memory = make_memory(mode);
    const Outcome expected = run_reference(fixture.image.data(), entry, state, memory);

    load_state(fixture.image.data(), state, memory);
    RunContext limited{budget, 0xFFFFFFFFu};
    const RunStatus status = function(ps2::vu::regs(1), ps2::g_vu1_mem, limited);
    expect(counters, status == RunStatus::BudgetExhausted, context + ": expected BudgetExhausted");
    expect(counters, limited.resume_pc == expected_resume, context + std::format(": resume PC 0x{:04X}, expected 0x{:04X}", limited.resume_pc, expected_resume));
    ps2::vu::interpret(1, limited.resume_pc);
    compare_outcomes(expected, capture(), context + " (resumed)", counters);
}

}

int main() {
    Counters counters;
    const std::vector<Fixture> fixtures = gt4::vurecomp::fixtures::build_fixtures();
    const std::uint32_t program_count = gt4::vugen::generated::kProgramCount;
    expect(counters, program_count == 7, std::format("emitted program count {}, expected 7 (5 recorded plus 2 jump targets)", program_count));

    for (const Fixture &fixture : fixtures) {
        for (std::uint32_t entry : fixture.entries) {
            std::uint64_t key = 0;
            const gt4::vugen::ProgramFunction function = find_function(fixture.image, entry, key);
            if (fixture.expect_refused) {
                expect(counters, function == nullptr, fixture.name + ": refused program must not be emitted");
                continue;
            }
            expect(counters, function != nullptr, fixture.name + ": recorded program missing from table");
        }
    }
    for (std::uint32_t i = 0; i < program_count; ++i) {
        const gt4::vugen::ProgramEntry &table_entry = gt4::vugen::generated::kPrograms[i];
        bool matched = false;
        for (const Fixture &fixture : fixtures) {
            const std::uint64_t key = ps2::vu::program_key(fixture.image.data(), ps2::vu::reachable(fixture.image.data(), 16383u, table_entry.entry), table_entry.entry);
            if (key != table_entry.key) continue;
            matched = true;
            for (std::uint32_t seed = 1; seed <= 3; ++seed) check_program(fixture.image.data(), table_entry.entry, table_entry.function, {seed, false}, gt4::vugen::kInterpreterInstructionBudget, fixture.name, counters);
            check_program(fixture.image.data(), table_entry.entry, table_entry.function, {4, true}, gt4::vugen::kInterpreterInstructionBudget, fixture.name, counters);
        }
        expect(counters, matched, std::format("table entry 0x{:016X} at 0x{:04X} matches no authored image", table_entry.key, table_entry.entry));
    }

    const Fixture &loop = fixtures[0];
    std::uint64_t loop_key = 0;
    const gt4::vugen::ProgramFunction loop_function = find_function(loop.image, 0, loop_key);
    expect(counters, loop_function != nullptr, "loop fixture entry 0 must be emitted for budget controls");
    if (loop_function) {
        check_budget(counters, loop, 0, loop_function, 0, 0x0000u, {1, false});
        check_budget(counters, loop, 0, loop_function, 12, 0x0008u, {1, false});
        check_budget(counters, loop, 0, loop_function, 12, 0x0008u, {2, false});
    }

    std::printf("vu_recomp fixture differential: %d checks, %d failures, %d compared, %d capped, %d resumed, %u emitted programs\n", counters.checks,
                counters.failures, counters.compared, counters.capped, counters.resumed, program_count);
    return counters.failures == 0 ? 0 : 1;
}
