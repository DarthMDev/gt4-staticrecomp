// Controls for the emitter's input, output, identity and text rules. These run
// without the donor runtime. Each case uses its own temporary directory.

#include "sha256.hpp"
#include "vu_fixtures.hpp"
#include "vurecomp.hpp"

#include "ps2/vu_ops.hpp"

#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace gt4::vurecomp;

namespace {

int g_failures = 0;
int g_checks = 0;

void expect(bool condition, const std::string &message) {
    ++g_checks;
    if (condition) return;
    ++g_failures;
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
}

fs::path fresh_temp_directory(const std::string &name) {
    const fs::path directory = fs::temp_directory_path() / ("gt4_vu_recomp_core_" + name);
    std::error_code ignored;
    fs::remove_all(directory, ignored);
    fs::create_directories(directory);
    return directory;
}

void write_bytes(const fs::path &path, const std::vector<std::uint8_t> &bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::string recorded_name(const std::vector<std::uint8_t> &image, std::uint32_t entry) {
    const std::uint64_t key = ps2::vu::program_key(image.data(), ps2::vu::reachable(image.data(), 16383u, entry), entry);
    return std::format("{:016X}_{:04X}.bin", key, entry);
}

void check_sha256() {
    expect(sha256_hex(nullptr, 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 of empty input");
    const std::string abc = "abc";
    expect(sha256_hex(reinterpret_cast<const std::uint8_t *>(abc.data()), abc.size()) ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
           "sha256 of abc");
    const std::string two_block = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    expect(sha256_hex(reinterpret_cast<const std::uint8_t *>(two_block.data()), two_block.size()) ==
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
           "sha256 of the 56-byte two-block vector");
}

void check_names() {
    std::uint64_t key = 0;
    std::uint32_t entry = 0;
    std::string error;
    expect(parse_input_file_name("0123456789ABCDEF_0040.bin", key, entry, error) && key == 0x0123456789ABCDEFull && entry == 0x40u,
           "valid recorded name is accepted");
    expect(!parse_input_file_name("0123456789abcdef_0040.bin", key, entry, error), "lowercase key digits are refused");
    expect(!parse_input_file_name("0123456789ABCDEF_0044.txt", key, entry, error), "wrong suffix is refused");
    expect(!parse_input_file_name("0123456789ABCDE_0040.bin", key, entry, error), "short key is refused");
    expect(!parse_input_file_name("0123456789ABCDEF-0040.bin", key, entry, error), "missing separator is refused");
    expect(!parse_input_file_name("0123456789ABCDEF_0041.bin", key, entry, error), "unaligned entry is refused");
    expect(!parse_input_file_name("0123456789ABCDEF_4000.bin", key, entry, error), "entry beyond the 16 KiB image is refused");
}

void check_image_sizes() {
    const fs::path directory = fresh_temp_directory("sizes");
    std::vector<std::uint8_t> image;
    std::string error;
    write_bytes(directory / "short.bin", std::vector<std::uint8_t>(kImageBytes - 1, 0));
    expect(!read_input_image(directory / "short.bin", image, error), "16383-byte input is refused");
    write_bytes(directory / "long.bin", std::vector<std::uint8_t>(kImageBytes + 1, 0));
    expect(!read_input_image(directory / "long.bin", image, error), "16385-byte input is refused");
    write_bytes(directory / "exact.bin", std::vector<std::uint8_t>(kImageBytes, 0x5Au));
    expect(read_input_image(directory / "exact.bin", image, error) && image.size() == kImageBytes && image[7] == 0x5Au,
           "exact 16384-byte input is read in full");
}

void check_collection_rejections() {
    const std::vector<gt4::vurecomp::fixtures::Fixture> fixtures = gt4::vurecomp::fixtures::build_fixtures();
    const std::vector<std::uint8_t> &loop = fixtures[0].image;
    std::string error;

    const fs::path bad_name = fresh_temp_directory("bad_name");
    write_bytes(bad_name / "not_a_program.bin", loop);
    std::vector<ProgramRecord> records;
    expect(!collect_program_records(bad_name, records, error), "unexpected input name is refused");

    const fs::path mismatch = fresh_temp_directory("key_mismatch");
    const std::string wrong_name = "0000000000000001_0000.bin";
    write_bytes(mismatch / wrong_name, loop);
    records.clear();
    expect(!collect_program_records(mismatch, records, error), "key mismatch is refused");

    const fs::path short_image = fresh_temp_directory("short_image");
    write_bytes(short_image / recorded_name(loop, 0), std::vector<std::uint8_t>(kImageBytes - 2, 0));
    records.clear();
    expect(!collect_program_records(short_image, records, error), "wrong-size file is refused");

    const fs::path good = fresh_temp_directory("good");
    write_bytes(good / recorded_name(loop, 0), loop);
    records.clear();
    expect(collect_program_records(good, records, error), "valid recorded file is collected: " + error);
    bool found_recorded = false, found_target = false;
    for (const ProgramRecord &record : records) {
        found_recorded = found_recorded || (record.origin == "recorded" && record.entry == 0);
        found_target = found_target || (record.origin == "jump_target" && record.entry == 32);
    }
    expect(found_recorded, "recorded entry 0 is present");
    expect(found_target, "jump target 32 is derived from the vi0-based constant load");
}

void check_collision_refusal() {
    std::vector<ProgramRecord> records;
    std::string error;
    ProgramRecord first;
    first.key = 0xABCDull;
    first.entry = 0;
    first.digest_sha256 = std::string(64, 'a');
    expect(add_program_record(records, first, error), "first record is added");
    ProgramRecord duplicate = first;
    expect(add_program_record(records, duplicate, error) && records.size() == 1, "identical identity is treated as a duplicate");
    ProgramRecord collision = first;
    collision.digest_sha256 = std::string(64, 'b');
    expect(!add_program_record(records, collision, error), "equal key with different identity is refused");
    expect(error.find("collision") != std::string::npos, "collision refusal names the collision");
    expect(records.size() == 1, "a refused collision is not stored");
}

void check_output_directory_rules() {
    const fs::path directory = fresh_temp_directory("output");
    std::string error;
    expect(prepare_fresh_output_directory(directory / "new", error) && fs::is_directory(directory / "new"), "missing output directory is created");
    fs::create_directories(directory / "populated");
    write_bytes(directory / "populated" / "vu1_0000000000000000.cpp", {'/', '/'});
    expect(!prepare_fresh_output_directory(directory / "populated", error), "non-empty output directory is refused");
    expect(fs::exists(directory / "populated" / "vu1_0000000000000000.cpp"), "refusal does not delete existing files");
}

void check_emission() {
    const std::vector<gt4::vurecomp::fixtures::Fixture> fixtures = gt4::vurecomp::fixtures::build_fixtures();
    const fs::path corpus = fresh_temp_directory("emission_corpus");
    for (const auto &fixture : fixtures)
        for (std::uint32_t entry : fixture.entries) write_bytes(corpus / recorded_name(fixture.image, entry), fixture.image);

    std::vector<ProgramRecord> records;
    std::string error;
    expect(collect_program_records(corpus, records, error), "fixture corpus is collected: " + error);
    const std::string source = emit_generated_source(records);
    expect(source.find("VUGEN_") == std::string::npos, "no unexpanded markers remain in generated source");
    expect(source.find("RunStatus vu1_") != std::string::npos, "functions are emitted");
    expect(source.find("extern const ProgramEntry kPrograms[]") != std::string::npos, "program table is emitted with external linkage");

    std::size_t refused = 0, with_fallback = 0;
    for (const ProgramRecord &record : records) {
        if (record.refused) {
            ++refused;
            expect(record.function_name.empty(), "refused record has no function");
            expect(record.refusal.find("0x0008") != std::string::npos, "refusal names the delay-slot exit PC");
        }
        if (record.fallback_sites > 0) ++with_fallback;
    }
    expect(refused == 1, std::format("exactly one refused program, found {}", refused));
    expect(with_fallback == 2, std::format("programs with interpreter exits: expected 2 (refused delay-slot exit, unknown lower), found {}", with_fallback));

    const std::string manifest = manifest_json(records, "vu1_generated.cpp", sha256_hex(reinterpret_cast<const std::uint8_t *>(source.data()), source.size()));
    expect(manifest.find("\"status\": \"refused\"") != std::string::npos, "manifest records the refusal");
    expect(manifest.find("\"fallback_sites\": 1") != std::string::npos, "manifest counts interpreter exits");
    expect(manifest.find("\"instruction_budget\": 1000000") != std::string::npos, "manifest records the instruction budget");
}

}

int main() {
    check_sha256();
    check_names();
    check_image_sizes();
    check_collection_rejections();
    check_collision_refusal();
    check_output_directory_rules();
    check_emission();
    std::printf("vu_recomp core: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
