#include "cursor_autohide.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace gt4::cursor {

Policy parse_policy(const char *value) {
  Policy policy;
  if (value == nullptr || std::strcmp(value, "1") == 0)
    return policy;
  if (std::strcmp(value, "0") == 0) {
    policy.auto_hide = false;
    return policy;
  }
  throw std::invalid_argument(std::string("GT4_CURSOR_AUTOHIDE must be unset, 0 or 1, got \"") + value + "\"");
}

bool AutoHide::update(std::int32_t x, std::int32_t y, std::uint64_t now_ms, bool over_picture) {
  const bool moved = !has_sample_ || x != x_ || y != y_;
  has_sample_ = true;
  x_ = x;
  y_ = y;
  if (moved)
    last_move_ms_ = now_ms;
  // Outside the picture (or switched off) the cursor belongs to whatever window is under it.
  if (!policy_.auto_hide || moved || !over_picture) {
    hidden_ = false;
    return false;
  }
  if (now_ms >= last_move_ms_ && now_ms - last_move_ms_ >= policy_.hide_delay_ms)
    hidden_ = true;
  return hidden_;
}

} // namespace gt4::cursor
