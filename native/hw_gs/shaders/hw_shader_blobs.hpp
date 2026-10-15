// SPDX-License-Identifier: MIT
//
// Embedded SPIR-V of the HWR shaders (TASK-027, M4). The arrays are generated at build time by
// native/hw_gs/shaders/CMakeLists.txt from the GLSL sources, compiled by the pinned glslang
// (implementation notes IN-201). Copyright (c) 2026 GT4Recomp contributors.

#pragma once

#include <cstddef>
#include <cstdint>

namespace hwgs::shaders {

// Vertex stage, smooth colour (IIP = 1) and flat colour (IIP = 0, -DHWR_FLAT_COLOUR=1).
extern const std::uint32_t hw_vertex_smooth_words[];
extern const std::size_t hw_vertex_smooth_word_count;
extern const std::uint32_t hw_vertex_flat_words[];
extern const std::size_t hw_vertex_flat_word_count;

// Uber pixel fragment stage, smooth and flat variants (must match the vertex stage).
extern const std::uint32_t hw_pixel_uber_smooth_words[];
extern const std::size_t hw_pixel_uber_smooth_word_count;
extern const std::uint32_t hw_pixel_uber_flat_words[];
extern const std::size_t hw_pixel_uber_flat_word_count;

// Exactness compute entry points (fixture only; never linked into the game).
extern const std::uint32_t hw_blend_exact_words[];
extern const std::size_t hw_blend_exact_word_count;
extern const std::uint32_t hw_pixel_exact_words[];
extern const std::size_t hw_pixel_exact_word_count;

}  // namespace hwgs::shaders
