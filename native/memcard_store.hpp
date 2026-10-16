#pragma once

// Crash-safe storage for the emulated PS2 memory card image (TASK-025, Part A).
// The donor rewrote the card in place; this writer replaces the file only after
// a complete copy is on disk, so a crash leaves either the old or the new image.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace gt4::memcard {

// 8 MB PS2 card with ECC: 16384 pages of 528 bytes (512 data + 16 spare).
constexpr std::size_t kCardBytes = 528u * 0x4000u;

// Test hook: where a simulated crash interrupts write_card_atomically().
enum class CrashPoint {
  none,
  after_partial_write,  // half of the temporary file is written, then "power loss"
  before_rename,        // the temporary file is complete and flushed, but not renamed
};

// Writes `bytes` to `<path>.tmp`, flushes it to stable storage, then renames it
// over `path`. Returns false on I/O error or on a simulated crash; `path` is then
// unchanged (old image intact). A stale `<path>.tmp` may remain after a failure and
// is overwritten by the next call.
bool write_card_atomically(const std::filesystem::path &path,
                           const std::vector<std::uint8_t> &bytes,
                           CrashPoint crash = CrashPoint::none);

// Reads an existing card file. Returns false when it is missing or unreadable.
// A short file is returned as-is (the caller decides how to pad it).
bool read_card_file(const std::filesystem::path &path, std::vector<std::uint8_t> &bytes);

} // namespace gt4::memcard
