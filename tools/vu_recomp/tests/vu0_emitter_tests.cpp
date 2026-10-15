// Emitter controls for the VU0 profile (slice 160). Synthetic 4 KiB images only; no game data.
//   - the profile fields of unit 0 and unit 1;
//   - XGKICK reachable on VU0 is refused, and VU1 emits the same bytes (unit 1 keeps XGKICK);
//   - reachable code at or beyond the 0xA80 upload is refused on VU0;
//   - VU0 records force the flag and status bits and use the full MAC policy;
//   - the VU0 identity differs from the VU1 identity over the same reachable words;
//   - a plain VU0 program emits with exact hand-offs (ctx.exact_run) and no interpreter call on the VU1 path.

#include "vurecomp.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace gt4::vurecomp;

int checks = 0;
int failures = 0;

void expect(bool condition, const std::string &message) {
    ++checks;
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
}

void put_pair(std::vector<std::uint8_t> &image, std::uint32_t pc, std::uint32_t lower, std::uint32_t upper) {
    for (int i = 0; i < 4; ++i) {
        image[pc + i] = static_cast<std::uint8_t>(lower >> (8 * i));
        image[pc + 4 + i] = static_cast<std::uint8_t>(upper >> (8 * i));
    }
}

constexpr std::uint32_t kEndUpper = 0x4000033Cu;  // VNOP with the E bit
constexpr std::uint32_t kNopUpper = 0x0000033Cu;  // VNOP without the E bit
constexpr std::uint32_t kXgkickLower = (0x40u << 25) | (27u << 6) | 0x3Cu;  // SPECIAL index 0x6C (XGKICK)

}  // namespace

int main() {
    const UnitProfile &vu0 = unit_profile(0);
    const UnitProfile &vu1 = unit_profile(1);
    expect(vu0.unit == 0 && vu0.image_bytes == 4096 && vu0.data_qwords == 256 && vu0.uploaded_bytes == 0xA80, "VU0 profile sizes");
    expect(!vu0.xgkick_allowed && vu0.exact_flags && vu0.file_prefix == "vu0_generated" && vu0.function_prefix == "vu0_", "VU0 profile policy and names");
    expect(vu0.identity_tag == "GT4-VU0-PROGRAM-IDENTITY-1" && vu0.code_mask() == 4095u, "VU0 identity tag and mask");
    expect(vu1.unit == 1 && vu1.image_bytes == 16384 && vu1.data_qwords == 1024 && vu1.xgkick_allowed && !vu1.exact_flags, "VU1 profile is unchanged");
    expect(vu1.identity_tag == "GT4-VU1-PROGRAM-IDENTITY-2" && vu1.file_prefix == "vu1_generated" && vu1.function_prefix == "vu1_", "VU1 tag and names");

    // XGKICK on VU0: refused. The same bytes on VU1 (16 KiB image, same entry) are emitted.
    std::vector<std::uint8_t> small(vu0.image_bytes, 0);
    put_pair(small, 0x000, kXgkickLower, kNopUpper);
    put_pair(small, 0x008, 0u, kEndUpper);
    std::vector<ProgramRecord> records;
    std::string error;
    expect(collect_explicit_program_records(small, {0x000}, records, error, vu0) && records.size() == 1, "explicit VU0 record collects: " + error);
    if (records.size() == 1) {
        ProgramRecord &record = records[0];
        expect(record.image_flags_used && record.image_status_used, "VU0 records force the flag and status bits on");
        expect(record.unit == 0, "VU0 record carries unit 0");
        const std::string text = emit_program_function(record);
        expect(record.refused && record.refusal.find("XGKICK") != std::string::npos, "XGKICK is refused on VU0: " + record.refusal);
        expect(text.empty(), "a refused VU0 program emits no text");
    }

    std::vector<std::uint8_t> large(vu1.image_bytes, 0);
    put_pair(large, 0x000, kXgkickLower, kNopUpper);
    put_pair(large, 0x008, 0u, kEndUpper);
    std::vector<ProgramRecord> vu1_records;
    expect(collect_explicit_program_records(large, {0x000}, vu1_records, error, vu1) && vu1_records.size() == 1, "explicit VU1 record collects");
    if (vu1_records.size() == 1) {
        const std::string text = emit_program_function(vu1_records[0]);
        expect(!vu1_records[0].refused && text.find("vu::kick(1,") != std::string::npos, "XGKICK still emits on VU1");
    }

    // Reachable code past the 0xA80 upload: the program starts at 0xA78 with no E bit, so 0xA80 is reachable.
    std::vector<std::uint8_t> beyond(vu0.image_bytes, 0);
    put_pair(beyond, 0xA78, 0u, kNopUpper);
    put_pair(beyond, 0xA80, 0u, kEndUpper);
    std::vector<ProgramRecord> beyond_records;
    expect(collect_explicit_program_records(beyond, {0xA78}, beyond_records, error, vu0) && beyond_records.size() == 1, "explicit record past the upload collects");
    if (beyond_records.size() == 1) {
        emit_program_function(beyond_records[0]);
        expect(beyond_records[0].refused && beyond_records[0].refusal.find("beyond") != std::string::npos,
               "reachable code beyond 0xA80 is refused on VU0: " + beyond_records[0].refusal);
    }

    // A plain VU0 program: E bit at 0x008. It emits, and its interpreter hand-offs use the exact path.
    std::vector<std::uint8_t> plain(vu0.image_bytes, 0);
    put_pair(plain, 0x000, 0u, kNopUpper);
    put_pair(plain, 0x008, 0u, kEndUpper);
    std::vector<ProgramRecord> plain_records;
    expect(collect_explicit_program_records(plain, {0x000}, plain_records, error, vu0) && plain_records.size() == 1, "plain VU0 record collects");
    if (plain_records.size() == 1) {
        const std::string text = emit_program_function(plain_records[0]);
        expect(!plain_records[0].refused && plain_records[0].mac_mode == "full", "plain VU0 program emits with the full MAC policy");
        expect(text.find("ps2::vu::interpret(") == std::string::npos, "VU0 emitted code never calls ps2::vu::interpret");
        expect(text.find("qword(mem,") == std::string::npos, "VU0 emitted code uses the 4 KiB data mask");
    }

    // Identity namespace: the same reachable words under the VU0 and VU1 tags give different digests.
    const std::vector<std::uint32_t> pcs = {0x000, 0x008};
    const std::string digest_vu0 = program_digest(small, pcs, 0x000, true, true, vu0.identity_tag);
    const std::string digest_vu1 = program_digest(small, pcs, 0x000, true, true, vu1.identity_tag);
    expect(digest_vu0 != digest_vu1, "VU0 and VU1 identities are separate namespaces");
    expect(digest_vu0 == program_digest(small, pcs, 0x000, true, true, vu0.identity_tag), "identity is deterministic");

    std::printf("vu0 emitter controls: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
