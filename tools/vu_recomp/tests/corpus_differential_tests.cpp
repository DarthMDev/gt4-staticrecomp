// Differential test over a recorded VU1 corpus. The corpus directory is read at
// run time and is never copied into the repository. The emitted table is built
// from the same corpus by CMake when GT4_VU_CORPUS_DIR is configured.
//
// usage: gt4_vu_corpus_differential [corpus_dir]   (or env GT4_VU_CORPUS_DIR)
// Without a corpus or a generated table the test prints a skip reason and passes.

#include "differential_support.hpp"
#include "vurecomp.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>

namespace gt4::vugen::generated {
extern const ProgramEntry kPrograms[];
extern const std::uint32_t kProgramCount;
}

namespace {

namespace fs = std::filesystem;
using namespace gt4::vutest;

struct CorpusImage {
    std::string name;
    std::vector<std::uint8_t> bytes;
};

std::vector<CorpusImage> load_corpus(const fs::path &directory) {
    std::vector<fs::path> files;
    for (const auto &item : fs::directory_iterator(directory)) {
        if (item.path().extension() == ".bin") files.push_back(item.path());
    }
    std::sort(files.begin(), files.end());
    std::vector<CorpusImage> images;
    std::set<std::vector<std::uint8_t>> seen;
    for (const fs::path &file : files) {
        std::ifstream stream(file, std::ios::binary);
        CorpusImage image{file.filename().string(), std::vector<std::uint8_t>(kCodeBytes)};
        stream.read(reinterpret_cast<char *>(image.bytes.data()), static_cast<std::streamsize>(kCodeBytes));
        if (stream.gcount() != static_cast<std::streamsize>(kCodeBytes)) continue;
        // Recorded snapshots of one image are common. Each distinct image is run once.
        if (seen.insert(image.bytes).second) images.push_back(std::move(image));
    }
    return images;
}

}

int main(int argc, char **argv) {
    std::string corpus = argc > 1 ? argv[1] : "";
    if (corpus.empty()) {
        const char *from_env = std::getenv("GT4_VU_CORPUS_DIR");
        if (from_env) corpus = from_env;
    }
    const std::uint32_t program_count = gt4::vugen::generated::kProgramCount;
    if (corpus.empty()) {
        std::printf("vu_recomp corpus differential: SKIPPED, no corpus directory (set GT4_VU_CORPUS_DIR)\n");
        return 0;
    }
    if (program_count == 0) {
        std::printf("vu_recomp corpus differential: SKIPPED, the build has no corpus table (configure GT4_VU_CORPUS_DIR=%s)\n", corpus.c_str());
        return 0;
    }
    std::error_code directory_error;
    if (!fs::is_directory(corpus, directory_error)) {
        std::printf("vu_recomp corpus differential: SKIPPED, corpus directory is absent: %s\n", corpus.c_str());
        return 0;
    }

    Counters counters;
    const std::vector<CorpusImage> images = load_corpus(corpus);
    const std::vector<SeedMode> seeds = {{1, false}, {2, false}, {3, false}, {4, true}};
    int matched_entries = 0;
    for (std::uint32_t i = 0; i < program_count; ++i) {
        const gt4::vugen::ProgramEntry &table_entry = gt4::vugen::generated::kPrograms[i];
        int matches = 0;
        for (const CorpusImage &image : images) {
            const std::uint64_t key = ps2::vu::program_key(image.bytes.data(), ps2::vu::reachable(image.bytes.data(), 16383u, table_entry.entry), table_entry.entry);
            if (key != table_entry.key) continue;
            // Same reachable code with a different image-wide flag reader is a different program.
            if (gt4::vurecomp::image_flags_used(image.bytes.data(), kCodeBytes) != table_entry.image_flags_used) continue;
            ++matches;
            for (const SeedMode &seed : seeds)
                check_program(image.bytes.data(), table_entry.entry, table_entry.function, seed, gt4::vugen::kInterpreterInstructionBudget,
                              image.name, counters);
        }
        if (matches > 0) ++matched_entries;
        expect(counters, matches > 0, std::format("table entry 0x{:016X} at 0x{:04X} matches no corpus image", table_entry.key, table_entry.entry));
    }

    std::printf("vu_recomp corpus differential: %d images, %u programs (%d matched), %d checks, %d failures, %d compared, %d capped, %d resumed\n",
                static_cast<int>(images.size()), program_count, matched_entries, counters.checks, counters.failures, counters.compared, counters.capped,
                counters.resumed);
    return counters.failures == 0 ? 0 : 1;
}
