// SPDX-License-Identifier: MIT
//
// CPU reference for the CoreGS 1.1 FSR 1.0 pass (slice 182, TASK-028): EASU then RCAS, transcribed
// line by line from AMD's ffx_fsr1.h and ffx_a.h (MIT, Copyright (c) 2021 Advanced Micro Devices, Inc.;
// vendored at native/third_party/fsr1, pinned in native/THIRD_PARTY.md). The GPU shader is the WGSL
// transcription in native/post/present.wgsl (fsr_easu_pixel, fsr_rcas_pixel). Each stage stores an
// 8-bit image between the two, as the GPU does, so the two agree to one level per channel.

#include "post/fsr_constants.hpp"
#include "post/post_reference.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ps2::gs::post_reference {
namespace {

std::uint32_t bits_of(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

float float_of(std::uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

// APrxLoRcpF1, APrxMedRcpF1, APrxLoRsqF1 (ffx_a.h).
float prx_lo_rcp(float a) { return float_of(0x7ef07ebbu - bits_of(a)); }
float prx_med_rcp(float a) {
    const float b = float_of(0x7ef19fffu - bits_of(a));
    return b * (-b * a + 2.0f);
}
float prx_lo_rsq(float a) { return float_of(0x5f347d74u - (bits_of(a) >> 1u)); }

struct Rgb3 {
    float r, g, b;
};
Rgb3 add(Rgb3 a, Rgb3 b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
Rgb3 scale(Rgb3 a, float s) { return {a.r * s, a.g * s, a.b * s}; }
Rgb3 min3c(Rgb3 a, Rgb3 b) { return {std::min(a.r, b.r), std::min(a.g, b.g), std::min(a.b, b.b)}; }
Rgb3 max3c(Rgb3 a, Rgb3 b) { return {std::max(a.r, b.r), std::max(a.g, b.g), std::max(a.b, b.b)}; }

struct Image {
    const std::vector<std::uint32_t> *pixels;
    std::uint32_t width, height;
    Rgb3 texel(std::int64_t x, std::int64_t y) const {
        const std::int64_t cx = std::clamp<std::int64_t>(x, 0, width - 1);
        const std::int64_t cy = std::clamp<std::int64_t>(y, 0, height - 1);
        const std::uint32_t p = (*pixels)[static_cast<std::size_t>(cy) * width + static_cast<std::size_t>(cx)];
        return {static_cast<float>(p & 255u) / 255.0f, static_cast<float>((p >> 8) & 255u) / 255.0f,
                static_cast<float>((p >> 16) & 255u) / 255.0f};
    }
    // textureGather order for one channel: (bottom-left, bottom-right, top-right, top-left).
    std::array<float, 4> gather(float px, float py, int channel) const {
        const auto x0 = static_cast<std::int64_t>(std::floor(px * static_cast<float>(width) - 0.5f));
        const auto y0 = static_cast<std::int64_t>(std::floor(py * static_cast<float>(height) - 0.5f));
        const auto pick = [channel](Rgb3 c) { return channel == 0 ? c.r : channel == 1 ? c.g : c.b; };
        return {pick(texel(x0, y0 + 1)), pick(texel(x0 + 1, y0 + 1)), pick(texel(x0 + 1, y0)), pick(texel(x0, y0))};
    }
};

// Four gathers of one position: R, G and B, each as (BL, BR, TR, TL).
struct Gathers {
    std::array<float, 4> r, g, b;
    std::array<float, 4> luma() const {
        std::array<float, 4> out;
        for (int i = 0; i < 4; ++i) out[i] = b[i] * 0.5f + (r[i] * 0.5f + g[i]);
        return out;
    }
};

Gathers gather_all(const Image &image, float px, float py) {
    return {image.gather(px, py, 0), image.gather(px, py, 1), image.gather(px, py, 2)};
}

// FsrEasuSetF for one bilinear group: returns (dir.x, dir.y, len) contributions.
std::array<float, 3> easu_set(float w, float la, float lb, float lc, float ld, float le) {
    const float dc = ld - lc;
    const float cb = lc - lb;
    float len_x = std::max(std::fabs(dc), std::fabs(cb));
    len_x = prx_lo_rcp(len_x);
    const float dir_x = ld - lb;
    len_x = std::clamp(std::fabs(dir_x) * len_x, 0.0f, 1.0f);
    len_x = len_x * len_x;
    const float ec = le - lc;
    const float ca = lc - la;
    float len_y = std::max(std::fabs(ec), std::fabs(ca));
    len_y = prx_lo_rcp(len_y);
    const float dir_y = le - la;
    len_y = std::clamp(std::fabs(dir_y) * len_y, 0.0f, 1.0f);
    len_y = len_y * len_y;
    return {dir_x * w, dir_y * w, (len_x + len_y) * w};
}

// FsrEasuTapF: returns the weighted colour (xyz) and the weight (w).
struct Tap {
    Rgb3 color;
    float weight;
};
Tap easu_tap(float ox, float oy, float dir_x, float dir_y, float len_x, float len_y, float lob, float clp, Rgb3 c) {
    float vx = ox * dir_x + oy * dir_y;
    float vy = ox * (-dir_y) + oy * dir_x;
    vx *= len_x;
    vy *= len_y;
    float d2 = vx * vx + vy * vy;
    d2 = std::min(d2, clp);
    float wb = (2.0f / 5.0f) * d2 + (-1.0f);
    float wa = lob * d2 + (-1.0f);
    wb = wb * wb;
    wa = wa * wa;
    wb = (25.0f / 16.0f) * wb + (-(25.0f / 16.0f - 1.0f));
    const float w = wb * wa;
    return {scale(c, w), w};
}

Rgb3 easu_pixel(const Image &image, std::uint32_t x, std::uint32_t y, const fsr::EasuConstants &con) {
    const auto con0 = [&](int i) { return con[i]; };
    float ppx = static_cast<float>(x) * con0(0) + con0(2);
    float ppy = static_cast<float>(y) * con0(1) + con0(3);
    const float fpx = std::floor(ppx);
    const float fpy = std::floor(ppy);
    ppx -= fpx;
    ppy -= fpy;
    const float p0x = fpx * con[4] + con[6];
    const float p0y = fpy * con[5] + con[7];
    const float p1x = p0x + con[8], p1y = p0y + con[9];
    const float p2x = p0x + con[10], p2y = p0y + con[11];
    const float p3x = p0x + con[12], p3y = p0y + con[13];
    const Gathers bczz = gather_all(image, p0x, p0y);
    const Gathers ijfe = gather_all(image, p1x, p1y);
    const Gathers klhg = gather_all(image, p2x, p2y);
    const Gathers zzon = gather_all(image, p3x, p3y);
    const auto bczz_l = bczz.luma(), ijfe_l = ijfe.luma(), klhg_l = klhg.luma(), zzon_l = zzon.luma();
    const float bL = bczz_l[0], cL = bczz_l[1];
    const float iL = ijfe_l[0], jL = ijfe_l[1], fL = ijfe_l[2], eL = ijfe_l[3];
    const float kL = klhg_l[0], lL = klhg_l[1], hL = klhg_l[2], gL = klhg_l[3];
    const float oL = zzon_l[2], nL = zzon_l[3];
    const float w_s = (1.0f - ppx) * (1.0f - ppy);
    const float w_t = ppx * (1.0f - ppy);
    const float w_u = (1.0f - ppx) * ppy;
    const float w_v = ppx * ppy;
    std::array<float, 3> acc = easu_set(w_s, bL, eL, fL, gL, jL);
    const auto add_set = [&acc](std::array<float, 3> r) {
        acc[0] += r[0];
        acc[1] += r[1];
        acc[2] += r[2];
    };
    add_set(easu_set(w_t, cL, fL, gL, hL, kL));
    add_set(easu_set(w_u, fL, iL, jL, kL, nL));
    add_set(easu_set(w_v, gL, jL, kL, lL, oL));
    float dir_x = acc[0], dir_y = acc[1];
    float len = acc[2];
    const float dir2x = dir_x * dir_x, dir2y = dir_y * dir_y;
    float dir_r = dir2x + dir2y;
    const bool zro = dir_r < (1.0f / 32768.0f);
    dir_r = prx_lo_rsq(dir_r);
    if (zro) {
        dir_r = 1.0f;
        dir_x = 1.0f;
    }
    dir_x *= dir_r;
    dir_y *= dir_r;
    len = len * 0.5f;
    len = len * len;
    const float stretch = (dir_x * dir_x + dir_y * dir_y) * prx_lo_rcp(std::max(std::fabs(dir_x), std::fabs(dir_y)));
    const float len2x = 1.0f + (stretch - 1.0f) * len;
    const float len2y = 1.0f + (-0.5f) * len;
    const float lob = 0.5f + ((1.0f / 4.0f - 0.04f) - 0.5f) * len;
    const float clp = prx_lo_rcp(lob);
    const Rgb3 min4 = min3c(min3c({ijfe.r[2], ijfe.g[2], ijfe.b[2]}, {klhg.r[3], klhg.g[3], klhg.b[3]}),
                            {ijfe.r[1], ijfe.g[1], ijfe.b[1]});
    const Rgb3 min4_all = min3c(min4, {klhg.r[0], klhg.g[0], klhg.b[0]});
    const Rgb3 max4 = max3c(max3c({ijfe.r[2], ijfe.g[2], ijfe.b[2]}, {klhg.r[3], klhg.g[3], klhg.b[3]}),
                            {ijfe.r[1], ijfe.g[1], ijfe.b[1]});
    const Rgb3 max4_all = max3c(max4, {klhg.r[0], klhg.g[0], klhg.b[0]});
    Rgb3 ac{0.0f, 0.0f, 0.0f};
    float aw = 0.0f;
    const auto tap = [&](float ox, float oy, Rgb3 c) {
        const Tap t = easu_tap(ox - ppx, oy - ppy, dir_x, dir_y, len2x, len2y, lob, clp, c);
        ac = add(ac, t.color);
        aw += t.weight;
    };
    tap(0.0f, -1.0f, {bczz.r[0], bczz.g[0], bczz.b[0]}); // b
    tap(1.0f, -1.0f, {bczz.r[1], bczz.g[1], bczz.b[1]}); // c
    tap(-1.0f, 1.0f, {ijfe.r[0], ijfe.g[0], ijfe.b[0]}); // i
    tap(0.0f, 1.0f, {ijfe.r[1], ijfe.g[1], ijfe.b[1]});  // j
    tap(0.0f, 0.0f, {ijfe.r[2], ijfe.g[2], ijfe.b[2]});  // f
    tap(-1.0f, 0.0f, {ijfe.r[3], ijfe.g[3], ijfe.b[3]}); // e
    tap(1.0f, 1.0f, {klhg.r[0], klhg.g[0], klhg.b[0]});  // k
    tap(2.0f, 1.0f, {klhg.r[1], klhg.g[1], klhg.b[1]});  // l
    tap(2.0f, 0.0f, {klhg.r[2], klhg.g[2], klhg.b[2]});  // h
    tap(1.0f, 0.0f, {klhg.r[3], klhg.g[3], klhg.b[3]});  // g
    tap(1.0f, 2.0f, {zzon.r[2], zzon.g[2], zzon.b[2]});  // o
    tap(0.0f, 2.0f, {zzon.r[3], zzon.g[3], zzon.b[3]});  // n
    const Rgb3 avg = scale(ac, 1.0f / aw);
    return min3c(max4_all, max3c(min4_all, avg));
}

Rgb3 rcas_pixel(const Image &image, std::uint32_t x, std::uint32_t y, float con) {
    const std::int64_t xi = x, yi = y;
    const Rgb3 b = image.texel(xi, yi - 1);
    const Rgb3 d = image.texel(xi - 1, yi);
    const Rgb3 e = image.texel(xi, yi);
    const Rgb3 f = image.texel(xi + 1, yi);
    const Rgb3 h = image.texel(xi, yi + 1);
    const auto min3v = [](float a, float b2, float c) { return std::min(std::min(a, b2), c); };
    const auto max3v = [](float a, float b2, float c) { return std::max(std::max(a, b2), c); };
    const float mn4r = std::min(min3v(b.r, d.r, f.r), h.r), mn4g = std::min(min3v(b.g, d.g, f.g), h.g),
                mn4b = std::min(min3v(b.b, d.b, f.b), h.b);
    const float mx4r = std::max(max3v(b.r, d.r, f.r), h.r), mx4g = std::max(max3v(b.g, d.g, f.g), h.g),
                mx4b = std::max(max3v(b.b, d.b, f.b), h.b);
    const float hit_min_r = std::min(mn4r, e.r) * (1.0f / (4.0f * mx4r));
    const float hit_min_g = std::min(mn4g, e.g) * (1.0f / (4.0f * mx4g));
    const float hit_min_b = std::min(mn4b, e.b) * (1.0f / (4.0f * mx4b));
    const float hit_max_r = (1.0f - std::max(mx4r, e.r)) * (1.0f / (4.0f * mn4r - 4.0f));
    const float hit_max_g = (1.0f - std::max(mx4g, e.g)) * (1.0f / (4.0f * mn4g - 4.0f));
    const float hit_max_b = (1.0f - std::max(mx4b, e.b)) * (1.0f / (4.0f * mn4b - 4.0f));
    const float lobe_r = std::max(-hit_min_r, hit_max_r);
    const float lobe_g = std::max(-hit_min_g, hit_max_g);
    const float lobe_b = std::max(-hit_min_b, hit_max_b);
    const float lobe = std::max(-(0.25f - 1.0f / 16.0f), std::min(std::max(std::max(lobe_r, lobe_g), lobe_b), 0.0f)) * con;
    const float rcp_l = prx_med_rcp(4.0f * lobe + 1.0f);
    return {(lobe * b.r + lobe * d.r + lobe * h.r + lobe * f.r + e.r) * rcp_l,
            (lobe * b.g + lobe * d.g + lobe * h.g + lobe * f.g + e.g) * rcp_l,
            (lobe * b.b + lobe * d.b + lobe * h.b + lobe * f.b + e.b) * rcp_l};
}

std::uint32_t quantise(float value) {
    return static_cast<std::uint32_t>(std::clamp(value * 255.0f + 0.5f, 0.0f, 255.0f));
}

} // namespace

std::vector<std::uint32_t> fsr_reference(const std::vector<std::uint32_t> &frame, std::uint32_t input_width,
                                         std::uint32_t input_height, std::uint32_t output_width, std::uint32_t output_height,
                                         float sharpness) {
    const Image input{&frame, input_width, input_height};
    const fsr::EasuConstants con = fsr::easu_constants(static_cast<float>(input_width), static_cast<float>(input_height),
                                                       static_cast<float>(output_width), static_cast<float>(output_height));
    // EASU into an 8-bit image, as the GPU stores it.
    std::vector<std::uint32_t> upscaled(static_cast<std::size_t>(output_width) * output_height);
    for (std::uint32_t y = 0; y < output_height; ++y)
        for (std::uint32_t x = 0; x < output_width; ++x) {
            const Rgb3 c = easu_pixel(input, x, y, con);
            upscaled[static_cast<std::size_t>(y) * output_width + x] =
                quantise(c.r) | (quantise(c.g) << 8) | (quantise(c.b) << 16) | (255u << 24);
        }
    const Image sharpen_input{&upscaled, output_width, output_height};
    const float rcas = fsr::rcas_constant(sharpness);
    std::vector<std::uint32_t> out(upscaled.size());
    for (std::uint32_t y = 0; y < output_height; ++y)
        for (std::uint32_t x = 0; x < output_width; ++x) {
            const Rgb3 c = rcas_pixel(sharpen_input, x, y, rcas);
            out[static_cast<std::size_t>(y) * output_width + x] =
                quantise(c.r) | (quantise(c.g) << 8) | (quantise(c.b) << 16) | (255u << 24);
        }
    return out;
}

} // namespace ps2::gs::post_reference
