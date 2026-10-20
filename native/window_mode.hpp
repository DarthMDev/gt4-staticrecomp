#pragma once

// Windowed and borderless fullscreen state for the native window. Pure: the
// Win32 calls (GetWindowRect, MonitorFromWindow, SetWindowPos) live in window.cpp,
// so the toggle rules can be checked without a window.

#include <cstdint>
#include <cstdlib>

namespace gt4::window_mode {

// Whether the game window starts in borderless fullscreen (GT4_FULLSCREEN, set by the launcher from its
// Display setting). Unset or empty = windowed; "0" = windowed; "1" = fullscreen. Any other value is refused
// with std::invalid_argument, so a typo never silently starts the wrong mode.
bool parse_start_fullscreen(const char *value);

// Same field order as RECT.
struct Rect {
  std::int32_t left, top, right, bottom;
};

// The windowed frame and the style that restores it.
struct Placement {
  Rect bounds;
  std::uint32_t style;
};

// WS_OVERLAPPEDWINDOW: caption, system menu, sizing frame and min/max boxes.
inline constexpr std::uint32_t overlapped_window_style = 0x00CF0000u;
// WS_POPUP: a frameless window that covers its monitor.
inline constexpr std::uint32_t popup_style = 0x80000000u;

// Style for borderless fullscreen: frame bits removed, popup added, all other bits kept.
std::uint32_t fullscreen_style(std::uint32_t windowed_style);

// Remembers the windowed placement while fullscreen is active.
class ToggleState {
public:
  bool fullscreen() const { return fullscreen_; }
  // Returns false (and changes nothing) when already fullscreen.
  bool enter(const Placement &windowed);
  // Returns false (and leaves `restored` untouched) when not fullscreen.
  bool leave(Placement &restored);

private:
  bool fullscreen_ = false;
  Placement saved_{};
};

} // namespace gt4::window_mode
