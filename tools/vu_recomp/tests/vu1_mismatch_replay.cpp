// Replays one VU1 verification mismatch dumped by the runtime (vu1-mismatch-EEEE.dump).
// The donor interpreter and the emitted function run offline from the dumped pre-state,
// and the results are compared with the in-game results recorded in the dump.
//
// usage: gt4_vu1_mismatch_replay <dump file>

#include "differential_support.hpp"

#include <cstdio>
#include <cstdlib>

namespace gt4::vugen::generated {
extern const ProgramEntry kPrograms[];
extern const std::uint32_t kProgramCount;
}

namespace {

// Layout written by aot_dump_mismatch: header {entry, top, itop}, the pre-call Vu0Regs,
// the in-game emitted result, the in-game interpreter result, VU1 memory, VU1 code image.
struct Dump {
    std::uint32_t header[3] = {0, 0, 0};
    ps2::Vu0Regs before{};
    ps2::Vu0Regs in_game_emitted{};
    ps2::Vu0Regs in_game_interpreted{};
    std::vector<std::uint8_t> memory = std::vector<std::uint8_t>(16384);
    std::vector<std::uint8_t> image = std::vector<std::uint8_t>(16384);
};

bool read_dump(const char *path, Dump &dump) {
    std::FILE *file = std::fopen(path, "rb");
    if (!file) return false;
    const bool complete = std::fread(dump.header, sizeof dump.header, 1, file) == 1 &&
                          std::fread(&dump.before, sizeof dump.before, 1, file) == 1 &&
                          std::fread(&dump.in_game_emitted, sizeof dump.in_game_emitted, 1, file) == 1 &&
                          std::fread(&dump.in_game_interpreted, sizeof dump.in_game_interpreted, 1, file) == 1 &&
                          std::fread(dump.memory.data(), 1, dump.memory.size(), file) == dump.memory.size() &&
                          std::fread(dump.image.data(), 1, dump.image.size(), file) == dump.image.size();
    std::fclose(file);
    return complete;
}

std::string describe(const std::string &difference) { return difference.empty() ? "equal" : difference; }

}

int main(int argc, char **argv) {
    gt4::vutest::use_interpreter_reference();
    if (argc != 2) {
        std::fprintf(stderr, "usage: gt4_vu1_mismatch_replay <dump file>\n");
        return 2;
    }
    Dump dump;
    if (!read_dump(argv[1], dump)) {
        std::fprintf(stderr, "cannot read a complete dump from %s\n", argv[1]);
        return 2;
    }
    const std::uint32_t entry = dump.header[0];
    ps2::vu::set_tops(dump.header[1], dump.header[2]);
    const std::uint64_t key = ps2::vu::program_key(dump.image.data(), ps2::vu::reachable(dump.image.data(), 16383u, entry), entry);
    gt4::vugen::ProgramFunction function = nullptr;
    for (std::uint32_t i = 0; i < gt4::vugen::generated::kProgramCount; ++i) {
        const gt4::vugen::ProgramEntry &program = gt4::vugen::generated::kPrograms[i];
        if (program.key == key && program.entry == entry) function = program.function;
    }
    if (!function) {
        std::printf("no emitted program for entry 0x%04X key 0x%016llX in this build\n", entry, static_cast<unsigned long long>(key));
        return 1;
    }
    const gt4::vutest::Outcome reference = gt4::vutest::run_reference(dump.image.data(), entry, dump.before, dump.memory);
    bool resumed = false;
    // In-game, dispatch targets re-enter the runtime and may run emitted programs. Replay that
    // when the build provides the switch: enable the gate for the emitted leg only.
#ifdef GT4_VU1_AOT
    ps2::vu::aot_set_enabled(true);
#endif
    const gt4::vutest::Outcome emitted = gt4::vutest::run_generated(function, dump.image.data(), dump.before, dump.memory,
                                                                     gt4::vugen::kInterpreterInstructionBudget, resumed);
#ifdef GT4_VU1_AOT
    ps2::vu::aot_set_enabled(false);
#endif
    std::printf("entry 0x%04X top=0x%04X itop=0x%04X\n", entry, dump.header[1], dump.header[2]);
    std::printf("  offline emitted vs offline reference: %s\n", describe(gt4::vutest::first_difference(reference.regs, emitted.regs)).c_str());
    std::printf("  offline reference vs in-game interpreter: %s\n",
                describe(gt4::vutest::first_difference(reference.regs, dump.in_game_interpreted)).c_str());
    std::printf("  offline emitted vs in-game emitted: %s\n",
                describe(gt4::vutest::first_difference(emitted.regs, dump.in_game_emitted)).c_str());
    return gt4::vutest::first_difference(reference.regs, emitted.regs).empty() ? 0 : 1;
}
