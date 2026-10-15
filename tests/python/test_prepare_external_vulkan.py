"""Offline tests for scripts/prepare_external_vulkan.py (slice 163 paraLLEl-GS hook option).

The synthetic tests need no donor checkout. The baseline test compares the default derivation
with the qualified slice153 port and skips when the private inputs are absent.
"""

import hashlib
import importlib.util
import shutil
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / 'scripts' / 'prepare_external_vulkan.py'
MAIN_CHECKOUT = Path('C:/Antigravity/gt4-staticrecomp')

_spec = importlib.util.spec_from_file_location('prepare_external_vulkan', SCRIPT)
prepare_external_vulkan = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(prepare_external_vulkan)


def donor_text(name):
    """Minimal donor sources containing every anchor, so the splice logic runs without the donor."""
    anchors = {
        'gs.cpp': [
            '#include "gif.hpp"\n',
            'void init() {\n',
            '#ifdef PS2_GPU\n    const char *backend = std::getenv("PS2_GS");',
            'void render_reset() {\n#ifdef PS2_GPU\n',
            'void write_privileged(std::uint32_t address, std::uint64_t value) {\n    switch',
            'void read_image(std::uint8_t *data, std::uint32_t qwords) {\n    if (!queue::active()) {',
            '        g_odd_field = !g_odd_field;\n        raise(8);\n',
            'bool display(const DisplayRegs &r, std::vector<std::uint32_t> &rgba, std::uint32_t &width, std::uint32_t &height) {\n',
            'bool present(const DisplayRegs &r) {\n',
            'bool attach_window(int kind, void *display, std::uint64_t window, void *surface, const char *selector, std::uint32_t width, std::uint32_t height) {\n',
            'void resize_window(std::uint32_t width, std::uint32_t height) {\n',
            'void flush_and_wait() {\n',
            'void start_thread() { queue::render().start(32u << 20); }',
            'void state(ps2::state::Stream &s) {\n    s.tag("gs");',
        ],
        'gif.cpp': [
            'namespace ps2::gif {\n\nnamespace {',
            'void transfer(int path, const std::uint8_t *data, std::uint32_t qwc) {\n',
            'void reset() {\n    g_stat = 0;\n',
            'bool busy(int path) { return g_paths[path].active; }',
        ],
        'hw.cpp': [
            'namespace ps2::hw {\n\nnamespace {',
            '    if (!g_options.rate_forced) g_options.pal = mode == 3u;\n',
        ],
    }
    return '\n'.join(anchors[name]) + '\n'


