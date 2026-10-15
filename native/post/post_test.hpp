#pragma once

// Test entry points for the CoreGS 1.1 post passes (slice 182). Used only by native/post_fixture.cpp.
#include "post/post_settings.hpp"

#include <cstdint>
#include <vector>

namespace ps2::gs::gpu {

// Runs one frame (width x height RGBA8 packed pixels) through the post passes selected by `settings`.
// `interlaced` is the donor's really-interlaced flag and `field` the field parity. History persists between calls.
bool post_frame_for_test(const std::vector<std::uint32_t> &pixels, std::uint32_t width, std::uint32_t height,
                         bool interlaced, std::uint32_t field, const PostSettings &settings,
                         std::vector<std::uint32_t> &result);
void post_reset_for_test();

} // namespace ps2::gs::gpu
