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

# Slice 163 paraLLEl-GS hooks: (anchor, replacement) per donor file. Each anchor must occur exactly
# once. The gs.cpp replacement may contain @FRONTEND@, which becomes native/parallel_gs_frontend.inc.
PARALLEL_HOOKS = {
    'gs.cpp': [
        ('#include "gif.hpp"\n',
         '#include "gif.hpp"\n#include "gs_video_mode.hpp"\n#include "pacing.hpp"\n#include "parallel_gs_backend.hpp"\n'),
        ('void init() {\n', '@FRONTEND@\nvoid init() {\n'),
        ('#ifdef PS2_GPU\n    const char *backend = std::getenv("PS2_GS");',
         '#ifdef PS2_GPU\n    if (parallel_active()) return;\n    const char *backend = std::getenv("PS2_GS");'),
        ('void render_reset() {\n#ifdef PS2_GPU\n',
         'void render_reset() {\n    if (parallel_active()) {\n        parallel_reset();\n        return;\n    }\n#ifdef PS2_GPU\n'),
        ('void write_privileged(std::uint32_t address, std::uint64_t value) {\n    switch',
         'void write_privileged(std::uint32_t address, std::uint64_t value) {\n'
         '    if (parallel_active()) parallel_mirror_privileged(address, value);\n    switch'),
        ('void read_image(std::uint8_t *data, std::uint32_t qwords) {\n    if (!queue::active()) {',
         'void read_image(std::uint8_t *data, std::uint32_t qwords) {\n    if (parallel_active()) {\n'
         '        parallel_read_image(data, qwords);\n        return;\n    }\n    if (!queue::active()) {'),
        ('        g_odd_field = !g_odd_field;\n        raise(8);\n',
         '        g_odd_field = !g_odd_field;\n        raise(8);\n        if (parallel_active()) parallel_vblank();\n'),
        ('bool display(const DisplayRegs &r, std::vector<std::uint32_t> &rgba, std::uint32_t &width, std::uint32_t &height) {\n',
         'bool display(const DisplayRegs &r, std::vector<std::uint32_t> &rgba, std::uint32_t &width, std::uint32_t &height) {\n'
         '    if (parallel_active() && parallel_dump_scanout()) return parallel_scanout_frame(rgba, width, height);\n'
         '    if (parallel_active()) parallel_sync_memory();\n'),
        ('bool present(const DisplayRegs &r) {\n',
         'bool present(const DisplayRegs &r) {\n'
         '    if (parallel_active()) return parallel_present(); // slice 168: Granite WSI window output\n'),
        ('bool attach_window(int kind, void *display, std::uint64_t window, void *surface, const char *selector, std::uint32_t width, std::uint32_t height) {\n',
         'bool attach_window(int kind, void *display, std::uint64_t window, void *surface, const char *selector, std::uint32_t width, std::uint32_t height) {\n'
         '    if (parallel_active()) return kind == 3 && parallel_attach_window(window, width, height); // 3 = Win32\n'),
        ('void resize_window(std::uint32_t width, std::uint32_t height) {\n',
         'void resize_window(std::uint32_t width, std::uint32_t height) {\n'
         '    if (parallel_active()) {\n        parallel_resize_window(width, height);\n        return;\n    }\n'),
        ('void flush_and_wait() {\n',
         'void flush_and_wait() {\n    if (parallel_active()) {\n        parallel_wait();\n        return;\n    }\n'),
        ('void start_thread() { queue::render().start(32u << 20); }',
         'void start_thread() {\n    if (parallel_active()) return;\n    queue::render().start(32u << 20);\n}'),
        ('void state(ps2::state::Stream &s) {\n    s.tag("gs");',
         'void state(ps2::state::Stream &s) {\n'
         '    if (parallel_active()) throw std::runtime_error("save states are not supported with GT4_RENDERER=parallel");\n'
         '    s.tag("gs");'),
    ],
    'gif.cpp': [
        ('namespace ps2::gif {\n\nnamespace {',
         'namespace ps2::gs {\nbool parallel_active();\n'
         'void parallel_gif_transfer(int path, const std::uint8_t *data, std::uint32_t qwc);\n'
         'void parallel_reset_gif();\nbool parallel_gif_busy(int path);\n}\n\nnamespace ps2::gif {\n\nnamespace {'),
        ('void transfer(int path, const std::uint8_t *data, std::uint32_t qwc) {\n',
         'void transfer(int path, const std::uint8_t *data, std::uint32_t qwc) {\n'
         '    if (gs::parallel_active()) {\n        gs::parallel_gif_transfer(path, data, qwc);\n        return;\n    }\n'),
        ('void reset() {\n    g_stat = 0;\n',
         'void reset() {\n    g_stat = 0;\n    if (gs::parallel_active()) gs::parallel_reset_gif();\n'),
        ('bool busy(int path) { return g_paths[path].active; }',
         'bool busy(int path) {\n    if (gs::parallel_active()) return gs::parallel_gif_busy(path);\n'
         '    return g_paths[path].active;\n}'),
    ],
    'hw.cpp': [
        ('namespace ps2::hw {\n\nnamespace {',
         'namespace ps2::gs {\nvoid note_crt_mode(std::uint32_t interlace, std::uint32_t mode, std::uint32_t field);\n}\n\n'
         'namespace ps2::hw {\n\nnamespace {'),
        ('    if (!g_options.rate_forced) g_options.pal = mode == 3u;\n',
         '    gs::note_crt_mode(interlace, mode, field);\n    if (!g_options.rate_forced) g_options.pal = mode == 3u;\n'),
    ],
}


