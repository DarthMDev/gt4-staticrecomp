// SPDX-License-Identifier: MIT
//
// HWR uber pixel shader (TASK-027, M4). Copyright (c) 2026 GT4Recomp contributors.
//
// Written from docs/hw-renderer-spec chapters 03.4, 08.8 and 12.6, clean room (decision 0069).
//
// Destination reads use storage images (r32ui frame words and depth words) rather than input
// attachments: the target is written by imageStore in the same invocation after the read, so no
// colour attachment is needed (fragmentStoresAndAtomics, chapter 12.6). Overlap ordering between
// primitives is the barrier policy of chapter 08.8 and is not handled here.
//
// Texel source: one r32ui storage image of raw texels; point sampling with repeat wrap only (full
// addressing is a later milestone, IN-212). The fragment colour is the interpolated colour rounded to
// the nearest integer (IN-203); Z is the linearly interpolated float (IN-205).

#version 450
#extension GL_GOOGLE_include_directive : require
#include "hw_pixel_core.glsl"

#if HWR_FLAT_COLOUR
layout(location = 2) flat in vec4 in_rgba;
#else
layout(location = 2) in vec4 in_rgba;
#endif
layout(location = 0) in vec2 in_st;
layout(location = 1) in float in_q;
layout(location = 3) in float in_fog;
layout(location = 4) in vec2 in_uv;
layout(location = 5) noperspective in float in_z;

layout(std140, set = 0, binding = 0) uniform PixelStateBlock {
    uint format;
    uint fbmsk;
    uint fba;
    uint dither;
    uint dimx_lo;
    uint dimx_hi;
    uint ate;
    uint atst;
    uint afail;
    uint aref;
    uint date_on;
    uint datm;
    uint ztest;
    uint ztst;
    uint zmsk;
    uint zformat;
    uint blend_on;
    uint pabe;
    uint blend_a;
    uint blend_b;
    uint blend_c;
    uint blend_d;
    uint fix;
    uint colclamp;
    uint tex_on;
    uint tfx;
    uint tcc;
    uint texel_kind;
    uint ta0;
    uint ta1;
    uint aem;
    uint fog_on;
    uint fog_r;
    uint fog_g;
    uint fog_b;
} state;

layout(set = 0, binding = 1, r32ui) uniform uimage2D hw_frame;
layout(set = 0, binding = 2, r32ui) uniform uimage2D hw_depth;
layout(set = 0, binding = 3, r32ui) uniform readonly uimage2D hw_texels;

void main() {
    ivec2 pixel = ivec2(floor(gl_FragCoord.xy));

    HwPixelState s;
    s.format = state.format;
    s.fbmsk = state.fbmsk;
    s.fba = state.fba;
    s.dither = state.dither;
    s.dimx_lo = state.dimx_lo;
    s.dimx_hi = state.dimx_hi;
    s.ate = state.ate;
    s.atst = state.atst;
    s.afail = state.afail;
    s.aref = state.aref;
    s.date_on = state.date_on;
    s.datm = state.datm;
    s.ztest = state.ztest;
    s.ztst = state.ztst;
    s.zmsk = state.zmsk;
    s.zformat = state.zformat;
    s.blend_on = state.blend_on;
    s.pabe = state.pabe;
    s.blend_a = state.blend_a;
    s.blend_b = state.blend_b;
    s.blend_c = state.blend_c;
    s.blend_d = state.blend_d;
    s.fix = state.fix;
    s.colclamp = state.colclamp;

    // Point-sampled texel with repeat wrap (IN-212).
    ivec2 texel_size = imageSize(hw_texels);
    ivec2 texel_xy = (ivec2(floor(in_uv)) >> 4) & (texel_size - ivec2(1));
    uint texel_raw = imageLoad(hw_texels, texel_xy).x;

    ivec4 frag = ivec4(floor(in_rgba + vec4(0.5)));
    ivec4 shaded = hw_shade_fragment(state.tex_on != 0u, state.tfx, state.tcc != 0u, state.texel_kind,
                                     texel_raw, state.ta0, state.ta1, state.aem != 0u, state.fog_on != 0u,
                                     int(floor(in_fog + 0.5)), ivec3(int(state.fog_r), int(state.fog_g), int(state.fog_b)),
                                     frag);

    HwFragment f;
    f.x = uint(pixel.x);
    f.y = uint(pixel.y);
    f.colour = shaded;
    f.z = uint(clamp(in_z, 0.0, 4294967040.0));

    uint frame_old = imageLoad(hw_frame, pixel).x;
    uint depth_old = imageLoad(hw_depth, pixel).x;
    HwOutcome o = hw_process_pixel(s, f, frame_old, depth_old);
    if (o.frame_written) {
        imageStore(hw_frame, pixel, uvec4(o.frame_stored));
    }
    if (o.depth_written) {
        imageStore(hw_depth, pixel, uvec4(o.depth_stored));
    }
}
