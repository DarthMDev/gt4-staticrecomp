#pragma once

// CoreGS 1.1 presentation options (slice 182, TASK-028). Shared by the GPU adapter and the post-pass tests.
#include <cstdint>

namespace ps2::gs::gpu {

enum class PostDeinterlace : std::uint32_t { off, weave, bob, blend, adaptive, automatic };
enum class PostFilter : std::uint32_t { automatic, nearest, linear, fsr };
struct PostSettings {
    PostDeinterlace deinterlace = PostDeinterlace::automatic;
    bool fxaa = false;
    PostFilter filter = PostFilter::automatic;
    float sharpness = 1.0f; // FSR RCAS in stops (GT4_FSR_SHARPNESS 50%): 0 is the sharpest, 2 the softest.
};

} // namespace ps2::gs::gpu
