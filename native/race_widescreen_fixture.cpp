// Fixture for TASK-079 GT4_FORCE_WIDESCREEN. Guest RAM is a synthetic 32 MB array with the owner pointer at 0x622F4C,
// the option object at owner + 0x38CB0, the four aspect presets at their pinned guest addresses and the published
// global word at 0x6184F0.

#include "race_widescreen.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {

using namespace gt4::display;

constexpr std::uint32_t ram_size = 32u << 20;
constexpr std::uint32_t owner = 0x00300000u;
constexpr std::uint32_t option = owner + 0x38CB0u;
constexpr std::uint32_t flag_address = option + 0x14u;
constexpr std::uint32_t record_address = option + 0x24u;
constexpr std::uint32_t video_address = option + 0x1388u;

void require(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

void store(std::vector<std::uint8_t> &ram, std::uint32_t address, std::uint32_t value) {
  for (int i = 0; i < 4; ++i)
    ram[address + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::uint32_t load(const std::vector<std::uint8_t> &ram, std::uint32_t address) {
  return static_cast<std::uint32_t>(ram[address]) | (static_cast<std::uint32_t>(ram[address + 1]) << 8) |
         (static_cast<std::uint32_t>(ram[address + 2]) << 16) |
         (static_cast<std::uint32_t>(ram[address + 3]) << 24);
}

// A 44-byte aspect record: 11 words; width at +0xC and height at +0x10 as f32.
void store_record(std::vector<std::uint8_t> &ram, std::uint32_t address, float width, float height, std::uint32_t tag) {
  for (std::uint32_t word = 0; word < 11; ++word)
    store(ram, address + 4 * word, tag + word);
  std::uint32_t bits = 0;
  std::memcpy(&bits, &width, 4);
  store(ram, address + 0xC, bits);
  std::memcpy(&bits, &height, 4);
  store(ram, address + 0x10, bits);
}

std::vector<std::uint8_t> make_ram(std::uint32_t owner_pointer, std::uint32_t flag, std::uint32_t video) {
  std::vector<std::uint8_t> ram(ram_size, 0);
  store(ram, widescreen_owner_pointer_address, owner_pointer);
  store(ram, option + 0x14u, flag);
  store(ram, video_address, video);
  // Presets as initialised by func_00439B30 from the pinned ELF: 4:3 is 275 x 205 (D_006A6188), wide is 425 x 240
  // (D_006A61B8); the NTSC presets hold the same two records.
  store_record(ram, widescreen_preset_4_3, 275.0f, 205.0f, 0x100u);
  store_record(ram, widescreen_preset_wide, 425.0f, 240.0f, 0x200u);
  store_record(ram, widescreen_preset_4_3_ntsc, 275.0f, 205.0f, 0x300u);
  store_record(ram, widescreen_preset_wide_ntsc, 425.0f, 240.0f, 0x400u);
  store_record(ram, record_address, 275.0f, 205.0f, 0x500u); // the game's 4:3 record
  store(ram, widescreen_global_address, 0);
  return ram;
}

void test_environment_parsing() {
  require(parse_force_widescreen(nullptr), "unset means on (launcher default)");
  require(!parse_force_widescreen("0"), "0 means off");
  require(parse_force_widescreen("1"), "1 means on");
  for (const char *bad : {"", "2", "16:9", "true", " 1", "01"}) {
    bool refused = false;
    try {
      parse_force_widescreen(bad);
    } catch (const std::invalid_argument &) {
      refused = true;
    }
    require(refused, "invalid values are refused");
  }
}

void test_address_chain() {
  require(widescreen_option_address(owner, ram_size) == option, "owner pointer chain to the option object");
  require(widescreen_option_address(0, ram_size) == 0, "null owner is not an option object");
  require(widescreen_option_address(0xFFFFFFF0u, ram_size) == 0, "wrapping owner is refused");
  const std::uint32_t last_owner = ram_size - (0x38CB0u + 0x1388u + 4u) + 1u;
  require(widescreen_option_address(last_owner, ram_size) == 0, "video word outside RAM is refused");
  require(widescreen_option_address(last_owner - 1u, ram_size) == last_owner - 1u + 0x38CB0u,
          "last valid owner is accepted");
  require(flag_address == owner + 0x38CC4u, "wide flag is at owner + 0x38CC4 (reader func_001AB4C0)");
}

void test_record_test() {
  std::vector<std::uint8_t> ram(ram_size, 0);
  store_record(ram, 0x1000, 853.0f, 480.0f, 0);
  require(record_is_widescreen(ram.data() + 0x1000), "853 x 480 is wide");
  store_record(ram, 0x1000, 640.0f, 448.0f, 0);
  require(!record_is_widescreen(ram.data() + 0x1000), "640 x 448 is 4:3");
  store_record(ram, 0x1000, 1.5f, 1.0f, 0);
  require(record_is_widescreen(ram.data() + 0x1000), "exactly 1.5 is wide (>=)");
  store_record(ram, 0x1000, 0.0f, 0.0f, 0);
  require(!record_is_widescreen(ram.data() + 0x1000), "0 / 0 (NaN) is not wide");
}

void test_preset_selection() {
  require(widescreen_preset_address(false, false) == widescreen_preset_4_3, "4:3 preset (func_00439108 sel 0)");
  require(widescreen_preset_address(true, false) == widescreen_preset_wide, "wide preset (func_00439108 sel 1)");
  require(widescreen_preset_address(false, true) == widescreen_preset_4_3_ntsc, "4:3 NTSC preset (sel 2)");
  require(widescreen_preset_address(true, true) == widescreen_preset_wide_ntsc, "wide NTSC preset (sel 3)");
}

void test_forced_write_ntsc() {
  WidescreenState state;
  auto ram = make_ram(owner, 0, 1);
  require(force_widescreen_at_vblank(ram.data(), ram_size, state), "4:3 option is forced at VBlank");
  require(load(ram, flag_address) == 1, "wide flag becomes 1");
  require(load(ram, widescreen_global_address) == 1, "published global becomes 1");
  require(std::memcmp(ram.data() + record_address, ram.data() + widescreen_preset_wide_ntsc, widescreen_record_bytes) == 0,
          "record copies the wide NTSC preset");
  require(state.writes == 1, "one write counted");
  require(!force_widescreen_at_vblank(ram.data(), ram_size, state), "a forced option is left alone");
  require(state.writes == 1, "no second write");
}

void test_forced_write_pal_source() {
  WidescreenState state;
  auto ram = make_ram(owner, 0, 0);
  require(force_widescreen_at_vblank(ram.data(), ram_size, state), "non-NTSC option is forced");
  require(std::memcmp(ram.data() + record_address, ram.data() + widescreen_preset_wide, widescreen_record_bytes) == 0,
          "non-NTSC record copies the wide preset");
}

void test_global_only() {
  WidescreenState state;
  auto ram = make_ram(owner, 1, 1);
  store_record(ram, record_address, 425.0f, 240.0f, 0x700u);
  require(force_widescreen_at_vblank(ram.data(), ram_size, state), "stale published global is corrected");
  require(load(ram, widescreen_global_address) == 1, "global becomes 1 with a wide record");
  require(load(ram, record_address) == 0x700u, "wide record is not overwritten");
}

void test_refusals() {
  WidescreenState state;
  auto null_owner = make_ram(0, 0, 1);
  require(!force_widescreen_at_vblank(null_owner.data(), ram_size, state), "null owner writes nothing");
  require(load(null_owner, flag_address) == 0, "flag untouched without owner");

  auto no_preset = make_ram(owner, 0, 1);
  store_record(no_preset, widescreen_preset_wide_ntsc, 275.0f, 205.0f, 0x900u);
  require(!force_widescreen_at_vblank(no_preset.data(), ram_size, state), "non-wide preset is refused");
  require(load(no_preset, flag_address) == 0, "refused write leaves the flag alone");
  require(load(no_preset, widescreen_global_address) == 0, "refused write leaves the global alone");
  require(state.writes == 0, "refusals are not counted as writes");
}

} // namespace

int main() {
  test_environment_parsing();
  test_address_chain();
  test_record_test();
  test_preset_selection();
  test_forced_write_ntsc();
  test_forced_write_pal_source();
  test_global_only();
  test_refusals();
  std::printf("native_race_widescreen: all checks passed\n");
  return 0;
}
