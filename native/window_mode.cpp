#include "window_mode.hpp"

#include <cstring>
#include <stdexcept>

namespace gt4::window_mode {

bool parse_start_fullscreen(const char *value) {
  if (value == nullptr || std::strlen(value) == 0 || std::strcmp(value, "0") == 0)
    return false;
  if (std::strcmp(value, "1") == 0)
    return true;
  throw std::invalid_argument("GT4_FULLSCREEN must be 0 or 1");
}

std::uint32_t fullscreen_style(std::uint32_t windowed_style) {
  return (windowed_style & ~overlapped_window_style) | popup_style;
}

bool ToggleState::enter(const Placement &windowed) {
  if (fullscreen_)
    return false;
  saved_ = windowed;
  fullscreen_ = true;
  return true;
}

bool ToggleState::leave(Placement &restored) {
  if (!fullscreen_)
    return false;
  restored = saved_;
  fullscreen_ = false;
  return true;
}

} // namespace gt4::window_mode
