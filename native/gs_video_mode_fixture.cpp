// Pure fixture for the SetGsCrt to SMODE1/SMODE2 mapping (TASK-012, slice 162 step 4).
// No Vulkan device is needed. Each accepted row is decoded back into its CMOD/LC/INT/FFMD
// fields and compared with the paraLLEl-GS encoding that the owner logs require.

#include "gs_video_mode.hpp"

#include <cstdint>
#include <cstdio>

namespace {

using gt4::gs::CrtMapping;
using gt4::gs::CrtMappingStatus;
using gt4::gs::CrtRequest;

int failures = 0;

void expect(bool condition, const char *what) {
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition)
        ++failures;
}

std::uint64_t field(std::uint64_t value, unsigned low, unsigned bits) {
    return (value >> low) & ((std::uint64_t(1) << bits) - 1);
}

// Expected SMODE1 for a row, written as the bit fields paraLLEl-GS decodes. Every other bit must be zero.
std::uint64_t expected_smode1(std::uint64_t cmod, std::uint64_t lc) { return (cmod << 13) | (lc << 3); }

void expect_mapping(const CrtRequest &request, std::uint64_t cmod, std::uint64_t lc, std::uint64_t smode2,
                    const char *what) {
    const CrtMapping mapping = gt4::gs::map_set_gs_crt(request);
    const bool exact = mapping.status == CrtMappingStatus::ok &&
                       mapping.registers.smode1 == expected_smode1(cmod, lc) &&
                       mapping.registers.smode2 == smode2;
    expect(exact, what);
    if (mapping.status == CrtMappingStatus::ok) {
        // Decode the fields as paraLLEl-GS would and check nothing else is set.
        const std::uint64_t smode1 = mapping.registers.smode1;
        expect(field(smode1, 13, 2) == cmod && field(smode1, 3, 7) == lc &&
                   (smode1 & ~((std::uint64_t(3) << 13) | (std::uint64_t(0x7F) << 3))) == 0,
               "    SMODE1 decodes to CMOD and LC only, all other bits zero");
    }
}

void expect_refused(const CrtRequest &request, CrtMappingStatus status, const char *what) {
    const CrtMapping mapping = gt4::gs::map_set_gs_crt(request);
    expect(mapping.status == status && mapping.registers.smode1 == 0 && mapping.registers.smode2 == 0, what);
}

} // namespace

int main() {
    // Owner-log rows (confirmed by the SetGsCrt lines in private/manual-play/session-*/probe.log).
    // NTSC 480i: CMOD=NTSC(2) LC_ANALOG(32); INT=1, field 1 means frame mode (FFMD=1). GT4 writes the
    // same SMODE2 = 0x3 itself from vblank 5 (slice 167 register log).
    expect_mapping({1, 0x02, 1}, 2, 32, 0x3, "owner log 'interlace 1 mode 0x2 field 1' -> NTSC interlaced");
    // 480p after the GT4_FORCE_480P rewrite: CMOD progressive, LC_ANALOG, INT=0.
    expect_mapping({0, 0x50, 1}, 0, 32, 0x0, "owner log 'interlace 0 mode 0x50 field 1' -> 480p");
    // 1080i: CMOD progressive, LC_HDTV(22), INT=1.
    expect_mapping({1, 0x51, 1}, 0, 22, 0x3, "owner log 'interlace 1 mode 0x51 field 1' -> 1080i");

    // Other rows of the table [Hyp]. Field mode (field 0) leaves FFMD clear.
    expect_mapping({1, 0x02, 0}, 2, 32, 0x1, "NTSC interlaced with field 0 -> field mode FFMD=0");
    expect_mapping({0, 0x03, 0}, 3, 32, 0x0, "PAL progressive setting");
    expect_mapping({1, 0x03, 1}, 3, 32, 0x3, "PAL interlaced, field 1 (frame mode)");
    expect_mapping({0, 0x52, 1}, 0, 22, 0x0, "720p -> CMOD progressive, LC_HDTV, INT=0");
    expect_mapping({0, 0x1A, 1}, 0, 15, 0x0, "VGA range start -> LC_VGA");
    expect_mapping({0, 0x3B, 1}, 0, 15, 0x0, "VGA range end -> LC_VGA");

    // Refusals: never a silent guess.
    expect_refused({0, 0x99, 1}, CrtMappingStatus::unknown_mode, "unknown mode 0x99 is refused");
    expect_refused({0, 0x00, 1}, CrtMappingStatus::unknown_mode, "mode 0x00 (no output) is refused");
    expect_refused({1, 0x50, 1}, CrtMappingStatus::unsupported_combination, "480p cannot be interlaced");
    expect_refused({0, 0x51, 1}, CrtMappingStatus::unsupported_combination, "1080i must be interlaced");
    expect_refused({1, 0x52, 1}, CrtMappingStatus::unsupported_combination, "720p cannot be interlaced");
    expect_refused({1, 0x1A, 1}, CrtMappingStatus::unsupported_combination, "VGA must be progressive");
    expect_refused({2, 0x02, 1}, CrtMappingStatus::unsupported_combination, "interlace value 2 is refused");
    expect_refused({1, 0x02, 2}, CrtMappingStatus::invalid_field, "field value 2 is refused");

    // Every refusal has a readable description for the log.
    expect(gt4::gs::describe(CrtMappingStatus::unknown_mode)[0] != '\0', "refusals carry a log message");

    std::printf("failures: %d\n", failures);
    return failures ? 1 : 0;
}
