# Static shape submission and VIF state boundary

Slice 124 follows [conditional packet framing](VIF-FRAMING.md) into the pinned
EE's shape submission code. This is source/ELF research only; no runtime hook,
geometry decoder, performance probe or owner gameplay action was added.
Evidence: [slice 124 metadata](inputs/slice124-vif-state-contract.json).

## Confirmed static submission flow

Generated unit `00E7` matches its frozen slice 116 hash. Independent bounded
ELF operand decoding corroborates the inspected code, including the original
LWC1 instruction at `0x0049D3C8` loading F12 from argument register r17+4.

| Code boundary | Confirmed access or effect |
|---|---|
| `0x0049D308` | Wrapper supplies state pointer `0x70002000`; continues at `0x0049D328` |
| `0x0049D328` | Retains incoming shape argument; u16 count at +0xA, descriptor array at +0x10, stride 8 |
| Descriptor loop `0x0049D458` | Uses QWC at descriptor+4 and directly loads/masks descriptor's first u32 as a DMA reference address |
| Shape argument +4 | Read as f32 bits into F12; compared against a cached state value when state+3 is set |
| Update path `0x0049D3D0` | Calls `0x0049E548` with state, destination parameter 193 and F12; then sets state+3 |
| `0x0049E548` | Caches F12 at state+0x20, reads packet cursor at state+0x1F8, appends 16-byte header and 128 bytes, advances by 144 |

The submission path does **not** add the shape base to the descriptor's first
word. **High confidence:** this path expects a relocated guest address, whereas
the offline reader bounds a serialized shape-relative offset. The disk-to-guest
relocator, lifetime and concrete selected object remain Unknown. Do not pass
disk offsets directly into the runtime or apply a guessed relocation twice.

On a nonzero F12 path, the helper multiplies values read from VU0 VF24/VF25/VF26
and writes results to VF1/VF2/VF3 for the outgoing packet. The zero path copies
the source values directly. Both paths copy VF27 without that multiplication.
These are conditional state/VU/buffer effects: observing this operation must
not evaluate it twice or use later restored registers as its original output.

**Confirmed:** argument+4 participates in floating-point matrix preparation in
this static path; it is not treated as a shape extent. **Unknown:** its universal
unit/scale meaning, association with a selected serialized car after relocation,
camera convention or native vertex formula. Earlier header tooling correctly
kept this field opaque. This slice does not reinterpret it in the public parser.

## Confirmed buffered cycle words; live state Unknown

Independent generated-source and ELF checks find three emitters:

| Entry block | Words constructed/stored into local packet buffer |
|---|---|
| `0x0049C2A8` | `0x01000400` at buffer+8, then `0x01000404` at buffer+44 |
| `0x0049C788` | Same pair and offsets |
| `0x0049F938` | Selects buffer/source path first, then builds the same pair |

The first two main-block pairs are written before their later branches; separate
optional blocks also build the pair under additional guards. The last emitter
selects its source path before common construction. Under standard VIF word
layout these encode CL/WL=(0,4) followed by (4,4). The zero CL case remains
unqualified by the public framer. The later non-fill setting consumes the same
source-vector count as (1,1); that is a framing equivalence only.

Buffer construction is not channel submission or observed VIF register state.
The later word may set (4,4) if executed; it is not proof of restoring a known
prior state. **Unknown:** which list reaches which DMA/VIF unit, whether those
emitters precede the selected Ford Ka packet, and what state is live at submission.
The (1,1) result in slice 123 remains conditional.

## Channel research is a lead, not a geometry contract

Pinned MIT PDTools assigns positions, UVs, normals, colors and strip markers
using raw immediate/type/ordering heuristics. `0xC040` alone has multiple roles
in that helper. Its `/4096`, V flip and strip conversion are reference-tool
operations; they do not establish the game's coordinate or topology contract.

The donor splits UNPACK immediate into low ten address bits, USN bit14 and
TOPS-relative flag bit15. Values such as `0x8000`, `0x8040`, `0xC040` are not
resolved absolute VU addresses. Actual memory rows depend on the unit, TOPS,
cycle, masks and mode state. No vertex, normal, UV or index values were decoded.

Parent review corrected two private report assumptions: helper output operands
are VF1–VF3, not VF24–VF26; and slice 123 supports explicit nonzero conditional
cycles rather than rejecting all cycle-dependent forms. Frozen reports remain
with addenda; verified publication does not inherit their stale head label.

## Evidence and next boundary

Four worker source units total 1,069,308 bytes, checked against generation pins.
Parent re-verifies them and the 6,123,004-byte pinned ELF, corroborating the shape
argument-to-F12 instruction and all five worker ELF window hashes. Detailed
code windows stay below 64 KiB. A separate Luna ELF/source audit independently
agrees on the buffered command pair and branch order. No game assets read here.

This documentation-only slice inherits slice 123's green CTest 3/3 and Python
120/120, zero skips; it does not claim a fresh build or runtime test. No public
software behavior changed, so no new tests or native rebuild were required.

Next extract ordinary UNPACK **raw lane values** with explicit width/count/
signedness and independent literal controls, preserving the distinction from
game geometry. In parallel, trace concrete buffer ownership, relocation and
pre-transfer state. Channel, topology, matrix/clip and live side-effect gates
must pass before native car substitution. P1 remains open.
