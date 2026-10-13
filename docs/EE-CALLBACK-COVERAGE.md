# Bounded EE Callback Recovery

2026-10-13, slice 117. Authorized P0/P1 continuation from commit
`a900038958c886c168006128b9e67e39abb6c5c5`. This is offline analysis, source
generation and changed-unit compilation. The playable runtime/library and their
hash gates remain unchanged. No guest instructions, gameplay, performance probes,
input automation or owner saves were used by this experiment.

## What was established

**Confirmed:** the baseline analyzer does not mark function starts `0x0044EA58`
and `0x0044EAC8` as code/entries. They are not rejected seeds. The generated
registry has zero entries and no bodies for them. The existing dispatcher at
`0x0044EB10` remains registered. A missing EE target without a module replacement
halts in the donor dispatcher; there is no EE interpreter fallback.

**Confirmed, bounded mechanism:** `analysis.cpp` scans LUI + ADDIU/ORI address
constants only inside already marked code. The supplier at `0x0044EAC8` contains
the construction of `0x0044EA58` at `0x0044EAEC` / `0x0044EAF4`; baseline analysis
never reaches that construction. Explicitly seeding the supplier marks its body,
then the constant-discovery pass recovers the callback automatically. Neither
address occurs as an exact absolute word or a direct J/JAL target in the reviewed
file-backed load-segment scan. That scan does not exclude runtime arithmetic,
aliases, relocations, compressed pointers or other ways to reach the code.

Synthetic C++ controls reproduce cold omission, supplier-seed discovery, a raw
supplier-pointer control, and ignored unaligned/outside seeds. They analyze
synthetic instructions and never execute them. These controls share the donor
analyzer; they are not an independent R5900 semantics oracle. Raw span review and
generated-source comparison provide complementary evidence.

**Unknown:** live reachability, intended scene, completeness of the remaining
entry ledger, and any relationship to current performance. The omission is not
evidence that a missing callback caused the observed low FPS.

## Four preserved generation profiles

| Profile | Explicit seeds | Instructions | Entries | Function census | Direct wrapper call sites |
|---|---|---:|---:|---:|---:|
| Baseline | None | 1,240,374 | 135,093 | 14,660 | 10 |
| Supplier only | `0x0044EAC8` | 1,240,419 | 135,102 | 14,661 | 12 |
| Callback only | `0x0044EA58` | 1,240,402 | 135,100 | 14,661 | 12 |
| Both | `0x0044EA58`, `0x0044EAC8` | 1,240,419 | 135,102 | 14,662 | 12 |

Every profile has zero analyzer rejections and zero emitted `stop(c,...)` sites.
This does not exclude dispatch misses or unknown dependencies. Each profile has
328 generated files. Baseline matches the frozen source inventory
`7a1a0909aa7197657ccb09a485d5bbb362d29afd3c08f28ffe7e134c0bf7d087`.

Supplier-only and both-seeds produce **byte-identical generated source**, inventory
`3c140795c020b46a0a24ef2c06a8010a90e59f3df628213b4b9c4838b5c4fd7b`.
The differing function census follows the analyzer's `strong(target,true)` for
explicit seeds versus `strong(target,false)` for recovered constants. It is not
an extra executable body and reinforces why census totals are not coverage proof.

The supplier profile adds exactly 45 marked instructions: 28 in the callback and
17 in the supplier. Nine dispatch labels are added; none are removed. There are
18 changed compilation units: the containing `unit_00D3.cpp` and 17 callers whose
numeric intra-unit entry IDs move. Registry table and generated header are unchanged.
Independent static normalization resolved old/new IDs to their actual guest labels;
the 17 callers and all pre-existing bodies in the containing unit then matched.
This comparison does not execute or independently qualify guest behavior.

All 18 changed units compiled to AMD64 objects with Clang 23.1.2, C++20, Release
`/O2 /Ob2 /DNDEBUG`, VS2022 x64 environment and one build worker, with zero observed
warnings. No full candidate archive, runtime link or gameplay check was performed.
Producer CTest: 2/2 passing in 3.24s. Python discovery: 55 tests, 52 passing and
three explicit skips for absent native Vulkan/audio/card-lock binaries.

## Contract and seed provenance

Input CORE/ELF and donor revision are independently hash-verified by the existing
producer preparation. The diagnostic translator is rebuilt from the pinned donor,
not a changed recovery algorithm. The control executable and generated outputs
are recorded by hash; their new executable hashes are observations, not production pins.

| Guest start | Reviewed span through return delay slot | SHA-256 |
|---|---:|---|
| `0x0044EA58` | 112 bytes | `7aaa37ba67140d0364263591c3dca897baba6d8405611b12907d1d60ab1ad284` |
| `0x0044EAC8` | 68 bytes | `e82cfc898f1123db93397537cbe27ebf7edab55e8f35279a509fd52e94d9cd05` |