class HookSpliceTests(unittest.TestCase):
    def test_every_anchor_occurs_exactly_once_in_synthetic_sources(self):
        for name in ('gs.cpp', 'gif.cpp', 'hw.cpp'):
            source = donor_text(name)
            for anchor, _ in prepare_external_vulkan.PARALLEL_HOOKS[name]:
                self.assertEqual(source.count(anchor), 1, (name, anchor[:40]))

    def test_frontend_block_is_inserted_before_init_and_guards_donor_path(self):
        spliced = prepare_external_vulkan.apply_hooks('gs.cpp', donor_text('gs.cpp'), 'BLOCK_MARKER\n')
        self.assertIn('BLOCK_MARKER\n\nvoid init() {', spliced)
        self.assertIn('if (parallel_active()) return;\n    const char *backend', spliced)
        self.assertIn('if (parallel_active()) parallel_mirror_privileged(address, value);', spliced)
        self.assertIn('if (parallel_active()) return;\n    queue::render().start', spliced)
        self.assertNotIn('@FRONTEND@', spliced)

    def test_window_hooks_route_to_the_parallel_presenter(self):
        # Slice 168 (step 6): the window, resize, present and scanout-dump paths reach the front end.
        spliced = prepare_external_vulkan.apply_hooks('gs.cpp', donor_text('gs.cpp'), 'BLOCK_MARKER\n')
        self.assertIn('if (parallel_active()) return kind == 3 && parallel_attach_window(window, width, height);', spliced)
        self.assertIn('if (parallel_active()) {\n        parallel_resize_window(width, height);\n        return;\n    }', spliced)
        self.assertIn('if (parallel_active()) return parallel_present();', spliced)
        self.assertIn('if (parallel_active() && parallel_dump_scanout()) return parallel_scanout_frame(rgba, width, height);',
                      spliced)
        self.assertIn('#include "pacing.hpp"\n', spliced)

    def test_gif_and_hw_hooks_route_to_the_front_end(self):
        gif = prepare_external_vulkan.apply_hooks('gif.cpp', donor_text('gif.cpp'))
        self.assertIn('gs::parallel_gif_transfer(path, data, qwc);', gif)
        self.assertIn('return gs::parallel_gif_busy(path);', gif)
        hw = prepare_external_vulkan.apply_hooks('hw.cpp', donor_text('hw.cpp'))
        self.assertIn('    gs::note_crt_mode(interlace, mode, field);\n', hw)

    def test_changed_anchor_is_refused(self):
        broken = donor_text('gs.cpp').replace('void flush_and_wait() {\n', 'void flush_and_wait_renamed() {\n')
        with self.assertRaisesRegex(ValueError, 'Anchor changed in donor gs.cpp'):
            prepare_external_vulkan.apply_hooks('gs.cpp', broken, '')

    def test_duplicate_anchor_is_refused(self):
        duplicated = donor_text('hw.cpp') + donor_text('hw.cpp')
        with self.assertRaisesRegex(ValueError, 'Anchor changed'):
            prepare_external_vulkan.apply_hooks('hw.cpp', duplicated, '')

    def test_frontend_include_is_present_in_the_hooked_port(self):
        spliced = prepare_external_vulkan.apply_hooks('gs.cpp', donor_text('gs.cpp'), '')
        self.assertIn('#include "parallel_gs_backend.hpp"', spliced)
        self.assertIn('#include "gs_video_mode.hpp"', spliced)


class OutputRootTests(unittest.TestCase):
    def test_output_outside_private_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'inside private'):
            prepare_external_vulkan.prepare(Path(tempfile.gettempdir()) / 'not-private-slice163')


class BaselineDerivationTests(unittest.TestCase):
    """The default derivation must stay byte-identical to the qualified slice153 port."""

    def setUp(self):
        if not (MAIN_CHECKOUT / 'private/references/ps2-web-recomp/runtime/src/gs/gs.cpp').exists():
            self.skipTest('private donor checkout is not present')
        if not (MAIN_CHECKOUT / 'private/slice153-pacing-port/gs.cpp').exists():
            self.skipTest('qualified slice153 port is not present')

    def test_default_derivation_matches_slice153_port(self):
        target = REPO / 'private' / f'slice163-tests-{Path(tempfile.mktemp()).name}'
        try:
            prepare_external_vulkan.prepare(target, MAIN_CHECKOUT, parallel=False)
            self.assertFalse((target / 'gif.cpp').exists())
            self.assertFalse((target / 'hw.cpp').exists())
            for name in ('gs.cpp', 'gpu.cpp', 'gpu.hpp', 'main.cpp'):
                expected = (MAIN_CHECKOUT / 'private/slice153-pacing-port' / name).read_bytes()
                self.assertEqual(hashlib.sha256((target / name).read_bytes()).hexdigest(),
                                 hashlib.sha256(expected).hexdigest(), name)
        finally:
            shutil.rmtree(target, ignore_errors=True)

    def test_parallel_derivation_writes_hooked_gif_and_hw(self):
        target = REPO / 'private' / f'slice163-tests-{Path(tempfile.mktemp()).name}'
        try:
            prepare_external_vulkan.prepare(target, MAIN_CHECKOUT, parallel=True)
            gs_text = (target / 'gs.cpp').read_text(encoding='utf-8')
            self.assertIn('void note_crt_mode(', gs_text)
            self.assertIn('if (parallel_active()) return;', gs_text)
            # Slice 167: the display decode of the copied VRAM must build the swizzle tables first.
            self.assertIn('    init(); // swizzle tables for read_pixel', gs_text)
            self.assertIn('gs::parallel_gif_transfer(path, data, qwc);', (target / 'gif.cpp').read_text(encoding='utf-8'))
            self.assertIn('gs::note_crt_mode(interlace, mode, field);', (target / 'hw.cpp').read_text(encoding='utf-8'))
        finally:
            shutil.rmtree(target, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
