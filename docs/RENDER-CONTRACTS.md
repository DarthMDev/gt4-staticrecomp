# Initial Model and Camera Contracts

Slice 116 is bounded P1 static research. No runtime hooks, render substitution,
performance probes or automated gameplay were added. The original game path
remains authoritative. P1 is **in progress**, not a completed native renderer.

## Evidence and method

The fresh P0 producer regenerates the frozen C++ inventory exactly. The scanner
`scripts/render_contract_inventory.py --output <verified-private-producer>`
checks the ELF and complete source inventory before mapping candidates to unit
registries, entry IDs and dispatch labels. It hashes the first 64 executable-file
bytes at each address: these are **prefix fingerprints**, not complete function
fingerprints. It records literal generated direct-call sites and separately
decodes raw JAL words in executable segments. Neither count measures frequency;
raw executable words can include data, and indirect/tail/optimized calls are
excluded. Full private site lists are in `render-contracts.json`.

The decomp reference is pinned at `c4b82c9b90b8ad712edfb6653cafb8451379a2db`.
Its `orig/README.md` names CORE SHA-1
`30031137ce494323816bb707b90d3ec0d07937d2`, which matches the verified local
USA v2.00 CORE. This supports executable correspondence despite differing disc
versions; it does not prove every asset/bootstrap is shared. Symbol names inferred
by that project's GT HD matching remain confidence-labelled research names.
No decomp implementation was copied or linked into the active runtime.

| Candidate | Guest address | Generated dispatch | Name confidence |
|---|---|---|---|
| ModelSet2 begin | `0x00455370` | unit_00D5 / 109 | High confidence structural role |
| ModelSet2 end | `0x004554D0` | unit_00D5 / 131 | High confidence structural role |
| Render all models | `0x00455528` | unit_00D5 / 136 | High confidence structural role |
| Internal model command dispatch | `0x00455708` | unit_00D5 / 153 | High confidence structural role |
| Camera projection matrix | `0x0044F210` | unit_00D3 / 420 | High confidence external symbol mapping |
| Camera modelview matrix | `0x0044F280` | unit_00D3 / 426 | High confidence external symbol mapping |
| Matrix copy helper | `0x004A7550` | unit_00E9 / 169 | Confirmed 64-byte copy behavior |

Exact prefix/unit hashes and counts are public metadata in
[slice 116 evidence](inputs/slice116-ee-producer-contracts.json).

## Confirmed omission and unresolved coverage

Raw/generated literal call counts differ: begin 22/20, end 21/19, render-all
12/10, internal-render 26/24, matrix-copy 66/62. The omitted raw sites have no
matching generated call/return markers. This does not make every discrepancy a
reachable missing function.

**Confirmed:** decomp callback `0x0044EA58` contains the two render-all calls
at `0x0044EA88`/`0x0044EAB0`. Supplier `0x0044EAC8` passes that callback to
`0x0044EB10`. Fresh unit_00D3 has zero entries at the two function starts and
does not emit their bodies/labels; dispatcher `0x0044EB10` is registered as
entry 318 and can call the supplied callback indirectly. Both missing function
starts have prefix hashes in the evidence metadata.

The donor `runtime/src/dispatch.cpp` tries a registered module for a missing EE
entry, then records the miss and halts with unwind. It has no EE interpreter
fallback. Entering the missing callback without a module replacement would take
that stop path. **Unknown:** whether owner-tested gameplay reaches these functions;
no live missing-entry failure was reproduced here. **Hypothesis:** heuristic
callback/constant recovery omitted the path; the exact recovery stage has not
been diagnosed. Do not change seed lists or relax source identity gates to hide
this finding. P0 coverage remains open.

## Render wrapper preconditions and observable effects

**Confirmed from generated code:** render-all receives model in guest a0 and
instance argument in a1. It calls begin once, then internal-render for model
indices starting at zero, then end on normal completion. The unsigned 16-bit
count at model+`0x16` is read after begin and again after each internal call.
Caching that count needs an immutability proof.

Internal-render reads the record table pointer at model+`0x38`, uses stride
`0x28`, and reads command stream at record+`0x18`. A null stream returns zero.
A null explicit instance falls back to the pointer at model+`0x7C`; instance
fields include data at `0x04` and callback object at `0x14`. The inspected signed
index check is not an independent nonnegative-bounds guarantee. A host bridge must
validate indices, address arithmetic, readable spans and lifetimes itself.

**Unknown:** ownership, relocation lifetime, particular car identity, pass identity,
thread/frame scope, concrete callbacks and whether these boundaries cover every
required rendering path. A caller at `0x003403B8` invokes render-all from guest
JAL `0x0034044C` with an explicit null instance; its showroom role is unproven.

## Side-effect ledger

| Boundary | Confirmed local effects | Remaining dependency |
|---|---|---|
| Begin | Reads model/instance flags, updates globals/state setters; registers callback `0x0049CEA0` through `0x0049CC38` | Transitive state ownership and packet completion |
| End | Calls `0x004AB040` with 4 then 3; conditional state restores using globals `0x00623898`/`0x006238B8` | All restoration effects and failure/unwind behavior |
| Internal-render | Byte command table `0x006AABD0` for values below 91; recursion, state/matrix operations and callbacks | Concrete stream/LOD/shape/texture/VM coverage |
| Shape branch | Calls `0x0049D308`, increments guest u32 at `0x006238BC` | Submission ownership, queue/device completion |
| Model VM branch | Guard at guest stack+`0x54` allows one `0x00457278(model,2,index,instance)` call per internal invocation | Bytecode/update semantics, indirect host methods |
| Camera getters | Save/setup helpers, adjusted-this virtual call, matrix copy, restoration | Concrete virtual targets and matrix conventions |

