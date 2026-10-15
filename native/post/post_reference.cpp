// SPDX-License-Identifier: MIT
//
// CPU reference for the CoreGS 1.1 present-path post passes (slice 182, TASK-028). See post_reference.hpp.

#include "post/post_reference.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ps2::gs::post_reference {
namespace {

std::uint32_t channel(std::uint32_t pixel, unsigned index) { return (pixel >> (8u * index)) & 255u; }

std::uint32_t pack(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a) {
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

// (a + b + 1) / 2 per channel, as average() in the shader.
std::uint32_t average(std::uint32_t a, std::uint32_t b) {
    std::uint32_t out = 0;
    for (unsigned k = 0; k < 4; ++k) out |= ((channel(a, k) + channel(b, k) + 1u) / 2u) << (8u * k);
    return out;
}

// (p + 2q + r + 2) / 4 per channel, as tap3() in the shader.
std::uint32_t tap3(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    std::uint32_t out = 0;
    for (unsigned k = 0; k < 4; ++k) out |= ((channel(a, k) + 2u * channel(b, k) + channel(c, k) + 2u) / 4u) << (8u * k);
    return out;
}

// Any of R, G, B differs by more than the threshold (fraction of 255), as exceeds() in the shader.
bool exceeds(std::uint32_t a, std::uint32_t b, float threshold) {
    for (unsigned k = 0; k < 3; ++k) {
        const float delta = std::fabs(static_cast<float>(channel(a, k)) - static_cast<float>(channel(b, k))) / 255.0f;
        if (delta > threshold) return true;
    }
    return false;
}

float luma(std::uint32_t pixel) {
    const float r = static_cast<float>(channel(pixel, 0)) / 255.0f;
    const float g = static_cast<float>(channel(pixel, 1)) / 255.0f;
    const float b = static_cast<float>(channel(pixel, 2)) / 255.0f;
    return r * 0.299f + g * 0.587f + b * 0.114f;
}

struct Rgb {
    float r, g, b;
};

Rgb operator+(Rgb a, Rgb b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
Rgb operator*(Rgb a, float s) { return {a.r * s, a.g * s, a.b * s}; }
Rgb mix(Rgb a, Rgb b, float t) { return a * (1.0f - t) + b * t; }

// Bilinear sample at pixel-centre coordinates with clamp-to-edge, divided by 255 (as fxaa_sample()).
Rgb sample(const std::vector<std::uint32_t> &frame, std::uint32_t width, std::uint32_t height, float x, float y) {
    const float fx = x - 0.5f;
    const float fy = y - 0.5f;
    const float x0 = std::floor(fx);
    const float y0 = std::floor(fy);
    const float tx = fx - x0;
    const float ty = fy - y0;
    const auto clampi = [](std::int64_t v, std::int64_t hi) { return std::clamp<std::int64_t>(v, 0, hi); };
    const std::int64_t ix0 = clampi(static_cast<std::int64_t>(x0), width - 1);
    const std::int64_t ix1 = clampi(static_cast<std::int64_t>(x0) + 1, width - 1);
    const std::int64_t iy0 = clampi(static_cast<std::int64_t>(y0), height - 1);
    const std::int64_t iy1 = clampi(static_cast<std::int64_t>(y0) + 1, height - 1);
    const auto texel = [&](std::int64_t ix, std::int64_t iy) {
        const std::uint32_t p = frame[static_cast<std::size_t>(iy) * width + static_cast<std::size_t>(ix)];
        return Rgb{static_cast<float>(channel(p, 0)), static_cast<float>(channel(p, 1)), static_cast<float>(channel(p, 2))};
    };
    const Rgb top = mix(texel(ix0, iy0), texel(ix1, iy0), tx);
    const Rgb bottom = mix(texel(ix0, iy1), texel(ix1, iy1), tx);
    return mix(top, bottom, ty) * (1.0f / 255.0f);
}

} // namespace

void Deinterlacer::reset() {
    width_ = height_ = 0;
    has_output_ = has_even_ = has_odd_ = false;
    output_.clear();
    even_.clear();
    odd_.clear();
}

std::vector<std::uint32_t> Deinterlacer::woven(const std::vector<std::uint32_t> &frame, std::uint32_t width,
                                               std::uint32_t height, std::uint32_t field) {
    std::vector<std::uint32_t> out = frame;
    if (has_output_) {
        // Rows of the current field come from the frame; the other rows keep the previous output.
        for (std::uint32_t y = 0; y < height; ++y)
            if ((y & 1u) != field)
                for (std::uint32_t x = 0; x < width; ++x) out[y * width + x] = output_[y * width + x];
    }
    output_ = out;
    has_output_ = true;
    return out;
}

std::vector<std::uint32_t> Deinterlacer::process(const std::vector<std::uint32_t> &frame, std::uint32_t width,
                                                 std::uint32_t height, PostDeinterlace mode, std::uint32_t field,
                                                 std::uint32_t scale, float threshold) {
    if (width != width_ || height != height_) {
        reset();
        width_ = width;
        height_ = height;
    }
    field &= 1u;
    switch (mode) {
    case PostDeinterlace::off:
    case PostDeinterlace::automatic:
        return frame;
    case PostDeinterlace::weave:
        return woven(frame, width, height, field);
    case PostDeinterlace::blend: {
        const std::vector<std::uint32_t> w = woven(frame, width, height, field);
        std::vector<std::uint32_t> out(frame.size());
        for (std::uint32_t y = 0; y < height; ++y) {
            const std::uint32_t up = y == 0 ? 0 : y - 1;
            const std::uint32_t down = std::min(y + 1, height - 1);
            for (std::uint32_t x = 0; x < width; ++x)
                out[y * width + x] = tap3(w[up * width + x], w[y * width + x], w[down * width + x]);
        }
        return out;
    }
    case PostDeinterlace::bob: {
        // Shift by scale * (1 - field) rows, clamped at the top and bottom edges.
        const std::int64_t shift = static_cast<std::int64_t>(scale) * (1 - static_cast<std::int64_t>(field));
        std::vector<std::uint32_t> out(frame.size());
        for (std::uint32_t y = 0; y < height; ++y) {
            const std::int64_t source_row = std::clamp<std::int64_t>(static_cast<std::int64_t>(y) - shift, 0, height - 1);
            for (std::uint32_t x = 0; x < width; ++x)
                out[y * width + x] = frame[static_cast<std::size_t>(source_row) * width + x];
        }
        return out;
    }
    case PostDeinterlace::adaptive: {
        std::vector<std::uint32_t> &previous_field = field == 0 ? even_ : odd_;
        const bool has_previous_field = field == 0 ? has_even_ : has_odd_;
        const bool has_previous_output = has_output_;
        std::vector<std::uint32_t> out(frame.size());
        for (std::uint32_t y = 0; y < height; ++y) {
            const bool current_row = (y & 1u) == field;
            const bool edge_row = y == 0 || y + 1 == height;
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * width + x;
                if (current_row || edge_row) {
                    out[i] = frame[i];
                    continue;
                }
                const std::uint32_t up = frame[i - width];
                const std::uint32_t down = frame[i + width];
                const std::uint32_t bob = average(up, down);
                bool motion = true;
                if (has_previous_field && has_previous_output) {
                    const std::uint32_t prev_up = previous_field[i - width];
                    const std::uint32_t prev_down = previous_field[i + width];
                    motion = exceeds(up, prev_up, threshold) || exceeds(down, prev_down, threshold) ||
                             exceeds(bob, output_[i], threshold);
                }
                out[i] = (motion || !has_previous_output) ? bob : output_[i];
            }
        }
        output_ = out;
        has_output_ = true;
        previous_field = frame;
        if (field == 0) has_even_ = true;
        else has_odd_ = true;
        return out;
    }
    }
    return frame;
}

std::vector<std::uint32_t> fxaa(const std::vector<std::uint32_t> &frame, std::uint32_t width, std::uint32_t height) {
    std::vector<std::uint32_t> out(frame.size());
    const auto at = [&](std::int64_t x, std::int64_t y) {
        const std::int64_t cx = std::clamp<std::int64_t>(x, 0, width - 1);
        const std::int64_t cy = std::clamp<std::int64_t>(y, 0, height - 1);
        return frame[static_cast<std::size_t>(cy) * width + static_cast<std::size_t>(cx)];
    };
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint32_t center = frame[static_cast<std::size_t>(y) * width + x];
            const std::int64_t xi = x, yi = y;
            const float lum_m = luma(center);
            const float lum_n = luma(at(xi, yi - 1));
            const float lum_s = luma(at(xi, yi + 1));
            const float lum_w = luma(at(xi - 1, yi));
            const float lum_e = luma(at(xi + 1, yi));
            const float lum_nw = luma(at(xi - 1, yi - 1));
            const float lum_ne = luma(at(xi + 1, yi - 1));
            const float lum_sw = luma(at(xi - 1, yi + 1));
            const float lum_se = luma(at(xi + 1, yi + 1));
            const float lum_min = std::min(lum_m, std::min(std::min(lum_n, lum_s), std::min(lum_w, lum_e)));
            const float lum_max = std::max(lum_m, std::max(std::max(lum_n, lum_s), std::max(lum_w, lum_e)));
            const float range = lum_max - lum_min;
            if (range < std::max(0.0312f, lum_max * 0.125f)) {
                out[static_cast<std::size_t>(y) * width + x] = center;
                continue;
            }
            float dir_x = -((lum_nw + lum_ne) - (lum_sw + lum_se));
            float dir_y = (lum_nw + lum_sw) - (lum_ne + lum_se);
            const float dir_reduce = std::max((lum_nw + lum_ne + lum_sw + lum_se) * (0.25f * 0.125f), 1.0f / 128.0f);
            const float rcp_dir_min = 1.0f / (std::min(std::fabs(dir_x), std::fabs(dir_y)) + dir_reduce);
            dir_x = std::clamp(dir_x * rcp_dir_min, -8.0f, 8.0f);
            dir_y = std::clamp(dir_y * rcp_dir_min, -8.0f, 8.0f);
            const float cx = static_cast<float>(x) + 0.5f;
            const float cy = static_cast<float>(y) + 0.5f;
            const Rgb a1 = sample(frame, width, height, cx + dir_x * (1.0f / 3.0f - 0.5f), cy + dir_y * (1.0f / 3.0f - 0.5f));
            const Rgb a2 = sample(frame, width, height, cx + dir_x * (2.0f / 3.0f - 0.5f), cy + dir_y * (2.0f / 3.0f - 0.5f));
            const Rgb rgb_a = (a1 + a2) * 0.5f;
            const Rgb b1 = sample(frame, width, height, cx - dir_x * 0.5f, cy - dir_y * 0.5f);
            const Rgb b2 = sample(frame, width, height, cx + dir_x * 0.5f, cy + dir_y * 0.5f);
            const Rgb rgb_b = rgb_a * 0.5f + (b1 + b2) * 0.25f;
            const float lum_b = rgb_b.r * 0.299f + rgb_b.g * 0.587f + rgb_b.b * 0.114f;
            const Rgb chosen = (lum_b < lum_min || lum_b > lum_max) ? rgb_a : rgb_b;
            const auto to_byte = [](float v) { return static_cast<std::uint32_t>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f)); };
            out[static_cast<std::size_t>(y) * width + x] =
                pack(to_byte(chosen.r), to_byte(chosen.g), to_byte(chosen.b), channel(center, 3));
        }
    }
    return out;
}

} // namespace ps2::gs::post_reference
