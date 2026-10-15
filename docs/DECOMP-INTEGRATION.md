# Integrating gt4-decomp

2026-10-14, slice 151. Read-only design study; nothing implemented. See
[DECOMP-IDENTITY](DECOMP-IDENTITY.md) for why the decomp applies to our CORE.

## Current generated code (Confirmed)

The donor translator emits 326 `unit_XXXX.cpp` files, one per 16 KiB address
window, each `void unit_XXXX(Cpu&, entry, depth)` with a `switch(entry)` to
labels. Units are address windows, not functions; there are no function names.
A call to an address without a translated entry halts with "no translated
code" (`runtime/src/dispatch.cpp`); there is no EE interpreter fallback.

## Coverage finding (Confirmed, stability relevant)

`config/symbol_addrs.txt` lists 8,360 function names; 8,273 are translator
entries. Of 25,821 matched decomp functions with resolvable addresses, 20,145
are entries and **5,676 are not** (example `func_00600018`). Reachability is
Unknown; many are probably reached only through function pointers, the same
class already seen at 0x44EA58/0x44EAC8 (slice 117). Feeding the decomp's full
function list to the translator as extra entries could close crash risks in
modes nobody has played yet.

Follow-up classification (2026-10-15, read-only scan of the ELF): of the 5,676
functions without an entry, 3,223 are in decomp `runtime` (libraries/runtime
support), 546 `race`, 507 `network`, 412 `ui`, 383 `engine`, 226 `adhoc`,
160 `libstream`, 132 `gamedata` and the rest small groups; 5,603 are unnamed.
Only 1 address appears as a 32-bit word anywhere in the ELF (no vtable
evidence), 14 are built as addresses by `lui`+`addiu/ori` in code, and 343 are
`jal` targets only from code that is itself unreached. **High confidence:** most
are dead code linked into the executable (unused library and engine functions),
so the crash risk is smaller than the count suggests. The 14 code-built
addresses are the first candidates for extra entries.

## Level 1: names (cheap)

- A read-only `ee_symbols` script builds a canonical name table (aliases and
  `gthd`-derived names tagged as such).
- Annotate generated labels with comments (`// RaceBase::getFrameInterval`).
  Proof of no semantic change: identical hash after stripping comments and
  identical object code. Needs a new producer contract (generated files are
  hash-pinned, see [EE-PRODUCER](EE-PRODUCER.md)).
- Runtime diagnostics (missing-code messages, probes) can print names.

## Level 2: native replacement (phase 2)

Blockers: guest pointers are 32-bit guest addresses; vtables hold guest code
addresses; 359 decomp files use inline asm, 622 touch hardware, 5,308 use floats
(EE FPU is not IEEE: use `fp::` helpers), 97 use 128-bit types.

Proposed mechanism: `GPtr<T>` (guest u32 address dereferenced through
`ps2::fast<T>`), structs with u32 pointer fields and `static_assert`ed offsets,
an opt-in native table checked at translated call sites (`native_X(Cpu&)` using
guest argument/result registers), indirect calls through `ps2::call`, and A/B
comparison from identical snapshots. First targets: leaf getters/setters, then
leaf float functions, then one-level callers. Hardware, VU0, syscall and
vtable-dispatch code stays translated until later.

Not a speed task: EE AOT already costs only about 1.6-2.3 ms per race frame.

## Upstream contributions

VU1 microcode location and keystream (credit Nenkai's notes), the 5,676
missing-entry audit, and resolving placeholder virtual-slot names from vtables.
