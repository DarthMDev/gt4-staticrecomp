// SPDX-License-Identifier: MIT
//
// Host constants for the CoreGS 1.1 FSR 1.0 pass (slice 182, TASK-028), computed as FsrEasuCon()
// and FsrRcasCon() in AMD's ffx_fsr1.h (MIT; vendored under native/third_party/fsr1). Shared by the
// GPU adapter and the CPU reference so both use the same numbers.

#pragma once

#include <array>
#include <cmath>

namespace ps2::gs::fsr {

// con0..con3 of FsrEasuCon with no input offset: viewport = input size.
using EasuConstants = std::array<float, 16>;

inline EasuConstants easu_constants(float input_width, float input_height, float output_width, float output_height) {
    const float rcp_out_w = 1.0f / output_width;
    const float rcp_out_h = 1.0f / output_height;
    const float rcp_in_w = 1.0f / input_width;
    const float rcp_in_h = 1.0f / input_height;
    return {
        // con0: output pixel to input position.
        input_width * rcp_out_w, input_height * rcp_out_h,
        0.5f * input_width * rcp_out_w - 0.5f, 0.5f * input_height * rcp_out_h - 0.5f,
        // con1: normalised position of 'f' and the first gather centre.
        rcp_in_w, rcp_in_h, 1.0f * rcp_in_w, -1.0f * rcp_in_h,
        // con2: gather centres of 'i' and 'l'.
        -1.0f * rcp_in_w, 2.0f * rcp_in_h, 1.0f * rcp_in_w, 2.0f * rcp_in_h,
        // con3: gather centre of 'o' and 'n'.
        0.0f * rcp_in_w, 4.0f * rcp_in_h, 0.0f, 0.0f,
    };
}

// FsrRcasCon: the sharpness in stops (0 is the sharpest), as the lobe scale 2^-sharpness.
inline float rcas_constant(float sharpness) { return std::exp2(-sharpness); }

} // namespace ps2::gs::fsr
