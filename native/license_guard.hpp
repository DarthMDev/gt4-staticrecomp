#pragma once

// TASK-017 license demo guard (slice205). Game-specific, narrow mitigation, not an FPU fix.
//
// GT4 decrypts each replay/license demo in place with an FPU-keyed stream cipher (func_00579E00 via
// func_0044D278) inside the license demo loader at 0x001100D0. The loader ignores the game's own CRC
// result and calls this->vtbl[154] = RaceSolitaire::loadDemoInputs (0x003EAD18) with arg1 = the
// decrypted body; that calls AutomobileControlRecord::Manager::load(this + 0xE560, arg1 + 0x1AA4, 1),
// which trusts the record count n at body + 0x1AA4. Our mul.s/div.s are not bit-exact with the
// R5900, so about half the US demos decrypt in the wrong key-stream phase, n reads 0x7F7F7F7F and the
// record copy sweeps 64 KiB slots through RAM (crash). Confirmed: docs/journal/2026-10-16.md.
//
// The guard runs at the indirect call from the loader into loadDemoInputs. When the decrypted body
// fails the game's own CRC and is not the single-Recorder layout, it writes n = 0xFFFFFFFF (negative):
// Manager::load then takes its own existing path for n < 0 (state 1, the record loop runs zero times),
// so the demo has no inputs and the license itself stays playable. This mimics the owner's PCSX2
// observation (the demo starts loading, then returns to the license menu). Files that decrypt to a
// sane layout, and every file whose CRC passes, are never touched.

#include <cstddef>
#include <cstdint>

namespace gt4::license_guard {

// Guest addresses (Confirmed from the translated units and the gt4-decomp symbols).
inline constexpr std::uint32_t kLoadDemoInputs = 0x003EAD18u;  // RaceSolitaire::loadDemoInputs (vtbl[154])
inline constexpr std::uint32_t kLoaderReturn = 0x001101E4u;    // return label after the vtbl[154] call in 0x001100D0
// Demo file layout: header {u32 body offset, u32 body size, u32 unused, u32 key}, body at the offset.
inline constexpr std::uint32_t kHeaderBytes = 16u;
inline constexpr std::uint32_t kExpectedBodyOffset = 0x40u;
inline constexpr std::uint32_t kMaxBodySize = 0x00100000u;    // license demos are 8-36 KiB; refuse anything absurd
inline constexpr std::uint32_t kManagerOffset = 0x1AA4u;       // record count n (Manager stream) in the body
inline constexpr std::uint32_t kSkipCount = 0xFFFFFFFFu;       // n < 0: Manager::load copies no records

struct Verdict {
    bool crc_ok = false;      // game rule: (crc32(body) & 0xFFFFFF) | body[0] << 24 == key
    bool layout_ok = false;   // n = 0 and one Recorder {frames > 0, size} that ends at the body end
    std::uint32_t count = 0;  // u32 at body + kManagerOffset (0 when the body is too short)
    bool guard = false;       // !crc_ok && !layout_ok
};

// Standard reflected CRC-32 (polynomial 0xEDB88320), the table at 0x00655358.
std::uint32_t crc32(const std::uint8_t *data, std::size_t size) noexcept;

// The game's own check of a decrypted body against the key word of the demo header.
bool game_crc_ok(const std::uint8_t *body, std::size_t size, std::uint32_t key) noexcept;

// Same rule as scripts/license_demo_oracle.py layout_sane().
bool single_recorder_layout(const std::uint8_t *body, std::size_t size) noexcept;

Verdict evaluate(const std::uint8_t *body, std::size_t size, std::uint32_t key) noexcept;

// Number of loads guarded in this process (for the log line).
std::uint32_t guarded_count() noexcept;
std::uint32_t note_guarded() noexcept;

}  // namespace gt4::license_guard
