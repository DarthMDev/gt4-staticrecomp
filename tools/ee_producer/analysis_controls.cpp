// Offline recovery controls: analyze synthetic instructions; never execute a guest.
#include "analysis.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
constexpr std::uint32_t kMain = 0x00100000;
constexpr std::uint32_t kSupplier = kMain + 0x100;
constexpr std::uint32_t kCallback = kMain + 0x200;
constexpr std::uint32_t kDispatcher = kMain + 0x300;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

recomp::Image synthetic_image(bool literal_supplier_pointer) {
    std::vector<std::uint8_t> bytes(0x500);
    auto word = [&](std::uint32_t address, std::uint32_t value) {
        const std::size_t offset = address - kMain;
        for (unsigned lane = 0; lane < 4; ++lane)
            bytes[offset + lane] = static_cast<std::uint8_t>(value >> (8 * lane));
    };
    word(kMain, 0x03E00008);                         // jr ra
    word(kSupplier, 0x3C060010);                    // lui a2, 0x10
    word(kSupplier + 4, 0x24C60200);                // addiu a2, a2, 0x200
    word(kSupplier + 8, 0x0C0400C0);                // jal 0x00100300
    word(kSupplier + 16, 0x03E00008);               // jr ra
    word(kCallback, 0x03E00008);
    word(kDispatcher, 0x03E00008);
    if (literal_supplier_pointer) word(kMain + 0x400, kSupplier);
    return recomp::Image::raw(kMain, std::move(bytes), {kMain, kMain + 0x500});
}

void synthetic_controls() {
    const auto cold = recomp::analyse(synthetic_image(false), {kMain});
    require(!cold.is_code(kSupplier) && !cold.entries.contains(kSupplier), "Cold supplier unexpectedly recovered");
    require(!cold.is_code(kCallback), "Cold callback unexpectedly recovered");
    const auto seeded = recomp::analyse(synthetic_image(false), {kMain, kSupplier});
    require(seeded.is_code(kSupplier) && seeded.entries.contains(kSupplier), "Explicit supplier seed not recovered");
    require(seeded.is_code(kCallback) && seeded.entries.contains(kCallback), "Marked supplier constant did not expose callback");
    require(seeded.is_code(kDispatcher), "Direct call target not recovered");
    const auto pointer = recomp::analyse(synthetic_image(true), {kMain});
    require(pointer.is_code(kSupplier) && pointer.is_code(kCallback), "Raw pointer control did not expose supplier/callback");
    const auto invalid = recomp::analyse(synthetic_image(false), {kMain, kSupplier + 1, 0xFFFFFFFF});
    require(!invalid.is_code(kSupplier), "Unaligned/outside seed changed recovery");
    std::puts("PASS: cold callback, explicit seed, raw pointer and invalid-seed recovery controls");
}

void profile(const char* name, const recomp::Program& program) {
    std::size_t instructions = 0;
    for (const auto marked : program.code) instructions += marked != 0;
    std::printf("{\"profile\":\"%s\",\"instructions\":%zu,\"functions\":%zu,\"entries\":%zu,\"rejected\":%zu,\"candidates\":[",
                name, instructions, program.functions.size(), program.entries.size(), program.rejected.size());
    const std::uint32_t candidates[] = {0x0044EA58, 0x0044EAC8, 0x0044EB10};
    for (unsigned index = 0; index < 3; ++index) {
        const auto address = candidates[index];
        auto boolean = [](bool value) { return value ? "true" : "false"; };
        std::printf("%s{\"address\":\"0x%08X\",\"code\":%s,\"entry\":%s,\"function\":%s,\"rejected\":%s}",
                    index ? "," : "", address, boolean(program.is_code(address)),
                    boolean(program.entries.contains(address)), boolean(program.functions.contains(address)),
                    boolean(program.rejected.contains(address)));
    }
    std::puts("]}");
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic_controls();
            return 0;
        }
        if (argc != 3 || std::string_view(argv[1]) != "--inspect")
            throw std::runtime_error("Usage: ee_analysis_controls [--inspect verified-pinned-ELF]");
        const auto image = recomp::Image::load(argv[2]);
        profile("baseline", recomp::analyse(image, {}));
        profile("supplier_seed", recomp::analyse(image, {0x0044EAC8}));
        profile("callback_seed", recomp::analyse(image, {0x0044EA58}));
        profile("both_seeds", recomp::analyse(image, {0x0044EAC8, 0x0044EA58}));
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "ee-analysis-controls: %s\n", error.what());
        return 1;
    }
}
