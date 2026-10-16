"""Halt forensic dump overlay (slice 204, TASK-017): unset is donor behaviour; set prints [halt-dump] lines.

The behaviour tests compile the overlay helper text with clang-cl and call it on synthetic thread state; they are skipped
when clang-cl is absent. The derivation tests check the anchors against the pinned donor kernel.cpp.
"""
import hashlib
import os
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
# The donor dump() lines that the overlay anchors on (after CRLF normalisation).
DONOR_EXCERPT = (
    'void dump() {\n'
    '    for (const auto &[id, t] : g_threads) {\n'
    '        std::fprintf(stderr, "[thread] %d status %d\\n", id, t.status);\n'
    '    }\n'
    '    for (const auto &[id, s] : g_semas)\n'
    '        if (!s.waiters.empty()) std::fprintf(stderr, "[sema] %d count %d waiters %zu\\n", id, s.count, s.waiters.size());\n'
    '}\n'
)
SEMA_LINE = '        if (!s.waiters.empty()) std::fprintf(stderr, "[sema] %d count %d waiters %zu\\n", id, s.count, s.waiters.size());\n'
DONOR_KERNEL = ROOT / 'private/references/ps2-web-recomp/runtime/src/kernel.cpp'
CAPSULE_KERNEL = ROOT / 'private/slice204-halt-runtime/kernel.cpp'


class HaltOverlayDerivationTests(unittest.TestCase):
    def test_overlay_inserts_helpers_and_one_call_and_reverses(self):
        derived = producer.apply_halt_dump(DONOR_EXCERPT)
        self.assertEqual(derived.count('bool halt_dump_enabled() {'), 1)
        self.assertEqual(derived.count('void halt_dump_copy(int id, const Reg128 *gpr) {'), 1)
        self.assertEqual(derived.count('\n    halt_dump();\n'), 1)
        self.assertLess(derived.index('void halt_dump() {'), derived.index('void dump() {'))
        self.assertNotEqual(derived, DONOR_EXCERPT)

    def test_overlay_sections_match_anchor_table(self):
        sections = producer.overlay_sections(producer.HALT_OVERLAY_PATH.read_text(encoding='utf-8'),
                                             producer.HALT_OVERLAY_ANCHORS)
        self.assertEqual(set(sections), set(producer.HALT_OVERLAY_ANCHORS))
        for name, text in sections.items():
            self.assertTrue(text.strip(), name)

    def test_donor_kernel_matches_pin_when_present(self):
        if not DONOR_KERNEL.exists():
            self.skipTest('donor checkout not present')
        raw = DONOR_KERNEL.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), producer.HALT_PIN['kernel.cpp'])
        self.assertEqual(raw.count(SEMA_LINE.replace('\n', '\r\n').encode('utf-8')), 1)

    def test_capsule_kernel_is_the_overlay_of_the_donor_when_present(self):
        if not DONOR_KERNEL.exists() or not CAPSULE_KERNEL.exists():
            self.skipTest('donor checkout or slice204 capsule not present')
        text = DONOR_KERNEL.read_bytes().decode('utf-8').replace('\r\n', '\n')
        self.assertEqual(producer.apply_halt_dump(text).encode('utf-8'), CAPSULE_KERNEL.read_bytes())

    def test_fresh_capsule_adds_kernel_only_with_the_option(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            donor = root / 'private/references/ps2-web-recomp/runtime/src'
            donor.mkdir(parents=True)
            fixture = {'system.cpp': b'a\n', 'vif.cpp': b'b\n', 'vu_micro.cpp': b'c\n'}
            pins = {}
            for name, data in fixture.items():
                (donor / name).write_bytes(data)
                pins[name] = hashlib.sha256(data).hexdigest()
            kernel_data = DONOR_EXCERPT.replace('\n', '\r\n').encode('utf-8')
            (donor / 'kernel.cpp').write_bytes(kernel_data)
            halt_pin = {'kernel.cpp': hashlib.sha256(kernel_data).hexdigest()}
            with patch.object(producer, 'ROOT', root), patch.object(producer, 'PINS', pins), \
                    patch.object(producer, 'HALT_PIN', halt_pin), \
                    patch.object(producer, 'derive', lambda name, text, **options: text):
                output = root / 'private/capsule'
                receipt = producer.prepare(output, root, halt_dump=True)
                self.assertEqual({p.name for p in output.iterdir()},
                                 {'system.cpp', 'vif.cpp', 'vu_micro.cpp', 'kernel.cpp', 'runtime.json'})
                self.assertEqual(receipt['halt_dump_profile'], 'overlay-v1')
                self.assertEqual(receipt['donor_sha256']['kernel.cpp'], halt_pin['kernel.cpp'])
                plain = root / 'private/plain'
                plain_receipt = producer.prepare(plain, root)
                self.assertNotIn('halt_dump_profile', plain_receipt)
                self.assertFalse((plain / 'kernel.cpp').exists())


@unittest.skipUnless(CLANG_CL.exists(), 'clang-cl is not installed')
class HaltDumpBehaviourTests(unittest.TestCase):
    """Compiles the overlay helpers with a minimal guest model and runs one synthetic halt per scenario."""

    MODEL = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
namespace ps2 {
inline constexpr std::uint32_t kRamSize = 32u << 20;
inline constexpr std::uint32_t kUnwind = 1u;
union alignas(16) Reg128 {
    std::uint64_t d[2];
    std::int64_t sd[2];
    std::uint32_t w[4];
};
struct Cpu {
    Reg128 r[32];
    std::uint32_t pc;
};
struct ThreadContext {
    Reg128 r[32];
    std::uint32_t pc;
};
std::uint8_t g_ram[kRamSize];
Cpu g_cpu;
namespace kernel {
struct Thread {
    int id = 0;
    int status = 0;
    int priority = 0;
    std::uint32_t entry = 0, stack = 0, stack_size = 0;
    ThreadContext context{};
};
std::map<int, Thread> g_threads;
int g_current = 0;
Thread *current() {
    auto it = g_threads.find(g_current);
    return it == g_threads.end() ? nullptr : &it->second;
}
{helpers}
void dump() {{
    halt_dump();
}}
}}  // namespace kernel
}}  // namespace ps2

