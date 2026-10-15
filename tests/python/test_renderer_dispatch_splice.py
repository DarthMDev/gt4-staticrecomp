"""Renderer dispatch splice (TASK-027 M0b, spec chapter 12.2): donor, paraLLEl-GS and HWR share one front end.

Offline: uses the synthetic donor sources of test_prepare_external_vulkan, so no donor checkout is needed.
"""

import re
import unittest

from test_prepare_external_vulkan import donor_text, prepare_external_vulkan as prepare

DISPATCH_NAMES = re.compile(
    r'\bparallel_(active|reset_gif|reset|gif_transfer|gif_busy|read_image|wait|vblank|present|attach_window|'
    r'resize_window|mirror_privileged|sync_memory|dump_scanout|scanout_frame)\b')


class RendererDispatchSpliceTests(unittest.TestCase):
    def test_every_renamed_anchor_occurs_exactly_once(self):
        for name in ('gs.cpp', 'gif.cpp', 'hw.cpp'):
            source = donor_text(name)
            for anchor, _ in prepare.RENDERER_HOOKS[name]:
                self.assertEqual(source.count(anchor), 1, (name, anchor[:40]))

    def test_hooks_call_only_the_neutral_entry_points(self):
        for name, pairs in prepare.RENDERER_HOOKS.items():
            for _, replacement in pairs:
                self.assertIsNone(DISPATCH_NAMES.search(replacement), (name, replacement[:80]))

    def test_attach_hook_passes_the_window_kind(self):
        spliced = prepare.apply_hooks('gs.cpp', donor_text('gs.cpp'), 'BLOCK\n', prepare.RENDERER_HOOKS)
        self.assertIn('return kind == 3 && alt_attach_window(kind, window, width, height);', spliced)
        self.assertNotIn('alt_attach_window(window, width, height)', spliced)

    def test_every_hook_is_guarded_by_alt_active(self):
        spliced = prepare.apply_hooks('gs.cpp', donor_text('gs.cpp'), 'BLOCK\n', prepare.RENDERER_HOOKS)
        self.assertIn('if (alt_active()) return;\n    const char *backend', spliced)
        self.assertIn('if (alt_active()) alt_mirror_privileged(address, value);', spliced)
        self.assertIn('if (alt_active() && alt_dump_scanout()) return alt_scanout_frame(rgba, width, height);', spliced)
        self.assertIn('if (alt_active()) return alt_present();', spliced)

    def test_gif_hooks_route_through_the_dispatch(self):
        gif = prepare.apply_hooks('gif.cpp', donor_text('gif.cpp'), hooks=prepare.RENDERER_HOOKS)
        self.assertIn('gs::alt_gif_transfer(path, data, qwc);', gif)
        self.assertIn('return gs::alt_gif_busy(path);', gif)
        self.assertIn('bool alt_active();', gif)

    def test_hw_includes_reach_the_hooked_gs_source(self):
        spliced = prepare.apply_hooks('gs.cpp', donor_text('gs.cpp'), 'BLOCK\n', prepare.RENDERER_HOOKS)
        self.assertIn('#include "hw_gs/hw_gs_backend.hpp"', spliced)
        self.assertIn('#include "hw_gs/hw_gs_options.hpp"', spliced)
        self.assertIn('#include "parallel_gs_backend.hpp"', spliced)

    def test_refusal_of_hw_is_compiled_out_when_the_front_end_is_absent(self):
        # renderer_dispatch.inc keeps GT4_RENDERER=hw behind GT4_RENDERER_HW_SPLICED, so a port without the
        # HWR front end refuses hw instead of silently running the donor path.
        dispatch = (prepare.ROOT / 'native' / 'renderer_dispatch.inc').read_text(encoding='utf-8')
        self.assertIn('#ifdef GT4_RENDERER_HW_SPLICED', dispatch)
        self.assertIn('this port carries no HWR front end', dispatch)

    def test_frontend_block_names_all_three_renderers(self):
        dispatch = (prepare.ROOT / 'native' / 'renderer_dispatch.inc').read_text(encoding='utf-8')
        for word in ('donor', 'parallel', 'hw'):
            self.assertIn(f'"{word}"', dispatch)
        routers = (prepare.ROOT / 'native' / 'renderer_routers.inc').read_text(encoding='utf-8')
        for entry in ('alt_reset', 'alt_gif_transfer', 'alt_present', 'alt_attach_window', 'alt_scanout_frame'):
            self.assertIn(f'{entry}(', routers)


if __name__ == '__main__':
    unittest.main()
