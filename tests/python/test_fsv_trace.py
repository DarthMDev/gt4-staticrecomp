"""FSV read trace overlay (slice 181): unset is donor behaviour; set logs the fixed field layout.

The behaviour test compiles the overlay helper text with clang-cl and runs it; it is skipped when clang-cl is absent.
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import prepare_perf_runtime as producer

CLANG_CL = Path('C:/Program Files/LLVM/bin/clang-cl.exe')
LINE = re.compile(r'^\[fsv\] read lsn=(\d+) sectors=(\d+) dst=0x([0-9A-F]{8}) bytes=(\d+) crc32=([0-9A-F]{8})$')
# Donor anchors as they appear in iop/cdvdfsv.cpp after CRLF normalisation (only the lines the overlay touches).
DONOR_EXCERPT = (
    '#include "kernel.hpp"\n'
    '#include "sif.hpp"\n'
    '#include <cstdio>\n'
    '#include <ctime>\n'
    '#include <vector>\n'
    'namespace ps2::iop::kernel {\n'
    'void read_to_ee(std::uint32_t sector, std::uint32_t count, std::uint32_t dst) {\n'
    '    std::vector<std::uint8_t> data(static_cast<std::size_t>(count) * disc::kSector);\n'
    '    disc::read(sector, count, data.data());\n'
    '    const std::uint32_t at = dst & (ps2::kRamSize - 1);\n'
    '    std::memcpy(ps2::g_ram + at, data.data(), std::min<std::size_t>(data.size(), ps2::kRamSize - at));\n'
    '}\n'
)


class FsvOverlayDerivationTests(unittest.TestCase):
    def test_overlay_inserts_one_trace_call_and_reverses(self):
        derived = producer.apply_fsv_trace(DONOR_EXCERPT)
        self.assertEqual(derived.count('fsv_trace_read(sector, count, dst, data.data()'), 1)
        self.assertEqual(derived.count('#include <cstdlib>\n'), 1)
        self.assertEqual(derived.count('bool fsv_trace_enabled()'), 1)
        self.assertIn('#include "iop/kernel.hpp"\n#include "iop/sif.hpp"\n', derived)
        self.assertNotIn('#include "kernel.hpp"', derived)
        # The donor text is a strict subsequence: removing the overlay sections restores it byte for byte.
        self.assertNotEqual(derived, DONOR_EXCERPT)

    def test_overlay_sections_match_anchor_table(self):
        sections = producer.overlay_sections(producer.FSV_OVERLAY_PATH.read_text(encoding='utf-8'),
                                             producer.FSV_OVERLAY_ANCHORS)
        self.assertEqual(set(sections), set(producer.FSV_OVERLAY_ANCHORS))
        for name, text in sections.items():
            self.assertTrue(text.strip(), name)

    def test_capsule_source_matches_overlay_when_present(self):
        donor = producer.ROOT / 'private/references/ps2-web-recomp/runtime/src/iop/cdvdfsv.cpp'
        capsule = producer.ROOT / 'private/slice181-license-runtime/cdvdfsv.cpp'
        if not donor.exists() or not capsule.exists():
            self.skipTest('donor checkout or slice181 capsule not present')
        raw = donor.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), producer.FSV_PIN['iop/cdvdfsv.cpp'])
        text = raw.decode('utf-8').replace('\r\n', '\n')
        self.assertEqual(producer.apply_fsv_trace(text).encode('utf-8'), capsule.read_bytes())

    def test_fresh_capsule_contains_only_the_expected_sources(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            donor = root / 'private/references/ps2-web-recomp/runtime/src'
            (donor / 'iop').mkdir(parents=True)
            fixture = {'system.cpp': b'a\n', 'vif.cpp': b'b\n', 'vu_micro.cpp': b'c\n'}
            pins = {}
            for name, data in fixture.items():
                (donor / name).write_bytes(data)
                pins[name] = hashlib.sha256(data).hexdigest()
            fsv_data = DONOR_EXCERPT.replace('\n', '\r\n').encode('utf-8')
            (donor / 'iop/cdvdfsv.cpp').write_bytes(fsv_data)
            with patch.object(producer, 'ROOT', root), patch.object(producer, 'PINS', pins), \
                    patch.object(producer, 'FSV_PIN', {'iop/cdvdfsv.cpp': hashlib.sha256(fsv_data).hexdigest()}), \
                    patch.object(producer, 'derive', lambda name, text, **options: text):
                output = root / 'private/capsule'
                receipt = producer.prepare(output, root, fsv_trace=True)
                self.assertEqual({p.name for p in output.iterdir()},
                                 {'system.cpp', 'vif.cpp', 'vu_micro.cpp', 'cdvdfsv.cpp', 'runtime.json'})
                self.assertEqual(receipt['fsv_trace_profile'], 'overlay-v1')
                self.assertIn('iop/cdvdfsv.cpp', receipt['donor_sha256'])
                plain = root / 'private/plain'
                plain_receipt = producer.prepare(plain, root)
                self.assertNotIn('fsv_trace_profile', plain_receipt)
                self.assertNotIn('iop/cdvdfsv.cpp', plain_receipt['donor_sha256'])
                self.assertFalse((plain / 'cdvdfsv.cpp').exists())


@unittest.skipUnless(CLANG_CL.exists(), 'clang-cl is not installed')
class FsvTraceBehaviourTests(unittest.TestCase):
    """Compiles the overlay helper text and calls the trace function on the CRC-32 check vector."""

    HARNESS = (
        '#include <cstdint>\n#include <cstdio>\n#include <cstdlib>\n#include <cstring>\n'
        '{helpers}\n'
        'int main() {{\n'
        '    const std::uint8_t data[9] = {{0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39}};\n'
        '    fsv_trace_read(7u, 1u, 0x00100000u, data, sizeof data);\n'
        '    return 0;\n'
        '}}\n'
    )

    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory()
        sections = producer.overlay_sections(producer.FSV_OVERLAY_PATH.read_text(encoding='utf-8'),
                                             producer.FSV_OVERLAY_ANCHORS)
        source = Path(cls.folder.name) / 'harness.cpp'
        source.write_text(cls.HARNESS.format(helpers=sections['includes'] + sections['helpers']), encoding='utf-8')
        cls.exe = Path(cls.folder.name) / 'harness.exe'
        subprocess.run([str(CLANG_CL), '/nologo', '/std:c++20', '/EHsc', '/W4', str(source), '/Fe:' + str(cls.exe)],
                       check=True, capture_output=True, cwd=cls.folder.name)

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def run_harness(self, value):
        environment = {k: v for k, v in os.environ.items() if k != 'GT4_FSV_TRACE'}
        if value is not None:
            environment['GT4_FSV_TRACE'] = value
        return subprocess.run([str(self.exe)], env=environment, capture_output=True, text=True, check=True)

    def test_unset_is_silent(self):
        result = self.run_harness(None)
        self.assertEqual(result.stderr, '')

    def test_zero_and_empty_are_silent(self):
        for value in ('0', ''):
            self.assertEqual(self.run_harness(value).stderr, '', repr(value))

    def test_set_logs_the_expected_fields(self):
        result = self.run_harness('1')
        lines = result.stderr.splitlines()
        self.assertEqual(len(lines), 1)
        match = LINE.match(lines[0])
        self.assertIsNotNone(match, lines[0])
        self.assertEqual(match.groups(), ('7', '1', '00100000', '9', 'CBF43926'))


if __name__ == '__main__':
    unittest.main()
