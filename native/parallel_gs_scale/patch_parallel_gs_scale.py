#!/usr/bin/env python3
"""GT4Recomp slice 176 (TASK-012): 3x and 4x high-resolution scanout for paraLLEl-GS.

Writes patched copies of the pinned paraLLEl-GS files into a build-tree directory. The pinned
checkout (private/references/parallel-gs, cc6184a) is only read, never modified. Every anchor must
occur exactly the expected number of times, otherwise the script fails and configure stops.

Inputs:  <vendor gs dir>/gs_interface.hpp, gs_renderer.hpp, gs_renderer.cpp
         native/parallel_gs_scale/sample_circuit_scaled_p{0,1}.spv (from sample_circuit_scaled.frag)
Outputs: <out dir>/gs_interface.hpp, gs_renderer.hpp, gs_renderer.cpp, parallel_gs_scale_spirv.inc

Usage: patch_parallel_gs_scale.py --vendor-gs DIR --spv-dir DIR --out DIR
"""

import argparse
import hashlib
import os
import struct
import sys

# SHA-256 of the committed SPIR-V (glslangValidator 1.4.341.1, vulkan1.2 target). A different
# compiler build changes these bytes; regenerate and update the pins together.
SPV_PINS = {
    "sample_circuit_scaled_p0.spv": "f680921cb761c96fea1dc57f0fc54492628a0b3299de6bb062c67302da6032a3",
    "sample_circuit_scaled_p1.spv": "2ed68b7a52ca9208dfcc92cff7ca11152204fa5d88726bf49fa186aabda6f28e",
}


def replace_exact(text, old, new, expected, name):
    found = text.count(old)
    if found != expected:
        sys.exit(f"anchor '{name}': expected {expected} occurrence(s), found {found}")
    return text.replace(old, new)


def read_text(path):
    with open(path, "rb") as handle:
        raw = handle.read()
    if b"\r\n" in raw:
        sys.exit(f"{path}: CRLF line endings; the pinned checkout must be LF (core.autocrlf=false)")
    return raw.decode("utf-8")


def patch_interface_header(text):
    return replace_exact(
        text,
        "\tbool high_resolution_scanout;\n",
        "\tbool high_resolution_scanout;\n"
        "\t// GT4Recomp slice 176: integer scale per axis of the high-resolution scanout (2, 3 or 4).\n"
        "\t// 2 is the donor 2x path; 3 and 4 need the 16x ordered grid and progressive frames.\n"
        "\tuint32_t high_resolution_scale = 2;\n",
        1, "VSyncInfo high_resolution_scanout")


def patch_renderer_header(text):
    text = replace_exact(
        text,
        "const SamplingRect &rect, uint32_t super_samples,",
        "const SamplingRect &rect, uint32_t super_samples, uint32_t output_scale,",
        1, "sample_crtc_circuit declaration")
    text = replace_exact(
        text,
        "\tbool high_resolution_scanout;\n",
        "\tbool high_resolution_scanout;\n"
        "\t// GT4Recomp slice 176: scale per axis of the high-resolution image actually used (1 when off).\n"
        "\tuint32_t high_resolution_scale = 1;\n",
        1, "ScanoutResult high_resolution_scanout")
    return text


