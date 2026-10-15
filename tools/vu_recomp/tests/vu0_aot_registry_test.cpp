// Controls for the VU0 AOT registry in the runtime overlay (scripts/vu0_aot_overlay.inc, slice 160).
// A generated VU0 program is used only when its key, entry, flag and status bits and identity digest
// all match the live VU0 code image. The run(0, entry) gate uses the same lookup.
//
// Controls: every table entry is found for its own image and its digest is the table digest (VU0 tag);
// a changed reachable word is not registered; a change in padding that no entry reaches keeps the hit;
// restoring the image restores the hit (code_changed(0) invalidates the cache); the gate counts a
// compiled call for a normalized entry, and a VF0 that is not normalized takes the exact interpreter.
//
// usage: gt4_vu0_aot_registry_test [vu0_image.bin]   (default: GT4_VU0_IMAGE)

#include "differential_support.hpp"
#include "program_identity.hpp"
#include "ps2/system.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>

namespace ps2::vu {
const gt4::vugen::ProgramEntry *vu0aot_find_program(std::uint32_t entry);
void vu0aot_set_enabled(bool enabled);
std::uint64_t vu0aot_counter(const char *name);
}

namespace gt4::vugen::vu0 {
extern const ProgramEntry kPrograms[];
extern const std::uint32_t kProgramCount;
}

namespace {

using namespace gt4::vutest;
constexpr std::size_t kVu0Bytes = 4096;
constexpr std::uint32_t kUploadBytes = 0xA80;

void install_image(const std::vector<std::uint8_t> &image) {
    std::memcpy(ps2::g_vu0_code, image.data(), kVu0Bytes);
    ps2::vu::code_changed(0);
}

}  // namespace

int main(int argc, char **argv) {
    std::string image_path = argc > 1 ? argv[1] : "";
    if (image_path.empty()) {
        if (const char *from_env = std::getenv("GT4_VU0_IMAGE")) image_path = from_env;
    }
    const std::uint32_t program_count = gt4::vugen::vu0::kProgramCount;
    if (image_path.empty() || program_count == 0) {
        std::printf("vu0 aot registry: SKIPPED, needs the VU0 table (GT4_VU0_GENERATED_DIR) and the pinned image (GT4_VU0_IMAGE)\n");
        return 0;
    }
    std::ifstream file(image_path, std::ios::binary);
    std::vector<std::uint8_t> image((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!file || image.size() != kVu0Bytes) {
        std::fprintf(stderr, "vu0 aot registry: cannot read a 4096-byte image from %s\n", image_path.c_str());
        return 1;
    }

    use_interpreter_reference();
    Counters counters;
    int found = 0;

    // Padding probe: an address inside the zero padding that no table entry reaches.
    std::vector<bool> reachable_byte(kVu0Bytes, false);
    for (std::uint32_t i = 0; i < program_count; ++i) {
        for (std::uint32_t pc : ps2::vu::reachable(image.data(), 4095u, gt4::vugen::vu0::kPrograms[i].entry)) {
            reachable_byte[pc] = true;
            reachable_byte[(pc + 4) & 4095u] = true;
        }
    }
    const std::uint32_t padding_probe = kUploadBytes + 0x10u;
    expect(counters, !reachable_byte[padding_probe], "the padding after the upload is not reachable from any table entry");

    install_image(image);
    for (std::uint32_t i = 0; i < program_count; ++i) {
        const gt4::vugen::ProgramEntry &table_entry = gt4::vugen::vu0::kPrograms[i];
        const std::uint32_t entry = table_entry.entry;
        install_image(image);
        const bool hit = ps2::vu::vu0aot_find_program(entry) == &table_entry;
        expect(counters, hit, std::format("table entry 0x{:03X} is found for its own image", entry));
        if (hit) ++found;

        // Identity control: the digest binds the VU0 tag, the forced flag and status bits, and the reachable words.
        const std::vector<std::uint32_t> pcs = ps2::vu::reachable(image.data(), 4095u, entry);
        const std::string digest = gt4::vurecomp::program_identity_digest(image.data(), pcs, entry, true, true, "GT4-VU0-PROGRAM-IDENTITY-1");
        expect(counters, digest == table_entry.identity_sha256, std::format("identity digest matches for entry 0x{:03X}", entry));
        const std::string vu1_tag_digest = gt4::vurecomp::program_identity_digest(image.data(), pcs, entry, true, true);
        expect(counters, vu1_tag_digest != table_entry.identity_sha256, std::format("the VU1 identity tag does not match entry 0x{:03X}", entry));

        // Key control: a changed reachable word is a different program and is not registered.
        std::vector<std::uint8_t> changed = image;
        changed[entry] ^= 0x01u;
        install_image(changed);
        expect(counters, ps2::vu::vu0aot_find_program(entry) == nullptr, std::format("a changed reachable word is not registered for entry 0x{:03X}", entry));

        // Restoring the image restores the hit: code_changed(0) invalidates the per-entry cache.
        install_image(image);
        expect(counters, ps2::vu::vu0aot_find_program(entry) == &table_entry, std::format("restoring the image restores entry 0x{:03X}", entry));

        // Outside control: a change in padding that no entry reaches keeps the hit.
        std::vector<std::uint8_t> padded = image;
        padded[padding_probe] ^= 0xFFu;
        install_image(padded);
        expect(counters, ps2::vu::vu0aot_find_program(entry) == &table_entry, std::format("a change outside the reachable set keeps entry 0x{:03X}", entry));
    }

    // Gate controls on the first entry. A normalized VF0 and VI0 = 0 take the compiled path; a VF0 that is
    // not normalized at entry takes the exact interpreter path (counted as interpreted).
    const gt4::vugen::ProgramEntry &first = gt4::vugen::vu0::kPrograms[0];
    install_image(image);
    ps2::vu::vu0aot_set_enabled(true);
    ps2::g_cpu.vu0 = make_state(SeedMode{1, false});
    ps2::g_cpu.vu0.tpc = kTpcSentinel;
    const std::uint64_t compiled_before = ps2::vu::vu0aot_counter("compiled");
    ps2::vu::run(0, first.entry);
    expect(counters, ps2::vu::vu0aot_counter("compiled") == compiled_before + 1, "the run(0) gate counts a compiled call for a normalized entry");

    ps2::g_cpu.vu0 = make_state(SeedMode{2, false});
    ps2::g_cpu.vu0.vf[0].w[0] = 1u;
    ps2::g_cpu.vu0.tpc = kTpcSentinel;
    const std::uint64_t interpreted_before = ps2::vu::vu0aot_counter("interpreted");
    ps2::vu::run(0, first.entry);
    expect(counters, ps2::vu::vu0aot_counter("interpreted") == interpreted_before + 1, "a VF0 that is not normalized at entry takes the exact interpreter path");

    std::printf("vu0 aot registry: %d of %u table entries found, %d checks, %d failures\n", found, program_count, counters.checks, counters.failures);
    return counters.failures == 0 ? 0 : 1;
}