The trailing supplier padding NOP at `0x0044EB0C` is excluded. Both starts are
supported by raw instruction/return review and corresponding decomp research,
rather than blindly adding every pointer-like word as an entry.

**Confirmed local supplier behavior:** constructs a two-u32 stack pair, passes
the callback and pair to `0x0044EB10`, and returns after the call. The second pair
store occurs in the JAL delay slot and precedes callee entry. The pair has local
call-stack lifetime; escape/ownership remains Unknown. Incoming r8/r9/r10 are
untouched by this supplier but immediately consumed by the dispatcher. A
four-argument decomp prototype is therefore insufficient for a host bridge.

**Confirmed local callback behavior:** reads the two pair values and can invoke
the model wrapper `0x00455528` for each nonzero value. Other helpers bracket the
first call; `0x004A3230(2)` is always called. Those helpers have transitive state
effects, so the callback is not a pure geometry getter. Return continuations
`0x0044EA90` and `0x0044EAB8` must exist when recovering the callback;
`0x0044EB00` belongs to supplier return. Full transitive effects remain open.
Do not invoke these callbacks again to extract render state.

## Reproduction

Use a fresh immediate child of this checkout's ignored `private/`. The named
slice 117 capsules already exist and must not be overwritten. With the verified
assets checkout and Python environment described in [EE Producer](EE-PRODUCER.md):

```powershell
cmd /c tools\ee_producer\build_analysis.cmd C:\Antigravity\gt4-staticrecomp C:\Users\Alano\.codex\worktrees\native-renderer-roadmap\gt4-staticrecomp\private\ee-coverage-new-attempt
& C:\Antigravity\gt4-staticrecomp\private\tooling-venv\Scripts\python.exe scripts/ee_entry_coverage.py --output private/ee-coverage-new-attempt
```

The script verifies inputs/toolchain and reviewed raw spans, regenerates four
profiles, verifies baseline equality and all address sets, cross-checks analyzer
code/entry flags with generated entries/bodies, and checks exact return-label and
wrapper-call deltas. It rejects emitted stops and supplier/both source differences.
It writes `entry-coverage.json`, `analysis.jsonl` and `changed-units.txt` only in
the new capsule. Failed attempts remain preserved and require a different output.

For changed-unit compilation, open a **VS2022 x64 Native Tools** terminal in this
checkout; the build wrapper's environment does not persist in the caller. Configure
a fresh cache, using the diagnostic source/list, then build only the object target:

```text
cmake -S tools/ee_producer -B private/ee-coverage-new-attempt/compile-check -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DCMAKE_MAKE_PROGRAM="C:/PROGRA~2/MICROS~2/2022/BUILDT~1/Common7/IDE/COMMON~1/MICROS~1/CMake/Ninja/ninja.exe" -DGT4_DONOR="C:/Antigravity/gt4-staticrecomp/private/references/ps2-web-recomp" -DGT4_GENERATED="C:/Users/Alano/.codex/worktrees/native-renderer-roadmap/gt4-staticrecomp/private/ee-coverage-new-attempt/supplier_seed/generated" -DGT4_CHANGED_UNITS="C:/Users/Alano/.codex/worktrees/native-renderer-roadmap/gt4-staticrecomp/private/ee-coverage-new-attempt/changed-units.txt" -DGT4_BUILD_TRANSLATOR=OFF
cmake --build private/ee-coverage-new-attempt/compile-check --target ee_changed_unit_controls --parallel 1
```

Adapt absolute paths to the receiving checkout. This diagnostic CMake target does
not replace producer source equality or native library hash gates. Run builds and
tests serially. The slice 117 qualified compile cache is separate from a preserved
failed configure attempt that lacked the Visual Studio environment.

## Remaining gates

[Public metadata](inputs/slice117-ee-entry-coverage.json) contains only hashes,
addresses, counts and methods. Game bytes, generated source, objects and capsules
remain private. Stronger final profile gates were also applied to the preserved
four-profile outputs without regenerating or overwriting them.

P0/P1 remain **in progress**. Next gates are broader evidence-based entry coverage,
full candidate library/runtime integration with separate pins and native checks,
then owner-controlled menus/showroom/race qualification. P1 still needs a concrete
car/instance, complete state/VM dependencies, matrix/asset conventions and safe
observation lifetimes. This experiment does not authorize or implement a renderer
replacement, performance fix, progressive HD, ultrawide or independent game timing.

Subsequent [slice 118](EE-CANDIDATES.md) qualifies a complete candidate library
and native runtime linking/checks. Owner gameplay and broader coverage remain open;
the slice 117 offline receipts are preserved.