def patch_renderer_source(text):
    text = replace_exact(
        text,
        '#include "shaders/slangmosh.hpp"\n',
        '#include "shaders/slangmosh.hpp"\n'
        '#include "parallel_gs_scale_spirv.inc" // GT4Recomp slice 176\n',
        1, "slangmosh include")

    # sample_crtc_circuit takes the output scale: 2 is the donor quad path, 3 and 4 use the new shader.
    text = replace_exact(
        text,
        "const SamplingRect &rect, uint32_t super_samples,",
        "const SamplingRect &rect, uint32_t super_samples, uint32_t output_scale,",
        1, "sample_crtc_circuit definition")
    text = replace_exact(
        text,
        "\tcmd.set_program(sample_quad[promoted ? 1 : 0]);\n",
        "\tif (output_scale == 2)\n"
        "\t\tcmd.set_program(sample_quad[promoted ? 1 : 0]);\n"
        "\telse\n"
        "\t{\n"
        "\t\t// GT4Recomp slice 176: this build has no SPIRV-Cross, so the resource layout is spelled out here.\n"
        "\t\t// Set 0 binding 0 is the VRAM buffer (or the promoted texture array); six push constants; spec 0..2.\n"
        "\t\tVulkan::ResourceLayout scaled_layout = {};\n"
        "\t\tif (promoted)\n"
        "\t\t\tscaled_layout.sets[0].sampled_image_mask = 1u;\n"
        "\t\telse\n"
        "\t\t\tscaled_layout.sets[0].storage_buffer_mask = 1u;\n"
        "\t\tscaled_layout.push_constant_size = 6 * sizeof(uint32_t);\n"
        "\t\tscaled_layout.spec_constant_mask = 0x7u;\n"
        "\t\t// Colour output at location 0; Granite derives render_target_mask from it.\n"
        "\t\tscaled_layout.output_mask = 1u;\n"
        "\t\tcmd.set_program(device->request_program(shaders.quad, device->request_shader(\n"
        "\t\t\tgt4_parallel_gs_scale::sample_circuit_scaled_words(promoted != nullptr),\n"
        "\t\t\tgt4_parallel_gs_scale::sample_circuit_scaled_bytes(promoted != nullptr), &scaled_layout)));\n"
        "\t}\n",
        1, "sample_crtc_circuit program")
    text = replace_exact(
        text,
        "\tcmd.set_specialization_constant(2, super_samples);\n",
        "\t// Constant 2 is SUPER_SAMPLES for the donor shader and OUTPUT_SCALE for the scaled shader.\n"
        "\tcmd.set_specialization_constant(2, output_scale == 2 ? super_samples : output_scale);\n",
        1, "sample_crtc_circuit specialization")
    text = replace_exact(
        text,
        "\t\tvalid_extent.width *= 2;\n",
        "\t\tvalid_extent.width *= output_scale;\n",
        1, "sample_crtc_circuit valid width")
    text = replace_exact(
        text,
        "\t\tvalid_extent.height *= 2;\n",
        "\t\tvalid_extent.height *= output_scale;\n",
        1, "sample_crtc_circuit valid height")

    # Scanout geometry: every former "2" of the high-resolution path becomes the chosen scale.
    text = replace_exact(
        text,
        "\tuint32_t super_samples = 1;\n",
        "\t// GT4Recomp slice 176: 3x and 4x only from the 16x ordered grid on progressive frames. Field-aware\n"
        "\t// frames and 2x requests keep the donor 2x path.\n"
        "\tuint32_t high_resolution_scale = 2;\n"
        "\tif (high_resolution_scanout && info.high_resolution_scale > 2 && !field_aware_rendering &&\n"
        "\t    sampling_rate_x_log2 == 2 && sampling_rate_y_log2 == 2)\n"
        "\t\thigh_resolution_scale = info.high_resolution_scale;\n"
        "\n"
        "\tuint32_t super_samples = 1;\n",
        1, "super_samples declaration")
    text = replace_exact(
        text,
        "\t\t\t\timage_info.width *= 2;\n\t\t\t\timage_info.height *= 2;\n",
        "\t\t\t\timage_info.width *= high_resolution_scale;\n\t\t\t\timage_info.height *= high_resolution_scale;\n",
        2, "circuit image size")
    text = replace_exact(
        text,
        "sample_crtc_circuit(cmd, *circuit1, priv.dispfb1, rect, super_samples, promoted1);",
        "sample_crtc_circuit(cmd, *circuit1, priv.dispfb1, rect, super_samples,\n"
        "\t\t\t                    high_resolution_scanout ? high_resolution_scale : 2u, promoted1);",
        1, "circuit1 sample call")
    text = replace_exact(
        text,
        "sample_crtc_circuit(cmd, *circuit2, priv.dispfb2, rect, super_samples, promoted2);",
        "sample_crtc_circuit(cmd, *circuit2, priv.dispfb2, rect, super_samples,\n"
        "\t\t\t                    high_resolution_scanout ? high_resolution_scale : 2u, promoted2);",
        1, "circuit2 sample call")
    text = replace_exact(
        text,
        "\t\t\thoriz_resolution0 >>= 1;\n\t\t\thoriz_resolution1 >>= 1;\n",
        "\t\t\thoriz_resolution0 /= high_resolution_scale;\n\t\t\thoriz_resolution1 /= high_resolution_scale;\n",
        1, "CRTC horizontal resolution")
    text = replace_exact(
        text,
        "\t\t\teffective_mode_width *= 2;\n\t\t\teffective_mode_height *= 2;\n",
        "\t\t\teffective_mode_width *= high_resolution_scale;\n\t\t\teffective_mode_height *= high_resolution_scale;\n",
        1, "raw circuit effective size")
    text = replace_exact(
        text,
        "\tresult.high_resolution_scanout = high_resolution_scanout;\n",
        "\tresult.high_resolution_scanout = high_resolution_scanout;\n"
        "\tresult.high_resolution_scale = high_resolution_scanout ? high_resolution_scale : 1;\n",
        1, "result high_resolution_scanout")
    text = replace_exact(
        text,
        "result.internal_width = result.image->get_width() >> int(high_resolution_scanout);",
        "result.internal_width = result.image->get_width() / (high_resolution_scanout ? high_resolution_scale : 1u);",
        1, "raw circuit internal width")
    text = replace_exact(
        text,
        "result.internal_height = result.image->get_height() >> int(high_resolution_scanout);",
        "result.internal_height = result.image->get_height() / (high_resolution_scanout ? high_resolution_scale : 1u);",
        1, "raw circuit internal height")
    text = replace_exact(
        text,
        "\timage_info.width = mode_width << int(high_resolution_scanout);\n"
        "\timage_info.height = mode_height << int(high_resolution_scanout);\n",
        "\timage_info.width = mode_width * (high_resolution_scanout ? high_resolution_scale : 1u);\n"
        "\timage_info.height = mode_height * (high_resolution_scanout ? high_resolution_scale : 1u);\n",
        1, "merged image size")
    text = replace_exact(
        text,
        "\t\t\t\tvp.x *= 2.0f;\n\t\t\t\tvp.y *= 2.0f;\n\t\t\t\tvp.width *= 2.0f;\n\t\t\t\tvp.height *= 2.0f;\n",
        "\t\t\t\tvp.x *= float(high_resolution_scale);\n\t\t\t\tvp.y *= float(high_resolution_scale);\n"
        "\t\t\t\tvp.width *= float(high_resolution_scale);\n\t\t\t\tvp.height *= float(high_resolution_scale);\n",
        2, "viewport scale")
    return text


