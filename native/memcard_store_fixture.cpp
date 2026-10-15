// Crash-safety fixture for the atomic memory card writer (TASK-025, Part A).
// Usage: memcard_store_fixture <scratch directory>

#include "memcard_store.hpp"

#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;
using gt4::memcard::CrashPoint;

namespace {

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

// Deterministic card-sized pattern; `seed` makes two images differ everywhere.
std::vector<std::uint8_t> pattern(std::uint32_t seed) {
  std::vector<std::uint8_t> image(gt4::memcard::kCardBytes);
  std::uint32_t state = seed * 2654435761u + 1u;
  for (auto &byte : image) {
    state = state * 1664525u + 1013904223u;
    byte = static_cast<std::uint8_t>(state >> 24);
  }
  return image;
}

std::vector<std::uint8_t> load(const fs::path &path) {
  std::vector<std::uint8_t> bytes;
  require(gt4::memcard::read_card_file(path, bytes), "card file must be readable");
  return bytes;
}

void check_fresh_write(const fs::path &dir) {
  const fs::path card = dir / "fresh.ps2";
  fs::remove(card);
  const auto image = pattern(1);
  require(gt4::memcard::write_card_atomically(card, image), "fresh write succeeds");
  require(load(card) == image, "fresh write reads back identical");
  require(!fs::exists(card.string() + ".tmp"), "temporary file is renamed away");
  std::fprintf(stderr, "[memcard] fresh write: PASS\n");
}

void check_crash_keeps_old_card(const fs::path &dir) {
  const fs::path card = dir / "crash.ps2";
  const auto old_image = pattern(2);
  const auto new_image = pattern(3);
  require(gt4::memcard::write_card_atomically(card, old_image), "seed the old card");

  require(!gt4::memcard::write_card_atomically(card, new_image, CrashPoint::after_partial_write),
          "a crash mid-write reports failure");
  require(load(card) == old_image, "crash mid-write leaves the old card byte-identical");
  require(fs::exists(card.string() + ".tmp"), "the incomplete temporary file is left behind");

  require(!gt4::memcard::write_card_atomically(card, new_image, CrashPoint::before_rename),
          "a crash before the rename reports failure");
  require(load(card) == old_image, "crash before rename leaves the old card byte-identical");

  require(gt4::memcard::write_card_atomically(card, new_image), "next save recovers");
  require(load(card) == new_image, "recovered save reads back the new image");
  require(!fs::exists(card.string() + ".tmp"), "recovery removes the temporary file");
  std::fprintf(stderr, "[memcard] crash mid-flush keeps the old card: PASS\n");
}

void check_crash_without_old_card(const fs::path &dir) {
  const fs::path card = dir / "first-crash.ps2";
  fs::remove(card);
  require(!gt4::memcard::write_card_atomically(card, pattern(4), CrashPoint::after_partial_write),
          "a crash on the first write reports failure");
  require(!fs::exists(card), "a crash on the first write creates no card");
  std::fprintf(stderr, "[memcard] crash on first write creates no card: PASS\n");
}

void check_unchanged_bytes_without_save(const fs::path &dir) {
  // A run that never saves must not touch the card: rewriting the loaded bytes is identical.
  const fs::path card = dir / "no-save.ps2";
  const auto image = pattern(5);
  require(gt4::memcard::write_card_atomically(card, image), "seed card");
  const auto before = load(card);
  require(gt4::memcard::write_card_atomically(card, before), "round-trip write");
  require(load(card) == before, "round-trip keeps card bytes identical");
  std::fprintf(stderr, "[memcard] unchanged card round-trip: PASS\n");
}

} // namespace

int main(int argc, char **argv) {
  try {
    const fs::path dir = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "gt4-memcard-fixture";
    fs::create_directories(dir);
    check_fresh_write(dir);
    check_crash_keeps_old_card(dir);
    check_crash_without_old_card(dir);
    check_unchanged_bytes_without_save(dir);
    std::fprintf(stderr, "PASS: atomic memory card flush\n");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
