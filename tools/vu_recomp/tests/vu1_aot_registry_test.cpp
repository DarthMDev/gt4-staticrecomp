// Controls for the VU1 AOT registry in the runtime overlay (scripts/vu1_aot_overlay.inc):
// key, entry, image-wide flag bit and identity digest must all match before a generated
// program is used. Runs only in the AOT configuration, against the table generated from
// GT4_VU_CORPUS_DIR. Without a corpus directory argument it prints a skip reason and passes.
//
// usage: gt4_vu1_aot_registry_test [corpus_dir]

#include "differential_support.hpp"
#include "program_identity.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>

namespace ps2::vu {
const gt4::vugen::ProgramEntry *aot_find_program(std::uint32_t entry);
}

namespace gt4::vugen::generated {
extern const ProgramEntry kPrograms[];
extern const std::uint32_t kProgramCount;
}

namespace {

namespace fs = std::filesystem;
using namespace gt4::vutest;

struct Image {
    std::string name;
    std::vector<std::uint8_t> bytes;
};

std::vector<Image> load_distinct_images(const fs::path &directory) {
    std::vector<fs::path> files;
    for (const auto &item : fs::directory_iterator(directory)) {
        if (item.path().extension() == ".bin") files.push_back(item.path());
    }
    std::sort(files.begin(), files.end());
    std::vector<Image> images;
    std::set<std::vector<std::uint8_t>> seen;
    for (const fs::path &file : files) {
        std::ifstream stream(file, std::ios::binary);
        Image image{file.filename().string(), std::vector<std::uint8_t>(kCodeBytes)};
        stream.read(reinterpret_cast<char *>(image.bytes.data()), static_cast<std::streamsize>(kCodeBytes));
        if (stream.gcount() == static_cast<std::streamsize>(kCodeBytes) && seen.insert(image.bytes).second) images.push_back(std::move(image));
    }
    return images;
}

void install_image(const std::vector<std::uint8_t> &bytes) {
    std::memcpy(ps2::g_vu1_code, bytes.data(), kCodeBytes);
    ps2::vu::code_changed(1);
}

}

int main(int argc, char **argv) {
    use_interpreter_reference();
    Counters counters;
    std::string corpus = argc > 1 ? argv[1] : "";
    if (corpus.empty()) {
        if (const char *from_env = std::getenv("GT4_VU_CORPUS_DIR")) corpus = from_env;
    }
    const std::uint32_t program_count = gt4::vugen::generated::kProgramCount;
    if (corpus.empty() || program_count == 0 || !fs::is_directory(corpus)) {
        std::printf("vu1 aot registry: SKIPPED, needs a corpus directory and a corpus table (GT4_VU_CORPUS_DIR)\n");
        return 0;
    }
    const std::vector<Image> images = load_distinct_images(corpus);
    int found = 0;
    for (std::uint32_t i = 0; i < program_count; ++i) {
        const gt4::vugen::ProgramEntry &table_entry = gt4::vugen::generated::kPrograms[i];
        const Image *match = nullptr;
        for (const Image &image : images) {
            const std::uint64_t key = ps2::vu::program_key(image.bytes.data(), ps2::vu::reachable(image.bytes.data(), 16383u, table_entry.entry), table_entry.entry);
            if (key == table_entry.key) {
                match = &image;
                break;
            }
        }
        if (!match) {
            expect(counters, false, std::format("no corpus image for table entry 0x{:04X}", table_entry.entry));
            continue;
        }
        install_image(match->bytes);
        // Positive control: the live image registers the same table entry.
        expect(counters, ps2::vu::aot_find_program(table_entry.entry) == &table_entry,
               std::format("table entry 0x{:04X} is found for its own image", table_entry.entry));
        // Identity control: the digest binds the image-wide flag bit.
        const std::vector<std::uint32_t> pcs = ps2::vu::reachable(match->bytes.data(), 16383u, table_entry.entry);
        const std::string digest = gt4::vurecomp::program_identity_digest(match->bytes.data(), pcs, table_entry.entry, table_entry.image_flags_used, table_entry.image_status_used);
        expect(counters, digest == table_entry.identity_sha256, std::format("identity digest matches for entry 0x{:04X}", table_entry.entry));
        const std::string other_flag = gt4::vurecomp::program_identity_digest(match->bytes.data(), pcs, table_entry.entry, !table_entry.image_flags_used, table_entry.image_status_used);
        const std::string other_status = gt4::vurecomp::program_identity_digest(match->bytes.data(), pcs, table_entry.entry, table_entry.image_flags_used, !table_entry.image_status_used);
        expect(counters, other_status != table_entry.identity_sha256, std::format("identity digest changes with the status bit for entry 0x{:04X}", table_entry.entry));
        expect(counters, other_flag != table_entry.identity_sha256, std::format("identity digest changes with the flag bit for entry 0x{:04X}", table_entry.entry));
        // Key control: a changed reachable word is a different program and must not match.
        std::vector<std::uint8_t> changed = match->bytes;
        changed[table_entry.entry] ^= 0x01u;
        install_image(changed);
        expect(counters, ps2::vu::aot_find_program(table_entry.entry) == nullptr,
               std::format("a changed reachable word is not registered for entry 0x{:04X}", table_entry.entry));
        install_image(match->bytes);
        ++found;
    }
    std::printf("vu1 aot registry: %d of %u table entries found, %d checks, %d failures\n", found, program_count, counters.checks, counters.failures);
    return counters.failures == 0 ? 0 : 1;
}