void put32(std::uint32_t address, std::uint32_t value) {{ std::memcpy(ps2::g_ram + address, &value, 4); }}

int main(int argc, char **argv) {{
    using namespace ps2;
    const char *scenario = argc > 1 ? argv[1] : "";
    const std::uint32_t s = 0x00100000u, p = 0x00110000u;
    std::uint32_t base = 0x00860000u, count = 2u;
    std::uint32_t struct_at = s;
    if (scenario[0] == 'n') {{ base = 0x00400000u; count = 1u; }}
    if (scenario[0] == 'u') struct_at = 0xFFFFFF00u;
    put32(s + 0x5C, base);
    put32(s + 0x60, 0x00200000u);
    for (std::uint32_t word = 0; word < 16; ++word) put32(p + 4 * word, 0xA0000000u + word);
    kernel::Thread copier;
    copier.id = 1;
    copier.status = 2;
    copier.priority = 64;
    copier.entry = 0x00100218u;
    copier.context.pc = 0x00346D24u;
    for (int reg = 0; reg < 32; ++reg) copier.context.r[reg].w[0] = 0x1000u + reg;
    copier.context.r[16].w[0] = struct_at;
    copier.context.r[17].w[0] = p;
    copier.context.r[18].w[0] = 3u;
    copier.context.r[19].w[0] = count;
    copier.context.r[3].w[0] = 0x00120000u;
    copier.context.r[6].w[0] = 0x00870000u;
    kernel::Thread halted;
    halted.id = 17;
    halted.status = 1;
    halted.priority = 13;
    halted.entry = 0x005786F0u;
    halted.stack = 0x008722C0u;
    halted.stack_size = 0x1000u;
    kernel::g_threads[copier.id] = copier;
    kernel::g_threads[halted.id] = halted;
    kernel::g_current = 17;
    g_cpu.pc = 1u;
    for (int reg = 0; reg < 32; ++reg) g_cpu.r[reg].w[0] = 0x2000u + reg;
    g_cpu.r[29].w[0] = 0x00872F80u;
    kernel::dump();
    return 0;
}}
'''

    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory()
        sections = producer.overlay_sections(producer.HALT_OVERLAY_PATH.read_text(encoding='utf-8'),
                                             producer.HALT_OVERLAY_ANCHORS)
        source = Path(cls.folder.name) / 'harness.cpp'
        source.write_text(cls.MODEL.replace('{helpers}', sections['helpers']).replace('{{', '{').replace('}}', '}'),
                          encoding='utf-8')
        cls.exe = Path(cls.folder.name) / 'harness.exe'
        subprocess.run([str(CLANG_CL), '/nologo', '/std:c++20', '/EHsc', '/W4', str(source), '/Fe:' + str(cls.exe)],
                       check=True, capture_output=True, cwd=cls.folder.name)

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def run_harness(self, scenario, value='1'):
        environment = {k: v for k, v in os.environ.items() if k != 'GT4_HALT_DUMP'}
        if value is not None:
            environment['GT4_HALT_DUMP'] = value
        return subprocess.run([str(self.exe), scenario], env=environment, capture_output=True, text=True, check=True)

    def test_unset_and_zero_are_silent(self):
        self.assertEqual(self.run_harness('o', None).stderr, '')
        for value in ('0', ''):
            self.assertEqual(self.run_harness('o', value).stderr, '', repr(value))

    def test_overlap_names_the_stack_thread(self):
        stderr = self.run_harness('o').stderr
        self.assertIn('[halt-dump] begin threads=2', stderr)
        self.assertIn('[halt-dump] copy thread=1 s=0x00100000 p=0x00110000 i=3 n=2', stderr)
        self.assertIn('[halt-dump] copy words at p: A0000000 A0000001', stderr)
        self.assertIn('[halt-dump] overlap thread=17 copier=1 base=0x00860000 end=0x880000 stack=0x008722C0..0x8732C0', stderr)
        self.assertNotIn('overlap none', stderr)

    def test_no_overlap_reports_none(self):
        stderr = self.run_harness('n').stderr
        self.assertIn('[halt-dump] overlap none copier=1 base=0x00400000 end=0x410000', stderr)
        self.assertNotIn('overlap thread=', stderr)

    def test_unreadable_struct_is_reported_not_read(self):
        result = self.run_harness('u')
        self.assertIn('[halt-dump] overlap unknown copier=1 base unreadable', result.stderr)
        self.assertIn('base=unreadable', result.stderr)

    def test_halted_thread_stack_is_dumped(self):
        stderr = self.run_harness('o').stderr
        self.assertIn('[halt-dump] halted thread=17 sp=0x00872F80 pc=0x00000001', stderr)
        self.assertEqual(stderr.count('[halt-dump] stack '), 16)
        self.assertTrue(stderr.rstrip().endswith('[halt-dump] end'))


if __name__ == '__main__':
    unittest.main()
