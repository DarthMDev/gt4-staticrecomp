// MIT License. Copyright (c) 2026 João Alano and GT4Recomp contributors.
// Written from the TASK-026 specification (docs/hw-renderer-spec), clean room per decision 0069.
// Implements chapter 13.6 (fixed 16:9 presentation rectangle). Pure arithmetic, no Vulkan.
#pragma once

#include <algorithm>
#include <cstdint>

namespace gt4::hw {

struct PresentRect {
  std::uint32_t left, top, width, height;
};

// Largest 16:9 rectangle centred in width x height. Zero extents give an empty rectangle;
// the arithmetic uses 64-bit intermediates so UINT32_MAX inputs cannot overflow.
inline PresentRect present_rect_widescreen(std::uint32_t width, std::uint32_t height) {
  if (!width || !height) return {0, 0, 0, 0};
  const std::uint64_t numerator = 16;
  const std::uint64_t denominator = 9;
  const auto fitted_width = static_cast<std::uint32_t>(std::max<std::uint64_t>(
      1, std::min<std::uint64_t>(width, height * numerator / denominator)));
  const auto fitted_height = static_cast<std::uint32_t>(std::max<std::uint64_t>(
      1, std::min<std::uint64_t>(height, width * denominator / numerator)));
  return {(width - fitted_width) / 2, (height - fitted_height) / 2, fitted_width, fitted_height};
}

} // namespace gt4::hw
