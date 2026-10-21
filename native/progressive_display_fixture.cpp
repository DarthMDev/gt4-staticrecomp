// Fixture for TASK-086 step 1 (native/progressive_display.hpp): GT4_PROGRESSIVE parsing, the display words for the
// switch on and off, and their fill into a synthetic 32 MB guest RAM. The words must reproduce the original
// instructions when off (scripts/aspect_elf_patch.py PROGRESSIVE_PATCHES and BOOT_REWRITES;
// tests/python/test_progressive_display.py checks the same numbers against the pinned ELF).

#include "display_aspect.hpp"
#include "progressive_display.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {

using namespace gt4::progressive;

constexpr std::uint32_t ram_size = 32u << 20;

void require(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

std::uint32_t load(const std::vector<std::uint8_t> &ram, std::uint32_t address) {
  return static_cast<std::uint32_t>(ram[address]) | (static_cast<std::uint32_t>(ram[address + 1]) << 8) |
         (static_cast<std::uint32_t>(ram[address + 2]) << 16) | (static_cast<std::uint32_t>(ram[address + 3]) << 24);
}

bool refused(const char *value) {
  try {
    parse_progressive(value);
  } catch (const std::invalid_argument &) {
    return true;
  }
  return false;
}

void test_environment_parsing() {
  require(parse_progressive(nullptr), "unset is on (progressive is the default)");
  require(parse_progressive("1"), "1 is on");
  require(!parse_progressive("0"), "0 is off");
  for (const char *bad : {"", "2", "on", "off", "true", " 1", "1 ", "01"})
    require(refused(bad), "anything but 0 and 1 is refused");
  // GT4_PROGRESSIVE_BOOT (experimental): off unless "1".
  require(!parse_progressive_boot(nullptr), "the boot path is off when unset");
  require(!parse_progressive_boot("0") && parse_progressive_boot("1"), "0 and 1");
  for (const char *bad : {"", "2", "on", " 1"}) {
    bool threw = false;
    try {
      parse_progressive_boot(bad);
    } catch (const std::invalid_argument &) {
      threw = true;
    }
    require(threw, "anything but 0 and 1 is refused (boot)");
  }
}

void test_layout() {
  using gt4::display_aspect::kHudParameterBlock;
  using gt4::display_aspect::kHudParameterWords;
  require(kBlockBase == kHudParameterBlock, "offsets count from the TASK-080 block base");
  require(kProgressiveBlock == kHudParameterBlock + 4u * kHudParameterWords,
          "the progressive words follow the HUD parameter block without overlap");
  require(kProgressiveBlock + 4u * kProgressiveWords <= 0x8000u, "reachable as a positive offset(zero)");
  require(kMenuPsmOffset == kProgressiveBlock - kBlockBase, "menu psm is the first word");
  require(kMenuRowOffset == kMenuPsmOffset + 4u && kFieldHalvingOffset == kMenuRowOffset + 4u &&
              kDrawBackViewportOffset == kFieldHalvingOffset + 4u && kBootLayoutOffset == kDrawBackViewportOffset + 4u &&
              kMonitorRowOffset == kBootLayoutOffset + 4u,
          "consecutive words");
  require(word_index(kMenuPsmOffset) == 0 && word_index(kMonitorRowOffset) == kProgressiveWords - 1, "word indexes");
}

void test_values() {
  const Block off = block_for(false);
  const Block on = block_for(true);
  // Off: the constants of the original instructions (addiu a1,zero,1; daddu a2,zero,zero; lui at,0x3F00;
  // addiu a0,zero,1) and boot layout 0, which is also the value of a word the host has not filled yet.
  require(off.words[0] == 1u && off.words[1] == 0u && off.words[2] == 0x3F000000u && off.words[3] == 1u &&
              off.words[4] == 0u && off.words[5] == 1u,
          "off reproduces the original constants");
  // On: exactly the tuple of the Screen page's 480p Try pop-up (func_00436640 index 1: psm 2, row 6), the full-frame
  // UI passes; the boot keeps its own layout unless the experimental boot path is on too.
  require(on.words[0] == 2u && on.words[1] == 6u, "on asks for the Try pop-up's mode (CT16, CRT row 6)");
  require(on.words[2] == 0x3F800000u && on.words[3] == 0u, "on: no field halving, full-frame draw-back");
  require(on.words[4] == 0u, "on: the boot keeps its field layout by default");
  require(on.words[5] == 6u, "on: the race monitor's field request asks for row 6");
  require(!(on == off), "the switch changes the words");
  require(block_for(true, true).words[4] == 1u, "progressive and boot: 480p boot layout");
  require(block_for(false, true) == off, "the boot switch alone changes nothing");
}

void test_fill() {
  std::vector<std::uint8_t> ram(ram_size, 0);
  const Block on = block_for(true, true);
  require(write_block(ram.data(), ram_size, on), "the first fill writes");
  require(load(ram, 0x7F08u) == 2u && load(ram, 0x7F0Cu) == 6u && load(ram, 0x7F10u) == 0x3F800000u &&
              load(ram, 0x7F14u) == 0u && load(ram, 0x7F18u) == 1u && load(ram, 0x7F1Cu) == 6u,
          "words at 0x7F08..0x7F1F");
  require(!write_block(ram.data(), ram_size, on), "unchanged words: no write");
  for (std::uint32_t at = 0; at < 0x10000u; ++at)
    if (at < kProgressiveBlock || at >= kProgressiveBlock + 4u * kProgressiveWords)
      require(ram[at] == 0, "nothing outside the words is written");
  ram[0x7F0Cu] = 0;  // the boot clears low RAM: the next VBlank refills
  require(write_block(ram.data(), ram_size, on), "a cleared word is refilled");
  const Block off = block_for(false);
  require(write_block(ram.data(), ram_size, off), "switching off rewrites");
  require(load(ram, 0x7F08u) == 1u && load(ram, 0x7F0Cu) == 0u && load(ram, 0x7F18u) == 0u, "off words");
  std::vector<std::uint8_t> tiny(0x7F10u, 0);
  require(!write_block(tiny.data(), static_cast<std::uint32_t>(tiny.size()), on), "out-of-range block: no write");
}

}  // namespace

int main() {
  test_environment_parsing();
  test_layout();
  test_values();
  test_fill();
  std::printf("progressive_display fixture OK (library %s the TASK-086 table)\n",
              library_has_table() ? "has" : "does not have");
  return 0;
}
