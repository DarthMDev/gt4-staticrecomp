// Window client size for the native host (slice 184). Pure: no Win32 calls, so the fixture can test it.
#pragma once
#include <cstdint>

namespace gt4::window_geometry {

struct Size {
    std::int32_t width;
    std::int32_t height;
};

struct Rect {
    std::int32_t left, top, right, bottom;
};

// The game frame is 448 lines. Unclamped sizes are the owner's table: 16:9 at 448 x scale lines,
// rounded as the owner wrote them (2x 1593x896, 3x 2389x1344, 4x 3185x1792).
constexpr Size kScaledClient[] = {{960, 540}, {1593, 896}, {2389, 1344}, {3185, 1792}};

// Client size of the window for scale and the frame added by AdjustWindowRectEx (frame_width/height).
// Scale 1 keeps the 960x540 client. Larger scales use the table when the outer window fits the work
// area; otherwise the largest 16:9 client that fits (height from the work area, width = floor(16 * h / 9)).
inline Size client_size(std::uint32_t scale, Rect work_area, std::int32_t frame_width, std::int32_t frame_height) {
    if (scale <= 1) return {960, 540};
    const std::int32_t work_width = work_area.right - work_area.left;
    const std::int32_t work_height = work_area.bottom - work_area.top;
    const std::uint32_t index = scale > 4 ? 3 : scale - 1;
    const Size want = kScaledClient[index];
    if (want.width + frame_width <= work_width && want.height + frame_height <= work_height) return want;
    std::int32_t height = work_height - frame_height;
    const std::int32_t height_for_width = (work_width - frame_width) * 9 / 16;
    if (height > height_for_width) height = height_for_width;
    if (height > want.height) height = want.height;
    if (height < 1) height = 1;
    return {height * 16 / 9, height};
}

// Top-left corner that centres an outer window of the given size in the work area.
inline Rect centred_rect(Rect work_area, std::int32_t outer_width, std::int32_t outer_height) {
    const std::int32_t left = work_area.left + (work_area.right - work_area.left - outer_width) / 2;
    const std::int32_t top = work_area.top + (work_area.bottom - work_area.top - outer_height) / 2;
    return {left, top, left + outer_width, top + outer_height};
}

} // namespace gt4::window_geometry
