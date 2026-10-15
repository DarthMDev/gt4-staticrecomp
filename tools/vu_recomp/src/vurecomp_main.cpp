// Command line driver: gt4_vu_recomp <input_dir> <fresh_output_dir>
//
// Writes vu1_generated.cpp (functions plus kPrograms table) and manifest.json.
// Refuses an existing non-empty output directory before reading any input.
// Nothing is deleted.

#include "sha256.hpp"
#include "vurecomp.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

bool write_text(const std::filesystem::path &path, const std::string &text) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file << text;
    return static_cast<bool>(file);
}

}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: gt4_vu_recomp <input_dir> <fresh_output_dir>\n");
        return 2;
    }
    const std::filesystem::path input_dir = argv[1];
    const std::filesystem::path output_dir = argv[2];
    std::string error;
    if (!gt4::vurecomp::prepare_fresh_output_directory(output_dir, error)) {
        std::fprintf(stderr, "gt4_vu_recomp: %s\n", error.c_str());
        return 1;
    }
    std::vector<gt4::vurecomp::ProgramRecord> records;
    if (!gt4::vurecomp::collect_program_records(input_dir, records, error)) {
        std::fprintf(stderr, "gt4_vu_recomp: %s\n", error.c_str());
        return 1;
    }
    const std::string generated_name = "vu1_generated.cpp";
    const std::string source = gt4::vurecomp::emit_generated_source(records);
    const std::string source_sha256 = gt4::vurecomp::sha256_hex(
        reinterpret_cast<const std::uint8_t *>(source.data()), source.size());
    if (!write_text(output_dir / generated_name, source) ||
        !write_text(output_dir / "manifest.json", gt4::vurecomp::manifest_json(records, generated_name, source_sha256))) {
        std::fprintf(stderr, "gt4_vu_recomp: cannot write outputs\n");
        return 1;
    }
    std::size_t emitted = 0, refused = 0;
    for (const auto &record : records) (record.refused ? refused : emitted) += 1;
    std::printf("gt4_vu_recomp: %zu programs emitted, %zu refused, from %s\n", emitted, refused, input_dir.string().c_str());
    return 0;
}
