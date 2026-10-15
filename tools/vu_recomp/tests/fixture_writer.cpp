// Writes the authored fixtures into a fresh corpus directory using the recorded
// file naming scheme KEY16HEX_ENTRY4HEX.bin. Used by the build to feed gt4_vu_recomp.
//
// usage: gt4_vu_fixture_writer <fresh_corpus_dir>

#include "vu_fixtures.hpp"
#include "ps2/vu_ops.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <format>

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: gt4_vu_fixture_writer <fresh_corpus_dir>\n");
        return 2;
    }
    const std::filesystem::path directory = argv[1];
    std::error_code error;
    if (std::filesystem::exists(directory, error) && !std::filesystem::is_empty(directory, error)) {
        std::fprintf(stderr, "fixture directory must be fresh: %s\n", argv[1]);
        return 1;
    }
    std::filesystem::create_directories(directory, error);
    if (error) return 1;
    std::size_t written = 0;
    for (const auto &fixture : gt4::vurecomp::fixtures::build_fixtures()) {
        for (std::uint32_t entry : fixture.entries) {
            const std::uint64_t key = ps2::vu::program_key(fixture.image.data(), ps2::vu::reachable(fixture.image.data(), 16383u, entry), entry);
            const std::string name = std::format("{:016X}_{:04X}.bin", key, entry);
            std::ofstream file(directory / name, std::ios::binary);
            file.write(reinterpret_cast<const char *>(fixture.image.data()), static_cast<std::streamsize>(fixture.image.size()));
            if (!file) return 1;
            ++written;
        }
    }
    std::printf("wrote %zu fixture images to %s\n", written, argv[1]);
    return 0;
}
