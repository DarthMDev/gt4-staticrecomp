// Shared parsing for the CoreGS options (slice 184). Header-only and pure, so the GS adapter
// (gpu_init.inc, compiled into ps2runtime_vulkan) and the window host (window.cpp) use one parser.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace gt4::coregs {

// PS2_SCALE: internal resolution, 1 to kMaxScale (each GS pixel holds SCALE x SCALE sub-samples).
constexpr std::uint32_t kMaxScale = 4;

// Returns the scale for PS2_SCALE text (nullptr means unset, which is scale 1).
inline std::uint32_t parse_scale(const char *text) {
    if (!text) return 1;
    if (!text[0] || text[1] || text[0] < '1' || text[0] > '0' + static_cast<char>(kMaxScale))
        throw std::runtime_error(std::string("Native Vulkan supports PS2_SCALE 1 to ") + std::to_string(kMaxScale) + ", got: " + text);
    return static_cast<std::uint32_t>(text[0] - '0');
}

// GT4_FSR_SHARPNESS: a percentage, 0 (softest) to 100 (sharpest); default 50 (nullptr means unset).
constexpr float kDefaultFsrSharpnessPercent = 50.0f;

// RCAS stops: stops = 2 * (1 - percent / 100), so 100 is 0 stops (sharpest) and 0 is 2 stops (softest).
inline float fsr_sharpness_stops(float percent) { return 2.0f * (1.0f - percent / 100.0f); }

inline float parse_fsr_sharpness_stops(const char *text) {
    if (!text) return fsr_sharpness_stops(kDefaultFsrSharpnessPercent);
    char *end = nullptr;
    const float percent = std::strtof(text, &end);
    if (end == text || *end || !(percent >= 0.0f && percent <= 100.0f))
        throw std::runtime_error(std::string("GT4_FSR_SHARPNESS must be a percentage from 0 to 100, got: ") + text);
    return fsr_sharpness_stops(percent);
}

} // namespace gt4::coregs