def spirv_array(name, words):
    lines = []
    for start in range(0, len(words), 8):
        lines.append("\t" + ", ".join(f"0x{word:08x}u" for word in words[start:start + 8]) + ",")
    body = "\n".join(lines)
    return f"static const uint32_t {name}[] = {{\n{body}\n}};\n"


def load_spirv(spv_dir, name):
    path = os.path.join(spv_dir, name)
    with open(path, "rb") as handle:
        raw = handle.read()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != SPV_PINS[name]:
        sys.exit(f"{path}: sha256 {digest} does not match the pin; regenerate and update SPV_PINS")
    if len(raw) % 4 or struct.unpack_from("<I", raw, 0)[0] != 0x07230203:
        sys.exit(f"{path}: not a SPIR-V module")
    return list(struct.unpack(f"<{len(raw) // 4}I", raw))


def spirv_include(spv_dir):
    donor = load_spirv(spv_dir, "sample_circuit_scaled_p0.spv")
    promoted = load_spirv(spv_dir, "sample_circuit_scaled_p1.spv")
    return (
        "// Generated by native/parallel_gs_scale/patch_parallel_gs_scale.py from sample_circuit_scaled.frag.\n"
        "// Do not edit. GT4Recomp slice 176 (TASK-012).\n"
        "#pragma once\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "namespace gt4_parallel_gs_scale {\n"
        + spirv_array("kSampleCircuitScaledWords", donor)
        + spirv_array("kSampleCircuitScaledPromotedWords", promoted)
        + "inline const uint32_t *sample_circuit_scaled_words(bool promoted) {\n"
        "\treturn promoted ? kSampleCircuitScaledPromotedWords : kSampleCircuitScaledWords;\n"
        "}\n"
        "inline std::size_t sample_circuit_scaled_bytes(bool promoted) {\n"
        "\treturn promoted ? sizeof(kSampleCircuitScaledPromotedWords) : sizeof(kSampleCircuitScaledWords);\n"
        "}\n"
        "} // namespace gt4_parallel_gs_scale\n"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vendor-gs", required=True)
    parser.add_argument("--spv-dir", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    outputs = {
        "gs_interface.hpp": patch_interface_header(read_text(os.path.join(args.vendor_gs, "gs_interface.hpp"))),
        "gs_renderer.hpp": patch_renderer_header(read_text(os.path.join(args.vendor_gs, "gs_renderer.hpp"))),
        "gs_renderer.cpp": patch_renderer_source(read_text(os.path.join(args.vendor_gs, "gs_renderer.cpp"))),
        "parallel_gs_scale_spirv.inc": spirv_include(args.spv_dir),
    }
    for name, text in outputs.items():
        target = os.path.join(args.out, name)
        temp = target + ".new"
        with open(temp, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        os.replace(temp, target)


if __name__ == "__main__":
    main()
