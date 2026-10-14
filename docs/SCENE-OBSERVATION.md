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

## Synthetic runtime integration (slice130)

**Confirmed:** `shape_observer_adapter` reads actual Cpu argument lanes/raw FPR12
and safe RAM/scratchpad views. It uses explicit pre-PC and actual return-PC,
rejects idle owners, skips handlers before sampling and samples the saved original
shape after volatile r4 changes. Unknown probes retain donor diagnostics; known
probes suppress diagnostics even when capture is off.

`prepare_shape_observation.py` verifies the clean pinned donor and canonical hashes,
then creates three source copies with exact-once hooks. Removing those hooks/include
restores every canonical byte. Lifecycle sites cover create/start/exit/delete/
terminate/main restart, reset/boot and both load overloads before mutation.
Epochs are monotonic identities: nested boot/reset or path/bytes load may advance
more than once. Historical preparation receipts remain preparation-only; the
separate [130 qualification](inputs/slice130-shape-runtime.json) records later checks.

The optional `GT4_SHAPE_RUNTIME_SOURCE` points to one fresh private capsule with
exactly three sources and a receipt. Pinned source hashes, configure dependencies
and header inventory reject edits after configure and extra shadow headers.
Only the synthetic fixture receives overlays; the ordinary game target and
unoverlaid control do not. Selected14 link-map bindings and original COFF strong/
NoDuplicates symbols verify the intended EE replacements. IOP kernel basename
collision is disambiguated by namespace and archive member paths.

**Confirmed controls:** original input/clobber and unwound return, real synthetic
callback dispatch through alarm/run_guest exactly once, ordinary token preservation
through handler save/restore, actual lifecycle invalidation for injected owner2
tokens, direct main restart/reset and failed path/short/partially mutating loads.
No retail generated function or gameplay executes. Core owner2 injection is not
proof of real game thread2 reaching these hooks; boot is source/compile evidence.

Fresh CTest5/5 in14.43s; Python147/147, no skips. Unoverlaid/observed transcripts
match46,036 bytes across15 scenarios, serializing all defined Cpu fields and
selected public ThreadStatus/owner/handler outputs. Host padding is excluded by
fieldwise serialization, not arbitrary byte masking. This does not compare all
private kernel queues, scheduler order/heap/stored contexts or system events.
The pinned ExitDelete path retains previous PC when current/best both become
null; ordinary ExitThread produces idle|unwind. This existing donor distinction
is preserved and recorded, not upgraded to PS2 fidelity or fixed here.

**Unknown/unimplemented:** full probed library/runtime game profile, real car
identity/reachability, owner-led capture, queued graphics ownership and overhead.
No native drawing, scan-mode, timing, input or performance change is installed.

## Full observation library (slice131)

**Confirmed:** `ee_observation_library.py` prepares a fresh source copy after
verifying the frozen baseline and slice129 profile. It preserves the original
producer's strict null-option gates. All328 files match129 exactly; only D5 has
the two54-byte probe statements at `0x00455A5C` and `0x00455A64`.

The full327-unit Release library rebuilt using Clang23.1.2/C++20/VS2022 ABI.
Compiler receipts and every unit's flags/include order match the baseline after
normalizing only the generated-source directory. Current tool executable hashes
are recorded; historical baseline compiler executable identity remains Unknown.

All327 unique AMD64 COFF members are checked. Normalizing only the four-byte
COFF timestamp yields326 exact baseline objects and one D5 difference. D5's
normalized hash also matches its independent129 unit compilation. Archive byte
identity is a separate pin; member path/offset layout may differ. This validates
the intended code delta, not PS2 fidelity or runtime noninterference.

Eight new controls cover exact object delta and independent D5 pin, recipe changes,
missing/duplicate path fields, source copying/tampering, forged baseline pins and
the128MiB archive read bound. Producer CTest1/1 runs Python155/155 with no skips,
using frozen130 native fixture binaries. No native runtime rebuild or retail game
execution occurs in this slice. An independent bounded archive parser/source
replay verifies the completed artifact separately from the production checker.

**Unknown/unimplemented:** a linked retail observation game profile, owner-led
capture, real shape/car association and overhead. The next slice integrates the
qualified library and runtime overlays into an isolated default-off executable;
activation/export and owner-controlled gameplay are later boundaries.
See [slice131 evidence](inputs/slice131-observation-library.json).
