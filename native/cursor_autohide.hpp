#pragma once

// TASK-060: mouse cursor auto-hide over the game picture. Pure: the Win32 calls (GetCursorPos, WindowFromPoint,
// SetCursor) live in window.cpp, so the rule can be checked without a window.
//
// Rule (the same in the game window and in the GS child window of gt4-gs.exe, which gets it as protocol 1.2 CONTROL
// cursor): the cursor is hidden after `hide_delay_ms` without movement while it is over the window's client area (the
// picture) and that window is the one under the cursor. Any movement shows it again at once. Outside the picture the
// cursor is never touched. In borderless fullscreen the client area is the whole monitor, so the same rule hides it
// anywhere on that monitor. GT4_CURSOR_AUTOHIDE=0 switches it off (default on).

#include <cstdint>

namespace gt4::cursor {

inline constexpr std::uint32_t kDefaultHideDelayMs = 3000;

struct Policy {
  bool auto_hide = true;
  std::uint32_t hide_delay_ms = kDefaultHideDelayMs;
};

// GT4_CURSOR_AUTOHIDE: unset or "1" = on, "0" = off. Any other value throws std::invalid_argument.
Policy parse_policy(const char *value);

class AutoHide {
public:
  explicit AutoHide(Policy policy = {}) : policy_(policy) {}

  // One cursor sample: screen position, a millisecond clock, and whether the cursor is over this window's picture
  // (client area, and this window is the one under the cursor). Returns true while the cursor must be hidden.
  bool update(std::int32_t x, std::int32_t y, std::uint64_t now_ms, bool over_picture);

  bool hidden() const { return hidden_; }
  const Policy &policy() const { return policy_; }

private:
  Policy policy_;
  bool has_sample_ = false;
  bool hidden_ = false;
  std::int32_t x_ = 0, y_ = 0;
  std::uint64_t last_move_ms_ = 0;
};

} // namespace gt4::cursor
