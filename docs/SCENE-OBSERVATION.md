# Shape-call observation contract

Slice 127 qualifies source mechanics and a bounded implementation contract.
It does not install hooks or replace rendering. The game remains authoritative:
observe its existing preparation exactly once, without calling a getter,
callback, matrix helper or model VM a second time.

## Confirmed frontiers

| Frontier | Guest address | Meaning |
|---|---|---|
| Pre-call | `0x00455A5C` | Selected shape call, before JAL/link work |
| Target | `0x0049D308` | Existing shape wrapper in unit_00E7 entry53 |
| Continuation | `0x00455A64` | Normal guest return, unit_00D5 entry171 |

The generated direct call bypasses the runtime dispatcher. Pinned donor
`--probes` emits `probe(c,address)` before instruction work, including labels.
Two explicitly whitelisted probes can provide these frontiers without modifying
the translator. Current `probe` only prints diagnostics. Integration requires
a separate generation/profile/library and runtime source overlay; normal
producer source/object/options gates remain strict.

**Confirmed:** direct depth spill and scheduler unwind can discard the original
C++ stack frame and later dispatch the registered continuation. Hooking only
the lexical C++ return misses these paths. The explicit probe address identifies
the instruction; pre-call `c.pc` can be stale.

## Smallest useful record

Capture original guest argument bits and guest SP before the call. Completion
must match the latest pending token for its ordinary guest owner, epoch,
thread incarnation and SP at the exact return PC without unwind. Consume once.
Owner/thread changes cannot exchange tokens; recursion is LIFO. Missing tokens
produce orphan counts, not reconstructed arguments. Skip handler captures while
preserving ordinary pending tokens through handler save/restore.

Read only fixed raw state fields at `0x70002000`: flag+3, enabled+0x14,
cached FPR bits+0x20 and cursor+0x1F8. Independently mark shape+4/count+0xA
samples valid/missing. Accept full checked direct RAM/scratchpad spans only;
no MMIO, alias normalization, `ld32`, pointer chasing or variable packet copying.
Store immutable values rather than guest pointers. Keep raw pre/post cursors:
their difference is not yet a buffer extent.

**Confirmed limitation:** D5 incoming FPR12 and pre-sampled shape+4 do not prove
the actual FPR12 consumed later by `0x0049E548`. Exact-helper observation would
need separately qualified pre/continuation probes at `0x0049D3D0`/`0x0049D3D8`.
A normally completed shape may take an early return or skip that helper.

## Lifecycle and limits

The observer is host-only and off by default. Fixed storage, saturating counters,
no hook allocation/disk writes and no guest mutation/calls/budget changes.
Capacity failure disables capture and aborts pending tokens, preserving completed
records for draining outside execution. Thread delete/reuse/start/terminate
invalidates that owner's pending tokens. Boot/reset and every state-load attempt
start a new epoch, including loads failing after partial mutation. Observer state
is not part of guest Cpu/ThreadContext/save layout.

**High confidence:** this protocol can preserve call inputs across suspended
execution. **Unknown:** live reachability, selected car/scene identity, aliases,
queued VIF ownership, DMA/VU completion and relationship to visible output.
Instrumented-off still has probe-call overhead; no performance-neutral claim.
Local CL/WL (0,4) is not equivalent to (1,1); CL0 remains unqualified.

## Acceptance before a live candidate

Use existing native CTest and Python fixture entrypoints. Literal synthetic
controls must exercise direct call, depth spill, repeated suspension, thread
switch/handler, recursion/duplicates, reuse/reset/load invalidation, early-return
paths, overflow-safe RAM spans and bounded capacity failures. Verify guest results
against an uninstrumented control and generated probe placement independently.
Core tests alone do not qualify generation, lifecycle overlays or live gameplay.

The first implementation boundary is the bounded correlation/memory core.
Only after that passes should a fresh two-probe profile link the runtime adapter
and lifecycle notifications. Preserve frozen producers, default launcher,
OpenCode branch, active session and saves.

Source identities and review hashes: [slice127 metadata](inputs/slice127-scene-observation.json).
Prior constraints: [Render Contracts](RENDER-CONTRACTS.md),
[VIF State](VIF-STATE-CONTRACT.md), [Reference Coverage](CHANNEL-SELECTION-AUDIT.md).

## Implemented core (slice128)

**Confirmed:** `native/shape_observer.hpp/.cpp` implements fixed host-only tokens,
six independently valid raw fields, original128-bit argument/FPR bits, exact
continuation/SP correlation, LIFO, epoch/incarnation invalidation and handler
skips. No donor dependency or live runtime linkage is present. Owner IDs are
opaque injected keys; the future adapter must reject idle/unknown kernel owners.

Limits are64 owner slots,16 pending per owner and256 completed records. Overflow
or stack ambiguity disables all capture and aborts pending tokens while preserving
completed records. Diagnostic counters saturate; identifiers never wrap. Storage
is single-owner: inspect/copy completed records outside guest execution. start()
begins a fresh capture and clears earlier records/counters; reset_epoch() retains
completed history while invalidating pending associations.

The actual C++ fixture is included in native CTest and Python discovery. Fresh
Windows/Clang build: CTest4/4, Python132/132, no skips or observed compile warnings.
Synthetic controls compare all fixture guest results against an uninstrumented
control; test original bits after clobber, simulated spill/suspension, owner switch,
handlers, recursion, stale/duplicate continuation, reuse/reset/restart, unchanged
cursor, checked RAM spans and all three storage caps.

**Unknown:** live probe placement and reachability, kernel/boot/load notifications,
actual scheduler noninterference and capture overhead. Correct core tests do not
qualify a runtime adapter. No game instance was launched or rendered by this new
component; existing playable behavior and launcher are unchanged.
See [slice128 evidence](inputs/slice128-shape-observer.json).

## Exact generated profile (slice129)

**Confirmed:** `scripts/ee_observation.py` supplies a separate, strict two-probe
recipe. It verifies the existing baseline producer, full source inventory,
translator/toolchain and ELF, then uses a fresh private directory. Exact canonical
probe bytes avoid the donor parser's permissive missing/partial/duplicate/module
tokens. It checks extents before reading candidate sources and rejects every
change beyond the two intended pre-instruction statements. Ordinary
`ee_producer.py` still requires null options and frozen inventory.

```powershell
private/tooling-venv/Scripts/python.exe scripts/ee_observation.py --baseline private/slice116-ee-producer --output private/<fresh-observation-capsule>
```

The qualified129 output is already frozen; do not reuse it. Private receipt:
`observation-generation.json`, exact arguments/pins and328-file source inventory.
Confirmed independent replay: only D5 changes, by54 bytes; removing its two probe
lines restores the original bytes. Registry/header and327 other files are exact.
Probe offsets123724/123984 precede pre-call budget/JAL and return-counter work.

Nine new literal controls reject malformed probes, missing/duplicate labels,
misplaced calls, guest edits, unexpected/missing files, baseline duplicates and
oversized extents. The actual modified D5 compiles with Clang23.1.2/C++20/VS ABI
in a fresh unit-only build. That producer's CTest1/1 runs Python141/141, no skips,
using frozen128 native fixtures. This does not repeat the full128 native build.

**Unknown/unimplemented:** full probed library linkage, runtime probe replacement,
kernel/boot/load notifications, owner-controlled live capture and any overhead.
Source insertion and object compilation are not a playable observation profile.
See [slice129 evidence](inputs/slice129-observation-profile.json).
