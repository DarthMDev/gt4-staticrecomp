// Window client size control (slice184). Pure: checks window_geometry.hpp and coregs_options.hpp without Win32.
// Scales 1 to 4 on a full 1920x1080 work area, a small work area that forces the clamp, and a work area
// offset from the origin (centring). Exit 0 when every case holds.
#include "coregs_options.hpp"
#include "window_geometry.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

using gt4::window_geometry::Rect;
using gt4::window_geometry::Size;

namespace {

void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

void expect_size(std::uint32_t scale, Rect area, std::int32_t frame_w, std::int32_t frame_h, Size want,
                 const std::string &label) {
    const Size got = gt4::window_geometry::client_size(scale, area, frame_w, frame_h);
    require(got.width == want.width && got.height == want.height,
            label + " expected " + std::to_string(want.width) + "x" + std::to_string(want.height) + ", got " +
                std::to_string(got.width) + "x" + std::to_string(got.height));
}

} // namespace

int main() {
    try {
        const Rect full{0, 0, 1920, 1080};
        const std::int32_t frame_w = 16, frame_h = 39; // typical WS_OVERLAPPEDWINDOW frame
        // Scale 1 keeps the original 960x540 client and CW_USEDEFAULT placement.
        expect_size(1, full, frame_w, frame_h, {960, 540}, "scale 1");
        expect_size(0, full, frame_w, frame_h, {960, 540}, "scale unset");
        // Scales 2 to 4: 16:9 at 448 x scale lines (1593x896, 2389x1344, 3185x1792) when they fit.
        const Rect big{0, 0, 3840, 2160};
        expect_size(2, big, frame_w, frame_h, {1593, 896}, "scale 2");
        expect_size(3, big, frame_w, frame_h, {2389, 1344}, "scale 3");
        expect_size(4, big, frame_w, frame_h, {3185, 1792}, "scale 4 on a 4K work area");
        // A 1920x1080 work area clamps scales 3 and 4 to the largest 16:9 client that fits the frame.
        // Scale 3 wants 1344 lines; 1080 - 39 = 1041 lines fit, so 1041 lines and 1850 columns (1041*16/9).
        expect_size(3, full, frame_w, frame_h, {1850, 1041}, "scale 3 clamped by height");
        // Scale 4 is also clamped by height: 1041 lines, same client.
        expect_size(4, full, frame_w, frame_h, {1850, 1041}, "scale 4 clamped by height");
        // A 1000x700 work area forces the width clamp: (1000 - 16) * 9 / 16 = 553 lines, 553 * 16 / 9 = 983 columns.
        const Rect small{0, 0, 1000, 700};
        expect_size(3, small, frame_w, frame_h, {983, 553}, "scale 3 clamped by width");
        // The clamped outer window stays inside the work area and keeps 16:9 for every case above.
        for (std::uint32_t scale = 1; scale <= 4; ++scale) {
            for (const Rect area : {full, small, big}) {
                const Size client = gt4::window_geometry::client_size(scale, area, frame_w, frame_h);
                const std::int32_t outer_w = client.width + frame_w, outer_h = client.height + frame_h;
                require(outer_w <= area.right - area.left && outer_h <= area.bottom - area.top,
                        "outer window exceeds the work area at scale " + std::to_string(scale));
                // Within one pixel of 16:9 (the owner's table rounds; the clamp floors).
                if (scale > 1) {
                    const std::int32_t skew = client.width * 9 - client.height * 16;
                    require(skew <= 9 && skew >= -9, "client not 16:9 at scale " + std::to_string(scale));
                }
            }
        }
        // Centring: the 3x window in a 1000x700 work area offset by (100, 50).
        const Rect offset{100, 50, 1100, 750};
        const Size small_client = gt4::window_geometry::client_size(3, offset, frame_w, frame_h);
        const Rect placed = gt4::window_geometry::centred_rect(offset, small_client.width + frame_w,
                                                               small_client.height + frame_h);
        require(placed.left - offset.left == (offset.right - offset.left - (small_client.width + frame_w)) / 2,
                "horizontal centring");
        require(placed.top - offset.top == (offset.bottom - offset.top - (small_client.height + frame_h)) / 2,
                "vertical centring");
        // The shared parser refuses anything but 1 to 4.
        require(gt4::coregs::parse_scale("3") == 3, "parse 3");
        for (const char *refused : {"0", "5", "2x", "", "x"}) {
            bool threw = false;
            try { gt4::coregs::parse_scale(refused); } catch (const std::runtime_error &) { threw = true; }
            require(threw, std::string("parse_scale refuses ") + refused);
        }
        std::fprintf(stderr, "[window-geometry] scales 1-4, clamps and centring hold\n");
        std::fprintf(stderr, "[window-geometry] PASS\n");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "[window-geometry] FAIL: %s\n", error.what());
        return 1;
    }
}
