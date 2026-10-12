"""Derive a private direct-Vulkan GPU adapter from the frozen MIT donor."""

import argparse
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GPU_SHA256 = 'bfd1b54921d3f4d6c8496433739ea97be6d675d3d7941e5ccb2f35e99e833465'
SOURCE_PINS = {
    'runtime/src/gs/gpu/gpu.cpp': GPU_SHA256,
    'runtime/src/gs/gpu/gpu.hpp': 'e99ea5d37565f55a3737dfcd76f79c700e789986803b7384eb0c5c666106f944',
    'runtime/src/gs/gs.cpp': '0a9fbfc53b4dffeaa43ae05c6e7f7f5d1fe8b5aa987d1a4ba94405177a708264',
    'host/main.cpp': '561d977743b0da7af528b86da4d5c5eb744e1b8ac9072487485f7ed3b790f007',
    'runtime/src/gs/gpu/gs.wgsl': 'a6ed68e9ca53ea9b6c90307d3ffeb2b74b15c5f0a20dd776f51bc243ef80d4a5',
}


def replace_section(source, start, end, replacement):
    first = source.index(start)
    last = source.index(end, first)
    return source[:first] + replacement + '\n\n' + source[last:]


def prepare(output):
    output = output.resolve()
    if not output.is_relative_to(ROOT / 'private') or output == ROOT / 'private':
        raise ValueError('Derived donor code must stay inside private/')
    donor = ROOT / 'private/references/ps2-web-recomp/runtime/src/gs/gpu'
    donor_root = donor.parents[3]
    if output.is_relative_to(donor_root) or donor_root.is_relative_to(output):
        raise ValueError('Derived output cannot replace the frozen donor checkout')
    for relative, expected in SOURCE_PINS.items():
        if hashlib.sha256((donor_root / relative).read_bytes()).hexdigest() != expected:
            raise ValueError(f'Donor source differs from qualified version: {relative}')
    gpu_bytes = (donor / 'gpu.cpp').read_bytes()
    if hashlib.sha256(gpu_bytes).hexdigest() != GPU_SHA256:
        raise ValueError('Donor GPU source differs from the qualified version')
    source = gpu_bytes.decode('utf-8').replace('\r\n', '\n')
    source = replace_section(source, '#include <webgpu/', '#include <algorithm>',
                             '#include "native_vulkan.hpp"\n#include <stdexcept>')
    source = source.replace('extern const char kGsShader[];\nextern const char kBlitShader[];\n', '')
    globals_text = '''gt4::vulkan::Context g_vulkan;
gt4::vulkan::Buffer *g_vram = nullptr, *g_tables = nullptr, *g_params = nullptr;
gt4::vulkan::Buffer *g_data = nullptr, *g_clut = nullptr, *g_scratch = nullptr, *g_decoded = nullptr;
std::uint32_t g_scale = 1, g_copy_words = 1u << 20, g_field_words = 1u << 19;
bool g_rendered[512] = {};
bool g_decode = true;
std::uint32_t g_decoded_used = 0;
std::uint64_t g_params_size = 0, g_data_size = 0;'''
    source = replace_section(source, 'WGPUInstance g_instance', 'struct Command {', globals_text)
    source = replace_section(source, 'struct CommandInfo {', 'std::vector<Command> g_commands;', '')
    source = source.replace('WGPUStringView view(const char *s) { return WGPUStringView{s, WGPU_STRLEN}; }\n', '')
    source = source.replace('WGPUBuffer g_cache = nullptr;', 'gt4::vulkan::Buffer* g_cache = nullptr;')
    source = source.replace('wgpuQueueWriteBuffer(g_queue, g_cache,', 'g_vulkan.write(g_cache,')
    source = replace_section(source, 'void note_raster(', 'void flush_batch()', '')
    source = source.replace('    note_raster(prims, tiles, total, snapshot);\n', '')
    source = source.replace('const std::uint32_t snapshot = snapshot_batch(prims, snapshot_lo, snapshot_hi);',
                            'snapshot_batch(prims, snapshot_lo, snapshot_hi);')
    replacements = ROOT / 'experimental/vulkan'
    source = replace_section(source, 'void on_error(', 'std::uint32_t make_setup(',
                             (replacements / 'gpu_helpers.inc').read_text())
    source = replace_section(source, 'bool init(Mode m)', 'void draw(',
                             (replacements / 'gpu_init.inc').read_text())
    source = source[:source.index('void flush() {')] + (replacements / 'gpu_tail.inc').read_text() + '\n}\n'
    if 'wgpu' in source.lower() or 'WGPU' in source:
        raise ValueError('Unexpected WebGPU reference remains after the native port')
    source = '// MIT donor batching, derived from pinned ps2-web-recomp gpu.cpp; see THIRD_PARTY.md.\n' + source
    header = (donor / 'gpu.hpp').read_text().replace('../gs.hpp', 'gs.hpp')
    header = header.replace('enum Kind { X11, Wayland, Canvas }', 'enum Kind { X11, Wayland, Canvas, Win32 }')
    output.mkdir(parents=True, exist_ok=True)
    (output / 'gpu.cpp').write_text(source, encoding='utf-8')
    (output / 'gpu.hpp').write_text(header, encoding='utf-8')
    graphics = donor.parent / 'gs.cpp'
    graphics_source = graphics.read_text(encoding='utf-8')
    graphics_source = graphics_source.replace('"../hw.hpp"', '"hw.hpp"')
    graphics_source = graphics_source.replace('"gpu/gpu.hpp"', '"gpu.hpp"')
    (output / 'gs.cpp').write_text(graphics_source, encoding='utf-8')
    host = donor.parents[3] / 'host/main.cpp'
    host_source = host.read_text(encoding='utf-8')
    # Preserve the donor's CLI while refusing a silent CPU raster fallback.
    host_source = host_source.replace('    register_generated_code();',
        '    _putenv_s("PS2_GS", "gpu");\n    register_generated_code();')
    (output / 'main.cpp').write_text(host_source, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    prepare(parser.parse_args().output)