Internal-render callback records at vtable+`0x10`/`0x18` can select branches;
their invocation count is path dependent. The VM helper reads instance fields
`0x04`/`0x08`/`0x0C`, model+`0x5C`, enters `0x004569F0`, and can invoke an
indirect callback at vtable+`0x20`. Skipping the existing path would remove more
than mesh drawing. Alternate wrappers at `0x00455598` and `0x00455300` also need
their own contracts; the latter lacks a local end call.

## Camera observation contract proposal

**Confirmed:** camera getters receive self in a0, destination in a1 and float
argument bits in FPR12. Projection uses adjusted-this virtual record at vtable
`0x10`; modelview uses `0x18`. Local decomp placeholder `Obj::update(float)` is
not evidence of a physics update: the corresponding class map names these slots
`applyProjection`/`applyModelview`.

`0x004A7550` copies VU0 vf28-vf31 into 64 destination bytes, supporting aligned
and unaligned output. Helpers run VU0 microprograms at decimal 2304/2352, touch
scratchpad byte `0x70002000` and reset matrix registers during restoration.
Therefore these getters are not pure host calculations.

**Proposed, not implemented:** observe a validated destination after the existing
authoritative invocation returns. Copy immutable state on its owner thread.
Do not invoke the getter/callback/VM a second time, and do not sample restored
VU registers as though they still held the result. Matrix order, handedness,
clip/depth range, interpolation meaning and aspect injection remain Unknown.

## Asset format research

Nenkai's published [GT4 MDLS template](https://github.com/Nenkai/GT-File-Specifications-Documentation/blob/05e52347890541758222d173dbb62d2b40581b19/Formats/PS2/GT4/GT4_MDLS_ModelSet.bt)
describes a `0x80` header, model count at `0x16`, model table at `0x38`, and `0x28`
records with commands at `0x18`. These agree with the inspected executable
accesses. **High confidence correspondence**, not validation on a chosen car.
The template also identifies relocation, per-color texture tables, material
variations, bind matrices and embedded/external instance forms. Disk offsets
must not be mistaken for relocated guest pointers.

The [common shape template](https://github.com/Nenkai/GT-File-Specifications-Documentation/blob/05e52347890541758222d173dbb62d2b40581b19/Formats/PS2/PS2ModelCommon.bt)
describes VIF parts, DMA quadword lengths and packed texture/material indices.
The [command template](https://github.com/Nenkai/GT-File-Specifications-Documentation/blob/05e52347890541758222d173dbb62d2b40581b19/Formats/Shared/ModelSetupCommands.bt)
includes shape calls, callback-driven branches, LOD and matrix/state operations.
Version-specific online-demo addresses cannot be transferred to this executable.
No parser implementation or asset dump was imported; publication is a research
lead, not an independent correctness oracle or a licensing decision for reuse.

## Before the first native car

[Slice 118 asset research](CAR-ASSET-RESEARCH.md) locates a bounded volume-index
candidate and verifies its header/table metadata. Page decoding and a named-car
mapping are still open; no selected asset or live instance is qualified.
At that checkpoint these were unknown. [Slice 119](NENKAI-REFERENCES.md) now
decodes the catalogue privately with independent hash/count/extent checks;
named-car mapping, model/texture payloads and live-instance qualification remain open.
**Slice 120:** [the asset ledger](CAR-ASSET-RESEARCH.md) confirms offline Ford
Ka '01 SpecDB identity with independent selected-row decoding. Bounded public
volume tooling is tested; the car's menu/LOD basename remains Unknown. This
does not identify a live instance or qualify any geometry/camera convention.
**Slice 121:** [Model Asset Probe](MODEL-ASSET-PROBE.md) resolves the exact
offline garage/racing file pair and bounds their serialized main headers.
Signed VM words and zero-size texture slots remain opaque; no live model,
geometry, texture pixels, relocation or camera convention is qualified.

1. Continue the omitted-entry/callback ledger using explicit seed provenance and
   error behavior; preserve the baseline producer. [Slice 117](EE-CALLBACK-COVERAGE.md)
   recovers one pair offline but does not qualify its live usage. Its supplier's
   r8/r9/r10 dependencies invalidate a four-argument-only host bridge.
2. Identify one real car/instance, concrete virtual methods, stream/pass selection
   and all state/VM/completion effects on the owner-controlled scene.
3. Establish bounds-checked relocation and geometry/texture/LOD extraction,
   coordinate/matrix/clip conventions, lifetimes and independent synthetic controls.
4. Review a bounded observation hook with single callback evaluation. Only then
   create a candidate executable for owner validation and P2/P3 implementation.

There is no supported conclusion that VU/GS is the current performance bottleneck,
and no claim of progressive/internal HD/high-refresh/ultrawide support from this work.

[Slice124 static VIF state contract](VIF-STATE-CONTRACT.md) extends shape
submission evidence with original ELF operand checks, direct runtime descriptor
addresses, argument+4 f32 flow and conditional matrix/buffer effects. Locally
constructed cycle words are not live channel-state proof. Disk relocation,
concrete buffer ownership and channel/topology/matrix conventions remain open.
