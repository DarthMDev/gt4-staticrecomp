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
    // Options come first: --no-flag-elision emits the donor flag behaviour (comparison
    // builds), --namespace NAME places the table and functions in another namespace.
    std::string name_space = "gt4::vugen::generated";
    int first = 1;
    while (first < argc && argv[first][0] == '-') {
        const std::string option = argv[first];
        if (option == "--no-flag-elision") {
            gt4::vurecomp::set_flag_elision(false);
            first += 1;
        } else if (option == "--namespace" && first + 1 < argc) {
            name_space = argv[first + 1];
            first += 2;
        } else {
            std::fprintf(stderr, "gt4_vu_recomp: unknown option %s\n", option.c_str());
            return 2;
        }
    }
    if (argc - first != 2) {
        std::fprintf(stderr, "usage: gt4_vu_recomp [--no-flag-elision] [--namespace NAME] <input_dir> <fresh_output_dir>\n");
        return 2;
    }
    const std::filesystem::path input_dir = argv[first];
    const std::filesystem::path output_dir = argv[first + 1];
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
    const std::string source = gt4::vurecomp::emit_generated_source(records, name_space);
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
