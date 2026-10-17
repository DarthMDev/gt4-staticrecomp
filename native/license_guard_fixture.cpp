// CTest native_license_guard (TASK-017, slice205): the license demo guard rule on synthetic bodies.
// Sane single-Recorder data passes unchanged, an insane count with a failing CRC is guarded, and a body
// whose CRC passes is never guarded, whatever its layout.

#include "license_guard.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char *what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

void put32(std::vector<std::uint8_t> &body, std::uint32_t offset, std::uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) body[offset + byte] = static_cast<std::uint8_t>(value >> (8 * byte));
}

// A body shaped like replay/license/usl0b0001 in the right phase: n = 0, frames 0x5BF, size 0x10E, ends at 0x1BB8.
std::vector<std::uint8_t> sane_body() {
    std::vector<std::uint8_t> body(0x1BB8u, 0u);
    for (std::size_t index = 0; index < 0x100u; ++index) body[index] = static_cast<std::uint8_t>(index * 7u + 3u);
    put32(body, gt4::license_guard::kManagerOffset, 0u);
    put32(body, gt4::license_guard::kManagerOffset + 4u, 0x5BFu);
    put32(body, gt4::license_guard::kManagerOffset + 8u, 0x10Eu);
    return body;
}

std::uint32_t key_for(const std::vector<std::uint8_t> &body) {
    return (gt4::license_guard::crc32(body.data(), body.size()) & 0x00FFFFFFu) |
           (static_cast<std::uint32_t>(body[0]) << 24);
}

}  // namespace

int main() {
    using namespace gt4::license_guard;
    const char check_text[] = "123456789";
    check(crc32(reinterpret_cast<const std::uint8_t *>(check_text), 9) == 0xCBF43926u, "CRC-32 check value");

    // Sane layout, CRC fails (wrong header bytes, as for B3/B4 in our build): unchanged.
    std::vector<std::uint8_t> sane = sane_body();
    const std::uint32_t wrong_key = key_for(sane) ^ 0x00010000u;
    Verdict verdict = evaluate(sane.data(), sane.size(), wrong_key);
    check(!verdict.crc_ok && verdict.layout_ok && !verdict.guard && verdict.count == 0u, "sane layout, CRC fail: kept");

    // Sane layout, CRC passes: unchanged.
    verdict = evaluate(sane.data(), sane.size(), key_for(sane));
    check(verdict.crc_ok && verdict.layout_ok && !verdict.guard, "sane layout, CRC pass: kept");

    // Wrong key-stream phase: n = 0x7F7F7F7F, CRC fails: guarded.
    std::vector<std::uint8_t> insane = sane_body();
    put32(insane, kManagerOffset, 0x7F7F7F7Fu);
    put32(insane, kManagerOffset + 4u, 0x7F7F7F7Fu);
    verdict = evaluate(insane.data(), insane.size(), key_for(sane));
    check(!verdict.crc_ok && !verdict.layout_ok && verdict.guard && verdict.count == 0x7F7F7F7Fu,
          "insane count, CRC fail: guarded");

    // The same insane body with a key that matches its CRC: never guarded.
    verdict = evaluate(insane.data(), insane.size(), key_for(insane));
    check(verdict.crc_ok && !verdict.layout_ok && !verdict.guard, "insane count, CRC pass: kept");

    // n = 0 but the Recorder size does not end at the body end, CRC fails: guarded (the copy would overrun too).
    std::vector<std::uint8_t> long_recorder = sane_body();
    put32(long_recorder, kManagerOffset + 8u, 0x7F7F0000u);
    verdict = evaluate(long_recorder.data(), long_recorder.size(), wrong_key);
    check(!verdict.layout_ok && verdict.guard && verdict.count == 0u, "n = 0, bad size, CRC fail: guarded");

    // Zero frames is not the expected layout.
    std::vector<std::uint8_t> no_frames = sane_body();
    put32(no_frames, kManagerOffset + 4u, 0u);
    check(!single_recorder_layout(no_frames.data(), no_frames.size()), "zero frames: not the layout");

    // Too short for the Manager stream: guarded when the CRC fails, count reads 0.
    std::vector<std::uint8_t> shorter(sane.begin(), sane.begin() + kManagerOffset + 8u);
    verdict = evaluate(shorter.data(), shorter.size(), wrong_key);
    check(!verdict.layout_ok && verdict.guard && verdict.count == 0u, "short body, CRC fail: guarded");

    // Empty body never passes the CRC.
    check(!game_crc_ok(sane.data(), 0, 0u), "empty body: CRC fail");

    check(guarded_count() == 0u, "counter starts at 0");
    check(note_guarded() == 1u && note_guarded() == 2u && guarded_count() == 2u, "counter increments");

    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("license_guard_fixture: all checks passed\n");
    return 0;
}