def replace_section(source, start, end, replacement):
    first = source.index(start)
    last = source.index(end, first)
    return source[:first] + replacement + '\n\n' + source[last:]


def apply_hooks(name, source, frontend=''):
    for anchor, replacement in PARALLEL_HOOKS[name]:
        if source.count(anchor) != 1:
            raise ValueError(f'Anchor changed in donor {name}: {anchor[:50]!r}')
        source = source.replace(anchor, replacement.replace('@FRONTEND@', frontend))
    return source


def prepare(output, assets_root=None, parallel=False):
    output = output.resolve()
    assets_root = Path(assets_root or ROOT).resolve()
    allowed_roots = [ROOT / 'private', assets_root / 'private']
    if not any(output.is_relative_to(root) and output != root for root in allowed_roots):
        raise ValueError('Derived donor code must stay inside private/')
    donor = assets_root / 'private/references/ps2-web-recomp/runtime/src/gs/gpu'
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
gt4::vulkan::Buffer *g_vram = nullptr, *g_tables = nullptr;
gt4::vulkan::Buffer *g_clut = nullptr, *g_scratch = nullptr, *g_decoded = nullptr;
std::uint32_t g_scale = 1, g_copy_words = 2048u * 2048u, g_field_words = 1u << 19;
bool g_rendered[512] = {};
bool g_decode = true;
std::uint32_t g_decoded_used = 0;'''
    source = replace_section(source, 'WGPUInstance g_instance', 'struct Command {', globals_text)
    source = replace_section(source, 'struct CommandInfo {', 'std::vector<Command> g_commands;', '')
    source = source.replace('WGPUStringView view(const char *s) { return WGPUStringView{s, WGPU_STRLEN}; }\n', '')
    source = source.replace('WGPUBuffer g_cache = nullptr;', 'gt4::vulkan::Buffer* g_cache = nullptr;')
    source = source.replace('wgpuQueueWriteBuffer(g_queue, g_cache,', 'g_vulkan.write(g_cache,')
    # Asynchronous submission: an entry may be overwritten only after the submission that last used it completed.
    guard_old = 'if (front.used == g_serial) return false;'
    guard_new = 'if (front.used > g_vulkan.completed_submission()) return false;'
    if source.count(guard_old) != 1:
        raise ValueError('Cache eviction guard anchor changed in the donor source')
    source = source.replace(guard_old, guard_new)
    source = replace_section(source, 'void note_raster(', 'void flush_batch()', '')
    source = source.replace('    note_raster(prims, tiles, total, snapshot);\n', '')
    source = source.replace('const std::uint32_t snapshot = snapshot_batch(prims, snapshot_lo, snapshot_hi);',
                            'snapshot_batch(prims, snapshot_lo, snapshot_hi);')
    replacements = ROOT / 'native'
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
    header = header.replace('bool has_surface();', 'bool has_surface();\nvoid set_widescreen(bool enabled);')
    output.mkdir(parents=True, exist_ok=True)
    (output / 'gpu.cpp').write_text(source, encoding='utf-8')
    (output / 'gpu.hpp').write_text(header, encoding='utf-8')
    graphics = donor.parent / 'gs.cpp'
    graphics_source = graphics.read_text(encoding='utf-8')
    graphics_source = graphics_source.replace('"../hw.hpp"', '"hw.hpp"')
    graphics_source = graphics_source.replace('"gpu/gpu.hpp"', '"gpu.hpp"')
    if parallel:
        frontend = (ROOT / 'native' / 'parallel_gs_frontend.inc').read_text(encoding='utf-8')
        graphics_source = apply_hooks('gs.cpp', graphics_source, frontend)
        donor_gif = (donor.parent / 'gif.cpp').read_text(encoding='utf-8')
        donor_hw = (donor.parents[1] / 'hw.cpp').read_text(encoding='utf-8')
        (output / 'gif.cpp').write_text(apply_hooks('gif.cpp', donor_gif), encoding='utf-8')
        (output / 'hw.cpp').write_text(apply_hooks('hw.cpp', donor_hw), encoding='utf-8')
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
    parser.add_argument('--assets-root', type=Path)
    parser.add_argument('--parallel-hooks', action='store_true',
                        help='Splice the slice 163 paraLLEl-GS front end (also writes gif.cpp and hw.cpp)')
    options = parser.parse_args()
    prepare(options.output, options.assets_root, options.parallel_hooks)
