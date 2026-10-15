// GT4Recomp slice 176 (TASK-012): high-resolution scanout at 3x and 4x per axis.
// Derived from paraLLEl-GS gs/shaders/sample_circuit.frag (LGPL-3.0+, Arntzen Software AS, pinned
// cc6184a). The pinned donor shader only resolves 2x (2x2 quadrant averages). This variant reads the
// 16x ordered grid (4x4 super samples per PS2 pixel) directly:
//   - OUTPUT_SCALE 4: every output pixel is one super sample (no averaging).
//   - OUTPUT_SCALE 3: every output pixel takes the nearest super sample to its centre.
// Sample storage matches ubershader.comp (NUM_SAMPLES 16): sample_index = sx&1 + 2*(sy&1) ... see
// sample_index_of(). Requires GSRenderer's 16x ordered rate; the caller refuses other rates.
// Build the SPIR-V with native/parallel_gs_scale/build_spirv.cmd (see that file for the defines).
#version 450

#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_GOOGLE_include_directive : require
#include "data_structures.h"
#include "swizzle_utils.h"
#include "utils.h"

#ifndef PROMOTED
#define PROMOTED 0
#endif

layout(location = 0) out vec4 FragColor;

#if PROMOTED
layout(set = 0, binding = 0) uniform texture2DArray uPromoted;
#else
// One 32-bit buffer only: two declarations on binding 0 would give the SPIR-V two variables on one
// binding, which pipeline creation rejects. 16-bit texels are extracted from their 32-bit word
// (little endian: texel 2k is the low half of word k).
layout(set = 0, binding = 0) readonly buffer VRAM32
{
    uint data[];
} vram32;

uint vram32_word(uint word_index)
{
    return vram32.data[word_index];
}

uint vram16_texel(uint texel_index)
{
    return (vram32.data[texel_index >> 1u] >> ((texel_index & 1u) * 16u)) & 0xffffu;
}
#endif

layout(push_constant) uniform Registers
{
    uint fbp;
    uint fbw;
    uint dbx;
    uint dby;
    uint phase;
    uint phase_stride;
} registers;

layout(constant_id = 0) const int PSM = 0;
layout(constant_id = 1) const uint VRAM_MASK = 4 * 1024 * 1024 - 1;
// 3 or 4: output pixels per PS2 pixel along each axis.
layout(constant_id = 2) const uint OUTPUT_SCALE = 4;

const bool is_tex_16bit = PSM == PSMCT16 || PSM == PSMCT16S || PSM == PSMZ16 || PSM == PSMZ16S;

#if PROMOTED
vec4 sample_vram(uvec2 coord, uint slice)
{
    return texelFetch(uPromoted, ivec3(coord, slice), 0);
}
#else
vec4 sample_vram(uint addr, uint slice)
{
    uint payload;

    if (is_tex_16bit)
    {
        payload = rgba16_to_rgba32(vram16_texel(addr + slice * (VRAM_MASK + 1) / 2), 0, 0x0, 0xff);
    }
    else
    {
        payload = vram32_word(addr + slice * (VRAM_MASK + 1) / 4);
        if (PSM != PSMCT32)
        {
            payload &= ~0xff000000u;
            payload |= 0x80000000u;
        }
    }

    return unpackUnorm4x8(payload);
}
#endif

#if PROMOTED
bool super_sample_is_valid(uvec2 coord) { return true; }
#else
bool super_sample_is_valid(uint addr)
{
    bool is_valid;

    if (is_tex_16bit)
    {
        is_valid = vram16_texel(addr + (VRAM_MASK + 1) / 2) == 0xffff;
    }
    else
    {
        uint payload = vram32_word(addr + (VRAM_MASK + 1) / 4);
        if (PSM != PSMCT32)
            is_valid = (payload & 0xffffffu) == 0xffffffu;
        else
            is_valid = payload == ~0u;
    }

    return is_valid;
}
#endif

// Slot of the super sample at grid position (sx, sy), 0..3 each, inside one PS2 pixel.
// Same bit layout as the sample_index that ubershader.comp derives for a 16-sample pixel
// (local_pixel.x = b1 + 2*b3, local_pixel.y = b0 + 2*b2 with b = bits of the lane).
uint sample_index_of(uint sx, uint sy)
{
    return (sy & 1u) + 2u * (sx & 1u) + 4u * ((sy >> 1) & 1u) + 8u * ((sx >> 1) & 1u);
}

void main()
{
    uvec2 output_pixel = uvec2(gl_FragCoord.xy);
    uvec2 single_sampled_coord = output_pixel / OUTPUT_SCALE;
    uvec2 inside = output_pixel - single_sampled_coord * OUTPUT_SCALE;

    // Nearest grid sample to the output pixel centre: grid = floor((2*inside+1) * 4 / (2*OUTPUT_SCALE)).
    // For OUTPUT_SCALE 4 this is inside itself; for 3 it selects grid 0, 2 or 3.
    uvec2 grid = ((2u * inside + 1u) * 4u) / (2u * OUTPUT_SCALE);

    // Is this how full-height interlace is supposed to work? :|
    uvec2 coord = single_sampled_coord * uvec2(1u, registers.phase_stride) +
        uvec2(registers.dbx, registers.dby + registers.phase);

#if PROMOTED
    #define addr coord
    const int BASE_SSAA_LAYER = 1;
#else
    uint addr = swizzle_PS2(coord.x, coord.y, registers.fbp * PGS_BLOCKS_PER_PAGE, registers.fbw, PSM, VRAM_MASK);
    const int BASE_SSAA_LAYER = 2;
#endif

    if (super_sample_is_valid(addr))
        FragColor = sample_vram(addr, uint(BASE_SSAA_LAYER) + sample_index_of(grid.x, grid.y));
    else
        FragColor = sample_vram(addr, 0);
}
