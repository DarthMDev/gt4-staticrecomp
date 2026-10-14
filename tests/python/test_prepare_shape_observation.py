"""Pin and exact-overlay controls use literal synthetic donor-shaped inputs."""
import hashlib
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import prepare_shape_observation as preparation


class RuntimeOverlayTests(unittest.TestCase):
    def test_exact_single_anchor_required(self):
        self.assertEqual(preparation.replace_once('first target last', 'target', 'hook target'),
                         'first hook target last')
        for value in ('absent', 'target target'):
            with self.assertRaisesRegex(ValueError, 'anchor'):
                preparation.replace_once(value, 'target', 'changed')

    def test_unknown_or_unpinned_sources_rejected_before_patching(self):
        for name, data in (('unknown.cpp', b''), ('kernel.cpp', b'changed'),
                           ('dispatch.cpp', 'notbytes'), ('system.cpp', bytearray())):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'pinned'):
                preparation.overlay_source(name, data)

    def test_dispatch_unknown_diagnostic_body_preserved(self):
        source = b'void probe(Cpu &c, std::uint32_t pc) {\n    original_diagnostic();\n}\n'
        with patch.dict(preparation.SOURCE_PINS, {'dispatch.cpp': hashlib.sha256(source).hexdigest()}):
            result = preparation.overlay_source('dispatch.cpp', source)
        self.assertEqual(result, b'#include "shape_observer_adapter.hpp"\n'
                         b'void probe(Cpu &c, std::uint32_t pc) {\n'
                         b'    if (gt4::observation::observe_probe(c, pc)) return;\n'
                         b'    original_diagnostic();\n}\n')

    def test_load_and_boot_invalidate_before_original_work(self):
        source = (b'bool load_state(const std::uint8_t *data, std::size_t size) {\n reader(); }\n'
                  b'bool load_state(const std::string &path) {\n open_file(); }\n'
                  b'void boot(std::uint32_t entry, const std::vector<std::string> &args) {\n reset_hw(); }\n')
        with patch.dict(preparation.SOURCE_PINS, {'system.cpp': hashlib.sha256(source).hexdigest()}):
            result = preparation.overlay_source('system.cpp', source)
        self.assertEqual(result.count(b'gt4::observation::reset_runtime_epoch();'), 3)
        for operation in (b' reader()', b' open_file()', b' reset_hw()'):
            self.assertIn(b'gt4::observation::reset_runtime_epoch();\n' + operation, result)

    def test_kernel_lifecycle_anchors_are_all_required_and_bounded(self):
        source = ('    g_threads[t.id] = t;\n    if (g_options.trace_threads) {}\n'
                  'void reset_thread(Thread &t, std::uint32_t argument) {\n context_reset(); }\n'
                  '    if (!t) return;\n    t->status = kDormant;\n'
                  '    case 0x21: g_threads.erase(static_cast<int>(arg(c, 0))); ret(c, 0); break;\n'
                  '        if (t) {\n            for (auto &[id, s] : g_semas) {} }\n'
                  'void reset() {\n clear(); }\n'
                  'void start_main_thread(std::uint32_t entry, std::uint32_t gp, std::uint32_t argc, std::uint32_t argv) {\n start(); }\n').encode()
        with patch.dict(preparation.SOURCE_PINS, {'kernel.cpp': hashlib.sha256(source).hexdigest()}):
            result = preparation.overlay_source('kernel.cpp', source)
        self.assertEqual(result.count(b'gt4::observation::invalidate_runtime_owner('), 6)
        self.assertEqual(result.count(b'gt4::observation::reset_runtime_epoch();'), 1)
        missing = source.replace(b'void reset() {', b'void other_reset() {')
        with patch.dict(preparation.SOURCE_PINS, {'kernel.cpp': hashlib.sha256(missing).hexdigest()}):
            with self.assertRaisesRegex(ValueError, 'anchor'):
                preparation.overlay_source('kernel.cpp', missing)


if __name__ == '__main__':
    unittest.main()
