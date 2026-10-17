//@section includes
#include "license_guard.hpp"
//@section helpers
// TASK-017 license demo guard (slice205, scripts/prepare_license_guard.py). Runs only when the indirect call from
// the license demo loader (0x001100D0, return label 0x001101E4) enters RaceSolitaire::loadDemoInputs (0x003EAD18).
// Rule and rationale: native/license_guard.hpp. One stderr line per checked demo load.
namespace {

bool license_guard_guest_span(std::uint32_t address, std::uint32_t size) {
    return address < kRamSize && size <= kRamSize - address;
}

void license_demo_guard(Cpu &c) {
    namespace guard = gt4::license_guard;
    if (c.r[31].w[0] != guard::kLoaderReturn) return;
    const std::uint32_t buffer = c.r[17].w[0];  // s1: the demo file buffer the loader decrypted in place
    const std::uint32_t body = c.r[5].w[0];     // a1: buffer + header body offset (the loader's arg1)
    if (!license_guard_guest_span(buffer, guard::kHeaderBytes)) {
        std::fprintf(stderr, "[license-guard] demo buffer 0x%08X outside RAM -> unchanged\n", buffer);
        return;
    }
    const std::uint32_t offset = ld32(buffer), size = ld32(buffer + 4u), key = ld32(buffer + 12u);
    if (offset != guard::kExpectedBodyOffset || body != buffer + offset || size > guard::kMaxBodySize ||
        !license_guard_guest_span(body, size)) {
        std::fprintf(stderr, "[license-guard] demo 0x%08X header offset 0x%X size 0x%X arg1 0x%08X unexpected -> unchanged\n",
                     buffer, offset, size, body);
        return;
    }
    const guard::Verdict verdict = guard::evaluate(g_ram + body, size, key);
    if (!verdict.guard) {
        std::fprintf(stderr, "[license-guard] demo 0x%08X size 0x%X key 0x%08X crc=%s layout=%s count=0x%08X -> unchanged\n",
                     body, size, key, verdict.crc_ok ? "ok" : "fail", verdict.layout_ok ? "ok" : "bad", verdict.count);
        return;
    }
    st32(body + guard::kManagerOffset, guard::kSkipCount);
    std::fprintf(stderr, "[license-guard] demo 0x%08X size 0x%X key 0x%08X crc=fail layout=bad count=0x%08X -> skipped "
                 "(count set to 0x%08X, guarded load %u)\n",
                 body, size, key, verdict.count, guard::kSkipCount, guard::note_guarded());
}

}  // namespace

//@section hook
    if (pc == gt4::license_guard::kLoadDemoInputs) [[unlikely]] license_demo_guard(c);
