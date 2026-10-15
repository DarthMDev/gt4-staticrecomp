// SPDX-License-Identifier: MIT
//
// CPU reference for the CoreGS 1.1 present-path post passes (slice 182, TASK-028).
// Written from docs/hw-renderer-spec chapter 13.4 and the MIT CPU reference of task/027
// (native/hw_gs/display/hw_gs_display.cpp, weave/blend/bob/adaptive). The GPU shader
// native/post/present.wgsl must match this reference; native/post_fixture.cpp checks it.

#pragma once

#include "post/post_settings.hpp"

#include <cstdint>
#include <vector>

namespace ps2::gs::post_reference {

using gpu::PostDeinterlace;

// Stateful deinterlacer on RGBA8 frames packed as u32 (red in the low byte).
class Deinterlacer {
public:
    // `field` is the row parity of the current field (0 or 1); `scale` is the donor internal scale (bob shift).
    // `mode` must be off, weave, bob, blend or adaptive (automatic is resolved by the caller).
    std::vector<std::uint32_t> process(const std::vector<std::uint32_t> &frame, std::uint32_t width, std::uint32_t height,
                                       PostDeinterlace mode, std::uint32_t field, std::uint32_t scale, float threshold);
    void reset();

private:
    std::vector<std::uint32_t> woven(const std::vector<std::uint32_t> &frame, std::uint32_t width, std::uint32_t height,
                                     std::uint32_t field);
    std::uint32_t width_ = 0, height_ = 0;
    bool has_output_ = false, has_even_ = false, has_odd_ = false;
    std::vector<std::uint32_t> output_, even_, odd_;
};

// FSR 1.0 (EASU then RCAS) from input_width x input_height to output_width x output_height, as the shader computes it.
std::vector<std::uint32_t> fsr_reference(const std::vector<std::uint32_t> &frame, std::uint32_t input_width,
                                         std::uint32_t input_height, std::uint32_t output_width, std::uint32_t output_height,
                                         float sharpness);

// FXAA (edge-only) on one frame, as the shader computes it. Stateless.
std::vector<std::uint32_t> fxaa(const std::vector<std::uint32_t> &frame, std::uint32_t width, std::uint32_t height);

} // namespace ps2::gs::post_reference
