#include "progressive_display.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace gt4::progressive {
namespace {

std::uint32_t read_le32(const std::uint8_t *bytes) {
  return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

void write_le32(std::uint8_t *bytes, std::uint32_t value) {
  for (int i = 0; i < 4; ++i)
    bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
}

struct Runtime {
  bool block_known = false;
  Block block{};
  std::uint32_t refills_logged = 0;
  std::uint64_t vblank = 0;
};

Runtime &runtime() {
  static Runtime state;
  return state;
}

}  // namespace

std::uint32_t word_index(std::uint32_t offset) { return (offset - (kProgressiveBlock - kBlockBase)) / 4u; }

bool parse_progressive(const char *value) {
  if (!value || std::strcmp(value, "1") == 0)
    return true;
  if (std::strcmp(value, "0") == 0)
    return false;
  throw std::invalid_argument("GT4_PROGRESSIVE must be 0 or 1");
}

bool parse_progressive_boot(const char *value) {
  if (!value || std::strcmp(value, "0") == 0)
    return false;
  if (std::strcmp(value, "1") == 0)
    return true;
  throw std::invalid_argument("GT4_PROGRESSIVE_BOOT must be 0 or 1");
}

Block block_for(bool progressive, bool boot) {
  Block block;
  block.words[word_index(kMenuPsmOffset)] = progressive ? kProgressiveMenuPsm : kOriginalMenuPsm;
  block.words[word_index(kMenuRowOffset)] = progressive ? kProgressiveMenuRow : kOriginalMenuRow;
  block.words[word_index(kFieldHalvingOffset)] = progressive ? kProgressiveFieldHalving : kOriginalFieldHalving;
  block.words[word_index(kDrawBackViewportOffset)] =
      progressive ? kProgressiveDrawBackViewport : kOriginalDrawBackViewport;
  block.words[word_index(kBootLayoutOffset)] = progressive && boot ? kProgressiveBootLayout : kOriginalBootLayout;
  block.words[word_index(kMonitorRowOffset)] = progressive ? kProgressiveMonitorRow : kOriginalMonitorRow;
  return block;
}

bool write_block(std::uint8_t *ram, std::uint32_t ram_size, const Block &block) {
  if (static_cast<std::uint64_t>(kProgressiveBlock) + 4u * kProgressiveWords > ram_size)
    return false;
  bool written = false;
  for (std::uint32_t i = 0; i < kProgressiveWords; ++i) {
    std::uint8_t *word = ram + kProgressiveBlock + 4u * i;
    if (read_le32(word) != block.words[i]) {
      write_le32(word, block.words[i]);
      written = true;
    }
  }
  return written;
}

bool library_has_table() {
#ifdef GT4_PROGRESSIVE_LIBRARY
  return true;
#else
  return false;
#endif
}

namespace {

struct Config {
  bool on = true;
  bool boot = false;
};

const Config &config() {
  static const Config value = [] {
    Config c;
    try {
      c.on = parse_progressive(std::getenv("GT4_PROGRESSIVE"));
      c.boot = parse_progressive_boot(std::getenv("GT4_PROGRESSIVE_BOOT"));
    } catch (const std::invalid_argument &error) {
      std::fprintf(stderr, "[progressive] %s, game not started\n", error.what());
      std::fflush(stderr);
      std::exit(1);
    }
    if (!library_has_table())
      std::fprintf(stderr, "[progressive] GT4_PROGRESSIVE=%d ignored: this build has no TASK-086 patch table "
                   "(EE library profile is not \"progressive\"); the game keeps its own video modes\n", c.on ? 1 : 0);
    else if (c.on)
      std::fprintf(stderr, "[progressive] GT4_PROGRESSIVE=1: menus after a scene and the normal race index use 480p "
                   "(psm 2 CT16, CRT row 6, 640x448) like the Screen Try pop-up; boot path %s "
                   "(GT4_PROGRESSIVE_BOOT, experimental)\n", c.boot ? "on" : "off");
    else
      std::fprintf(stderr, "[progressive] GT4_PROGRESSIVE=0: original video modes (480i menus)\n");
    return c;
  }();
  return value;
}

}  // namespace

bool configured_progressive() { return config().on; }

bool configured_progressive_boot() { return config().boot; }

void at_guest_vblank(std::uint8_t *ram, std::uint32_t ram_size) {
  const bool on = configured_progressive();
  if (!library_has_table())
    return;
  Runtime &r = runtime();
  ++r.vblank;
  const Block wanted = block_for(on, configured_progressive_boot());
  if (!write_block(ram, ram_size, wanted))
    return;
  // The boot clears low RAM before the game runs; a refill of the same words is logged a few times only.
  if (r.block_known && wanted == r.block && r.refills_logged >= 4)
    return;
  if (r.block_known && wanted == r.block)
    ++r.refills_logged;
  r.block_known = true;
  r.block = wanted;
  std::fprintf(stderr, "[progressive] display words 0x%08X at vblank %llu: menu psm %u, menu row %u, field halving "
               "0x%08X, draw-back viewport %u, boot layout %u, monitor row %u\n", kProgressiveBlock,
               static_cast<unsigned long long>(r.vblank), wanted.words[word_index(kMenuPsmOffset)],
               wanted.words[word_index(kMenuRowOffset)], wanted.words[word_index(kFieldHalvingOffset)],
               wanted.words[word_index(kDrawBackViewportOffset)], wanted.words[word_index(kBootLayoutOffset)],
               wanted.words[word_index(kMonitorRowOffset)]);
}

}  // namespace gt4::progressive
