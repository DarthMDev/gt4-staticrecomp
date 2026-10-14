# Selective VU AOT feasibility

2026-10-14, slice145. Read-only source review of pinned MIT donor
`1b4808793a12c45125262fd2d5f50a9ec9f7c020`; no generated GT4 VU code is adopted.
Parent: [Hybrid Port Plan](HYBRID-PORT-PLAN.md). Windows first, no guest JIT.

## Confirmed source mechanisms

- `runtime/src/vu_micro.cpp:788–837` looks up generated VU1 programs and falls
  back to the interpreter. VU0 micro remains interpreted. Native
  `vu_jit_web.cpp:18–31,72–75` returns disabled/null outside Emscripten.
- `runtime/include/ps2/vu_ops.hpp:44–88` derives a64-bit key from entry and sorted
  reachable paired instruction words. Entry-cache candidates also compare their
  code bytes. Address alone is not identity; static registry lookup is by key.
- Code writes (`vif.cpp:242–252`, `memory.cpp:74–80`) and state load
  (`vu_micro.cpp:982–994`) invalidate lookup generations. These paths must remain
  covered when compiling any program.
- `recomp/vurecomp.cpp:568–631` emits offline C++ functions and a registration
  table from recorded16KiB VU1 code images. Qualified114/143 native main already
  calls `register_generated_vu()` at line81; donor has a weak empty default at
  `vu_micro.cpp:978`. A generated provider must be explicitly compiled/linked;
  generated source files alone do not activate AOT. Current project build does
  not select an emitted GT4 table/provider.
- Checked116 generated outputs describe328 EE files, without a VU corpus.
  The144 report contains aggregate VU timing/call counts, not program identities
  or bytes. Hot VU0/VU1 identities and usable GT4 microcode records remain Unknown.

## Gates before trusting the emitter

The existing emitter is a starting point, not a qualified GT4 backend:

1. Validate exact16,384-byte records, successful reads and content identity before
   invoking it; its current read lacks exact size/gcount checks.
2. Use a fresh output directory. It deletes preexisting `vu1_*` outputs at608–610;
   never point it at a frozen capsule.
3. Preserve bounded execution. Interpreter `run_fast` caps at1,000,000 instructions
   (`vu_micro.cpp:872–918`); generated goto loops lack that cap (`vurecomp.cpp:525–562`).
   Add an independently tested bound/resume/fallback contract before adoption.
4. Exact identity and collision controls: the static registry/deduplication uses
   only64-bit keys. Preserve entry, full record and reachable-byte digest; do not
   accept a distinct-code same-key variant silently.
5. Compare all defined state and side effects from identical initial conditions:
   VU registers/flags/TPC, memory, TOP/ITOP and GIF/XGKICK outputs. Include branch/
   delay behavior, unsupported exits, mutation/load invalidation and budget paths.
   Interpreter agreement proves this donor model, not independent PS2 fidelity.

Lower-op unsupported paths may fall back to interpretation. Unknown upper
encodings map to NOP in the current donor decoder/emitter; that is an existing
model limit, not permission for new tooling to silently accept unsupported code.

## Smallest next experiment

First obtain matched145 overhead intervals and separate VU0-micro/VU1 timing in a
new pinned scope derivation. Then, if VU remains material, add a bounded private
identity/call inventory for one owner-selected scene. Capture one hot VU1 entry
and exact16KiB image with explicit quotas/activation; no bulk asset/RAM scans.
Qualify the safety gates above and one generated function against independent
literal controls plus the interpreted model before linking its explicit provider.
Keep interpreter fallback for unknown/mutated variants, no JIT, and compare AOT-on
with interpreter-only in the same scene/probe profile. No speedup is predicted.

Read-only Luna review/source hashes, corrected registrar evidence and pair review
are pinned in145 candidate metadata. Its initial registrar wording was corrected
before adoption. Private reports/corpora remain ignored. No VU tool build/run,
microcode capture, generated-table integration or VU performance change occurred.
