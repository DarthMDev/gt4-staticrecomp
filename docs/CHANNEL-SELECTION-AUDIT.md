# Reference channel predicates: coverage and limits

Slice 126 audits pinned PDTools channel-selection predicates against the
qualified [raw lane metadata](RAW-CHANNEL-LANES.md). It adopts no converter,
geometry selector, coordinate formula or topology. Evidence:
[slice 126 metadata](inputs/slice126-channel-selection.json).

## Confirmed conditional predicate coverage

Workers match the exact frozen slice 125 derived-data hash before and after
reading. All 2,320 records pass lane count/digest checks; no original asset or
packet payload is read. Separate implementations reproduce these counts:

| Published helper predicate | Garage packets (154) | Racing packets (312) |
|---|---:|---:|
| Position raw immediate C000 or 8000 | 154 | 312 |
| UV predicate: C040/32-bit or 8040/16-bit | 136 | 219 |
| Selected UV has two components, as converter requires | 113 | 219 |
| Selected UV has three components and would be skipped | 23 | 0 |
| Float-normal predicate: C040/C080, 32-bit, three components | 126 | 6 |
| Byte-color predicate, with four components observed | 51 | 101 |
| Byte reset-marker predicate C040 | 154 | 312 |

These are **reference predicate matches**, not confirmed game channel roles.
All matching roles have one candidate in the observed packets; missing roles
remain missing in the audit, and synthetic multiple matches remain visible.
The normal predicate's low racing coverage does not mean the game lacks normals.
It means this particular reconstruction rule cannot be adopted as a complete
lighting/geometry contract without further work.

Racing data includes 8-bit, three-component input at immediate8080 in 205
packets, plus other base/type combinations and nonbase immediates. These are
unassigned raw patterns. Do not invent roles by proximity to the reference's
addresses or replace missing normals with an unexplained default.

## Full-header and reference-parser boundary

The initial worker audit sees UNPACK-derived records only. Parent rechecks the
complete 4,916 command headers using the frozen slice 123 hash, excludes C0C0
from the reference's typed-array view, and models the reference's NUM0 behavior.
No actual NUM0 or non-UNPACK position-immediate collision appears in this
selected set. The complete-header predicate audit reproduces every count above.

This resolves command-list coverage for the **qualified conditional framing**;
it does not validate or execute PDTools' complete VIF parser. That reader treats
C0C0 as a GIFTag and has known payload-framing gaps. Our framer records the
standard V3-32 12-byte input span without GIF interpretation. The marker does
not satisfy any role predicate here, so excluding it does not change these counts.

Eight private literal metadata controls demonstrate first-match ordering,
multiple candidates, a broad position predicate matching a NOP with a colliding
immediate, C0C0 exclusion, a three-component color selected by a broad predicate,
NUM0's absent reference arrays and no nearest-address assignment. They control
the audit's boundaries, not game geometry.

## Why the converter remains a research reference

- The UV predicate can select a three-component array that its converter skips.
- The color predicate tests byte-array type but lacks the four-index safety guard
  its conversion needs. Observed matching colors have four components; other
  inputs could violate that assumption.
- Reset handling starts at source row1 and assumes the stream exists and has
  sufficient rows; source code does not enforce the complete bounds contract.
- FirstOrDefault makes ordering observable. A missing channel or extra candidate
  cannot be silently dropped during migration.
- Raw immediates carry TOPS/USN flags; masks, STMOD/ROW/COL, incoming cycle,
  relocation and matrix/owner state remain unqualified. Raw input lanes are not
  post-VU vertex results.

Primary pinned MIT source:
[PGLUshape.GetShapeData](https://github.com/Nenkai/PDTools/blob/ffe0bb26377ac9ff62f40951c1c0b16c5d9a380f/PDTools.Files/Models/PS2/PGLUshape.cs#L107).
Its `/4096`, float conversion, V flip and alternating strip algorithm are
reference-tool behavior, not an accepted GT4 coordinate/topology specification.
No implementation is copied, built or linked here.

## Next boundary

This research-only slice inherits slice 125's green CTest 3/3 and Python 131/131,
zero skips. No new public software behavior, native build, gameplay, performance,
input, save or timing change. Derived arrays and private experiments remain ignored.

Next establish an observation point after the authoritative game preparation
has run once, with concrete object/relocation, command-buffer ownership,
matrix and completion semantics. Continue bounded channel/strip semantics with
explicit controls and missing-role behavior. The audit is useful for exposing
the reference's limits; it does not qualify native car substitution. P1 remains open.
