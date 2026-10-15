// Command line driver.
//
//   gt4_vu_recomp [options] <input_dir> <fresh_output_dir>          corpus mode (KEY_ENTRY.bin files)
//   gt4_vu_recomp [options] --image FILE --entries FILE <fresh_output_dir>
//                                                                  explicit-entry mode (slice 155)
//
// --unit 0 (slice 160) emits the VU0 profile: explicit-entry mode only, a 4096-byte image, and the
// output vu0_generated.cpp (parts vu0_generated_NNN.cpp). Unit 1 is the default.
//
// Writes vu1_generated.cpp (functions plus kPrograms table) and manifest.json.
// Refuses an existing non-empty output directory before reading any input.
// Nothing is deleted.

#include "sha256.hpp"
#include "vurecomp.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

bool write_text(const std::filesystem::path &path, const std::string &text) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file << text;
    return static_cast<bool>(file);
}

bool read_bytes(const std::filesystem::path &path, std::vector<std::uint8_t> &bytes, std::string &error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path.filename().string();
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    // Options come first: --no-flag-elision emits the donor flag behaviour (comparison
    // builds), --namespace NAME places the table and functions in another namespace,
    // --image and --entries select explicit-entry mode.
    std::string name_space = "gt4::vugen::generated";
    std::string image_argument, entries_argument;
    // --entry-policy LABEL is recorded in the manifest (vu1_image.py writes the label with the entry list).
    // --programs-per-file N splits the functions into vu1_generated_NNN.cpp parts (0: one file).
    std::string entry_policy = "unspecified";
    std::size_t programs_per_file = 0;
    // --unit 0 selects the VU0 profile (slice 160). Only explicit-entry mode supports it.
    int unit = 1;
    int first = 1;
    while (first < argc && argv[first][0] == '-') {
        const std::string option = argv[first];
        if (option == "--no-flag-elision") {
            gt4::vurecomp::set_flag_elision(false);
            first += 1;
        } else if (option == "--unit" && first + 1 < argc) {
            unit = std::atoi(argv[first + 1]);
            if (unit != 0 && unit != 1) {
                std::fprintf(stderr, "gt4_vu_recomp: --unit takes 0 or 1\n");
                return 2;
            }
            first += 2;
        } else if (option == "--namespace" && first + 1 < argc) {
            name_space = argv[first + 1];
            first += 2;
        } else if (option == "--image" && first + 1 < argc) {
            image_argument = argv[first + 1];
            first += 2;
        } else if (option == "--entries" && first + 1 < argc) {
            entries_argument = argv[first + 1];
            first += 2;
        } else if (option == "--entry-policy" && first + 1 < argc) {
            entry_policy = argv[first + 1];
            first += 2;
        } else if (option == "--programs-per-file" && first + 1 < argc) {
            programs_per_file = std::strtoull(argv[first + 1], nullptr, 10);
            first += 2;
        } else {
            std::fprintf(stderr, "gt4_vu_recomp: unknown option %s\n", option.c_str());
            return 2;
        }
    }
    const bool explicit_mode = !image_argument.empty() || !entries_argument.empty();
    const int positional = argc - first;
    const bool usage_ok = explicit_mode ? (!image_argument.empty() && !entries_argument.empty() && positional == 1) : positional == 2;
    if (!usage_ok) {
        std::fprintf(stderr,
                     "usage: gt4_vu_recomp [--no-flag-elision] [--namespace NAME] [--programs-per-file N] <input_dir> <fresh_output_dir>\n"
                     "       gt4_vu_recomp [--no-flag-elision] [--namespace NAME] [--programs-per-file N] [--entry-policy LABEL] --image FILE --entries FILE <fresh_output_dir>\n");
        return 2;
    }
    if (unit == 0 && !explicit_mode) {
        std::fprintf(stderr, "gt4_vu_recomp: --unit 0 requires --image and --entries (explicit-entry mode)\n");
        return 2;
    }
    const gt4::vurecomp::UnitProfile &profile = gt4::vurecomp::unit_profile(unit);
    const std::filesystem::path output_dir = argv[argc - 1];
    std::string error;
    if (!gt4::vurecomp::prepare_fresh_output_directory(output_dir, error)) {
        std::fprintf(stderr, "gt4_vu_recomp: %s\n", error.c_str());
        return 1;
    }
    std::vector<gt4::vurecomp::ProgramRecord> records;
    gt4::vurecomp::ManifestInput manifest_input;
    std::string input_description;
    if (explicit_mode) {
        const std::filesystem::path image_path = image_argument;
        const std::filesystem::path entries_path = entries_argument;
        std::vector<std::uint8_t> image, entry_bytes;
        std::vector<std::uint32_t> entries;
        if (!gt4::vurecomp::read_input_image(image_path, image, error, profile.image_bytes) ||
            !gt4::vurecomp::read_entry_list(entries_path, entries, error, profile.code_mask()) ||
            !read_bytes(entries_path, entry_bytes, error) ||
            !gt4::vurecomp::collect_explicit_program_records(image, entries, records, error, profile)) {
            std::fprintf(stderr, "gt4_vu_recomp: %s\n", error.c_str());
            return 1;
        }
        manifest_input.kind = "explicit_entries";
        manifest_input.unit = unit;
        manifest_input.image_sha256 = gt4::vurecomp::sha256_hex(image);
        manifest_input.entry_list_sha256 = gt4::vurecomp::sha256_hex(entry_bytes);
        input_description = image_path.string() + " with " + std::to_string(entries.size()) + " entries";
    } else {
        const std::filesystem::path input_dir = argv[first];
        if (!gt4::vurecomp::collect_program_records(input_dir, records, error)) {
            std::fprintf(stderr, "gt4_vu_recomp: %s\n", error.c_str());
            return 1;
        }
        input_description = input_dir.string();
    }
    const std::string generated_name = profile.file_prefix + ".cpp";
    manifest_input.entry_policy = entry_policy;
    manifest_input.programs_per_file = programs_per_file;
    std::string source;
    if (programs_per_file == 0) {
        source = gt4::vurecomp::emit_generated_source(records, name_space);
        manifest_input.generated_bytes = source.size();
    } else {
        source = gt4::vurecomp::emit_generated_split(records, programs_per_file, manifest_input.parts, name_space);
        manifest_input.generated_bytes = source.size();
        for (const auto &part : manifest_input.parts) manifest_input.generated_bytes += part.text.size();
    }
    const std::string source_sha256 = gt4::vurecomp::sha256_hex(
        reinterpret_cast<const std::uint8_t *>(source.data()), source.size());
    if (!write_text(output_dir / generated_name, source) ||
        !write_text(output_dir / "manifest.json", gt4::vurecomp::manifest_json(records, generated_name, source_sha256, manifest_input))) {
        std::fprintf(stderr, "gt4_vu_recomp: cannot write outputs\n");
        return 1;
    }
    for (const auto &part : manifest_input.parts) {
        if (!write_text(output_dir / part.file_name, part.text)) {
            std::fprintf(stderr, "gt4_vu_recomp: cannot write %s\n", part.file_name.c_str());
            return 1;
        }
    }
    std::size_t emitted = 0, refused = 0;
    for (const auto &record : records) (record.refused ? refused : emitted) += 1;
    std::printf("gt4_vu_recomp: %zu programs emitted, %zu refused, %zu source bytes, from %s\n", emitted, refused, source.size(),
                input_description.c_str());
    return 0;
}
