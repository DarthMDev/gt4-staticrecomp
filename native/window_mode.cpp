#include "window_mode.hpp"

namespace gt4::window_mode {

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
