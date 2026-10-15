#include "window_mode.hpp"

namespace gt4::window_mode {

std::uint32_t fullscreen_style(std::uint32_t windowed_style) {
  return (windowed_style & ~overlapped_window_style) | popup_style;
}

Size high_resolution_client_size(std::uint32_t scale, Size work) {
  if (scale != 2 && scale != 4)
    return {960, 540};
  std::int32_t height = static_cast<std::int32_t>(448 * scale);
  std::int32_t width = (height * 16 + 4) / 9;
  if (height > work.height) {
    height = work.height;
    width = height * 16 / 9;
  }
  if (width > work.width) {
    width = work.width;
    height = width * 9 / 16;
  }
  return {width, height};
}

Point centred_origin(Size outer, Rect work) {
  return {work.left + (work.right - work.left - outer.width) / 2,
          work.top + (work.bottom - work.top - outer.height) / 2};
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
