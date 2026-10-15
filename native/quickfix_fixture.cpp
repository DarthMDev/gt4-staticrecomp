#define _CRT_SECURE_NO_WARNINGS
// Pure fixture for the quick fixes: the fullscreen toggle state machine, the borderless style
// mask, and the GT4_FORCE_480P value parser (race_display). No window or device is touched.
#include "race_display.hpp"
#include "window_mode.hpp"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

using namespace gt4::window_mode;

void require(bool value, const std::string &label) {
  if (!value)
    throw std::runtime_error(label);
}

bool refused(const char *value) {
  try {
    gt4::display::parse_force_480p(value);
  } catch (const std::invalid_argument &) {
    return true;
  }
  return false;
}

// Slice 185: the window of the 2x and 4x high-resolution scanout (16:9, 448 * scale lines), clamped and centred.
void test_high_resolution_window() {
  require(high_resolution_client_size(2, {1920, 1080}).width == 1593 &&
              high_resolution_client_size(2, {1920, 1080}).height == 896,
          "2x window is 1593x896 on a 1920x1080 work area");
  require(high_resolution_client_size(4, {1920, 1080}).width == 1920 &&
              high_resolution_client_size(4, {1920, 1080}).height == 1080,
          "4x window is clamped to 1920x1080 on a 1920x1080 work area");
  require(high_resolution_client_size(4, {3840, 2160}).width == 3186 &&
              high_resolution_client_size(4, {3840, 2160}).height == 1792,
          "4x window is 3186x1792 when it fits");
  require(high_resolution_client_size(3, {3840, 2160}).width == 960 &&
              high_resolution_client_size(3, {3840, 2160}).height == 540,
          "scales other than 2 and 4 keep the 960x540 window");
  const Point origin = centred_origin({1000, 500}, {0, 0, 1920, 1080});
  require(origin.x == 460 && origin.y == 290, "a 1000x500 window is centred on 1920x1080");
}

void test_toggle_restores_windowed_placement() {
  ToggleState state;
  const Placement windowed{{100, 80, 1060, 620}, 0x16CF0000u};
  require(!state.fullscreen(), "starts windowed");
  Placement restored{};
  require(!state.leave(restored), "leave while windowed is refused");
  require(state.enter(windowed), "enter from windowed");
  require(state.fullscreen(), "fullscreen after enter");
  require(!state.enter({{0, 0, 1920, 1080}, 0x80000000u}),
          "second enter is refused and keeps the first placement");
  require(state.leave(restored), "leave from fullscreen");
  require(!state.fullscreen(), "windowed after leave");
  require(restored.bounds.left == 100 && restored.bounds.top == 80 &&
              restored.bounds.right == 1060 && restored.bounds.bottom == 620,
          "restored bounds equal the saved windowed bounds");
  require(restored.style == 0x16CF0000u, "restored style equals the saved style");
  require(state.enter({{5, 6, 7, 8}, 0x10CF0000u}), "toggle can enter again");
  require(state.leave(restored) && restored.bounds.left == 5 &&
              restored.style == 0x10CF0000u,
          "second cycle restores its own placement");
}

void test_fullscreen_style_mask() {
  const std::uint32_t visible = 0x10000000u;
  const std::uint32_t windowed = overlapped_window_style | visible;
  const std::uint32_t full = fullscreen_style(windowed);
  require((full & overlapped_window_style) == 0, "frame bits are removed");
  require((full & popup_style) != 0, "popup bit is added");
  require((full & visible) != 0, "visibility is kept");
  require(fullscreen_style(full) == full, "mask is idempotent");
  require(fullscreen_style(0) == popup_style, "empty style becomes popup");
}

void test_environment_values_are_strict() {
  require(!gt4::display::parse_force_480p(nullptr), "unset disables the switch");
  require(!gt4::display::parse_force_480p("0"), "0 disables the switch");
  require(gt4::display::parse_force_480p("1"), "1 enables the switch");
  require(refused(""), "empty value is refused");
  require(refused("true"), "true is refused");
  require(refused("01"), "01 is refused");
  require(refused("480p"), "named values are refused");
}

} // namespace

int main() {
  try {
    test_toggle_restores_windowed_placement();
    test_high_resolution_window();
    test_fullscreen_style_mask();
    test_environment_values_are_strict();
    std::puts("quickfix fixture PASS");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "quickfix fixture FAIL: %s\n", error.what());
    return 1;
  }
}
