# One-Car Asset Research Ledger

2026-10-13, slice 118. Luna performed bounded read-only catalogue research while
the parent built EE candidates. The parent independently checked ISO directory,
real header and offset-table metadata. No game navigation, asset extraction or
live car observation was performed. **No named car is qualified yet.**

**Slice 119 update:** catalogue decoding and directory/extent validation now
succeed in a bounded private Luna experiment, independently replayed by the parent.
The slice 118 findings below retain their checkpoint context. Subsequent evidence
resolves the codec and packed-version interpretation; named-car identity, payloads
and live instances remain unqualified at slice 119. See [references](NENKAI-REFERENCES.md) and
[slice 119 metadata](inputs/slice119-nenkai-assets.json).

**Current slice 120 update:** offline USA SpecDB identity now confirms Ford Ka
'01 / `ka_01` / code 59. Its actual model filename and live instance remain
Unknown. The appended slice 120 section records selected extraction, independent
row replay, public reader controls and the next loader-mapping boundary.

## Confirmed local observations

The current ISO size matches the pinned 5,314,478,080 bytes. Its full SHA-256
was verified in slice 116; this research did not repeat the whole-file hash.
The ISO9660 root entry `/GT4.VOL;1` has 2,459,502,592 bytes at LBA 105879,
absolute ISO byte offset 216,840,192. Existing public inputs pin the ISO,
CORE and executable, rather than individual car assets.

The real-header candidate at VOL offset **17,483,776** has the following bounded
metadata, independently reproduced by the parent:

| Field | Observation |
|---|---|
| Magic | `0xACB990AD` |
| Version major/minor | `1` / `3` |
| Declared compressed ToC length | 365,622 bytes |
| Physical header/ToC pages | 179 |
| Page size | 2,048 bytes |
| Logical ToC pages | 407 |
| Offset table | 408 entries, 1,632 bytes |
| Decoded offset range | 1,696 through 365,622, strictly increasing |
| Indexed extent end | VOL offset 17,849,398 |
| Rounded data start | VOL offset 17,850,368 |

The first offset equals the 64-byte header plus the offset table. All observed
offsets fit inside the declared physical page extent and volume. The parent
read exactly 67,232 unique bytes: an initial 64 KiB window, 64-byte real header,
and 1,632-byte offset table. Luna additionally attempted the first eight indexed
pages using bounded reads well below its 256 KiB additional-read limit.
Window hashes and methods are in [public metadata](inputs/slice118-volume-catalogue.json).
No game bytes are published.

## Primary reference and limits

Nenkai's [RoFS format documentation](https://github.com/Nenkai/gt-modding-hub/blob/b312010b54c89e774a6c23f5d37a9f6de8606812/docs/formats/volume/ps2_gt4_volume.md)
identifies a decoy table at the volume start, a GT4 real table at page `0x2159`,
and a compressed/encrypted B-tree. **High confidence:** the observed signature,
page dimensions and table boundaries correspond to that structure. This primary
research document is not an independent correctness oracle.

**Unknown at the slice 118 checkpoint:** the version discrepancy (observed 1.3, documented 3.1) and the exact
page codec/layout. Luna's bounded XOR and common zlib-mode attempts did not decode
a valid page. No directory node/path was recovered; monotonic offsets alone do
not validate a complete reader. The offset-table extent end above follows the
observed offsets and is distinct from an assumed header-plus-length convention.

The [GT4 folder documentation](https://github.com/Nenkai/gt-modding-hub/blob/b312010b54c89e774a6c23f5d37a9f6de8606812/docs/ps2/gt4/file_structure.md)
provides candidate menu/LOD/open model directories and SpecDB data leads.
These paths have **not** been observed in this particular volume. A public
GT6-derived master car list must not be used as a GT4 vehicle/model-code crosswalk.
No GT4FS implementation was imported or bulk extraction performed.

The lack of observed paths above describes slice 118. Slice 119 observes selected
car and SpecDB directory leads below; their payloads remain unread.

Local decomp research identifies a `ShowRoomCar` class and generic model/camera
functions, but does not link Ford Ka '01 or any other named car to an asset/live
instance. The decomp executable revision/CORE correspondence does not establish
all disc-asset identities. Model count/table/command offsets in
[Render Contracts](RENDER-CONTRACTS.md) are executable-level evidence, not a
validation of any selected MDLS payload.

## Next bounded extraction contract

1. Calibrate a read-only RoFS page reader against bounds, version and real
   decompressed page structure. Specify allocation/output limits and independent
   synthetic malformed-page controls before extracting any resource.
2. Traverse only the catalogue paths needed for one menu car and its GT4 v2.00
   SpecDB mapping; record model code, row/colour identity, archive node and offsets.
3. Extract only the selected model and referenced texture/material/colour/wheel
   entries into a new ignored capsule, with input/output hashes and strict limits.
4. Validate MDLS fields/relocations, geometry/texture/LOD conventions and lifetimes.
   Then correlate with one owner-controlled live instance/camera observation.

Ford Ka '01 is a useful owner-observed candidate, **not an identified asset**.
P1 remains in progress; the concrete model, material, camera, live instance and
complete state/VM/effect contracts remain unqualified. A decoded offline asset
alone would still not establish live rendering replacement.

## Slice 119 — Reviewed catalogue experiment

**Confirmed:** Luna reverified the full ISO hash against its existing pin before
catalogue research. Six reference checkouts were cloned and pinned. No reference
code was linked, no extractor CLI was invoked and no asset payload was read.
Worker and parent inspected the same ToC interval: **365,622 unique bytes** in
union, excluding full-ISO hash streaming. Decoded pages occupy **833,536 bytes**,
below the 2 MiB output cap.

The codec is raw DEFLATE inflation **before** XOR `0x55` of decoded bytes.
Each stored span produces exactly 2,048 bytes with no unused/unconsumed input.
Of 407 pages, 406 lack a final-stream marker and end in the observed empty
stored-block flush suffix `00 00 FF FF`; one has a final marker. An independently
specified synthetic `Z_SYNC_FLUSH` segment reproduces this non-final framing.
The exact producer flush mode and complete DEFLATE bitstream grammar remain
unqualified. Requiring `eof` for every page rejects this input; accepting arbitrary
truncation would also be incorrect.

The packed little-endian u32 version at header `0x04` is `0x00030001`, matching
GT4FS's `Version3_1` constant. The little-endian u16 values at `0x04/0x06` are
1/3; the published specification labels those fields in the opposite major/minor
order. Earlier wording is not evidence of a distinct format version. Frozen
slice 118 raw observations remain unchanged.

| Observation | Reviewed result |
|---|---|
| Stored ToC SHA-256 | `c308312caf6cb2e0fed42158c5463bdb353d35ac8e773ef55fa4f8634f397d9b` |
| Decoded pages SHA-256 | `04368b3d5bd5b6a355e532375e03873051235d2445c5abfaa189918c48862afb` |
| Page types | 6 index pages, 401 record pages |
| Records / directories / files | 18,562 / 210 / 18,352 |
| Directory traversal | All records reached from sole `.` root; no missing parents, duplicate directory IDs or cycles observed |
| Packed file extents | All 18,352 fit within VOL; payload bytes read: zero |
| Selected paths | `./car/menu` (807 direct files), `./car/lod` (1,614), `./specdb/GT4_US2560` (169) |
| Identity leads | `CAR_NAME_american.dbt` and `.idi` are present; contents unread |

The parent separately implemented contiguous-window decompression, leaf framing,
directory traversal and file-bound checks; its hash/counts and selected paths
agree with Luna. Both use host zlib and published format facts, not an independent
PS2 execution/fidelity oracle. Ten worker synthetic controls cover final/non-final
framing, incorrect XOR order, middle/tail truncation, output cap, page/file extents,
child-page range and record bounds. The parent reruns the private experiment and
these controls before recording qualification.

**Unknown:** index-key search/routing semantics, reciprocal page-link ordering,
all malformed-input cases, selected payload correctness, named-car/model-code
mapping and live model/camera state. This experiment enumerates record pages,
not a production B-tree lookup implementation. Only metadata and selected research
leads are published; complete filenames, private scripts and game bytes are not.

Next: review a maintainable read-only reader with public synthetic controls, then
extract only USA v2.00 identity resources needed for one-car mapping under a fresh
capped extraction contract. MDLS/texture parsing and live-hook qualification follow
that mapping. P1 remains in progress; no native car drawing or P2/P3 renderer was
implemented.

## Slice 120 — Selected extraction and offline named-car identity

**Confirmed:** maintainable [volume tooling](VOLUME-READER.md) reproduces both
slice 119 catalogue hashes/counts and validates a fresh five-file extraction.
The whole ISO pin is verified before every extraction/metadata read, then the
same handle supplies ISO9660/catalogue/payload data. Selected SpecDB payloads
total **70,560 stored bytes / 159,549 decoded bytes**, all kind 2 PS2ZIP with
matching complements, bounded raw DEFLATE, EOF, exact sizes and no trailing input.
Hashes/extents/methods are in [slice 120 metadata](inputs/slice120-car-identity.json).

Luna's private IDI/DBT/SDB probe establishes:

| Selected identity | Result |
|---|---|
| USA folder | `GT4_US2560` |
| GENERIC_CAR / CAR_NAME_american label | `ka_01` in both |
| Shared row ID | 59 |
| IDI table IDs / paired counts | 0 / 722 and 39 / 804; IDI and paired DBT code sets agree |
| Selected row sizes | 24 and 16 bytes |
| Name / Grade / ShortName SDB indexes | 84 / 84 / 84 |
| Display name | Ford Ka '01; stored ASCII uses a literal backtick before 01 |
| NarrationID | Raw integer 435; **not** interpreted as an SDB index |

The parent independently decodes the two rows using bit-by-bit lookup in the
complete Huffman code list, rather than the worker's eight-bit acceleration
table. Both row SHA-256 values, label/code/table IDs and the exact selected ASCII
string agree. One hand-authored dictionary control passes; replaying the frozen
worker probe also reproduces results and its five synthetic type-0/type-1/type-2,
missing-row and truncated-stream controls. Format sources are still shared:
this is independent implementation evidence, not independent PS2 fidelity.
The SpecDB probe remains private; general endian/charset/compression/malformed
coverage is not qualified. No full table row data or game payload is published.

**Unknown:** actual model filename. Neither menu nor LOD has an exact `ka_01`
basename. Eleven Ford-prefixed menu leaves are candidate leads only. MODEL_INFO
IDI/DBT is absent in this GT4 folder; that source-schema assumption cannot supply
the missing link. An attempted explicit MODEL_INFO selection fails before any
payload reads/output creation. DEFAULT_PARTS schema describes equipment/component
keys, not a demonstrated model-filename bridge; no speculative extra extraction.

Source-only CAR4/MDLS/Tex1 header research identifies nested relative offsets,
minimum header sizes and per-color texture indirection. No model or texture
payload was read, no parser adopted and no geometry/live rendering qualified.
Next: map the executable asset-loader car code/label to the actual menu/LOD
basename, then qualify a single container/model/texture reader with public
independent fixtures. A reviewed header-only candidate probe needs a justified
target and its own fresh extraction contract. Do not guess a car from its prefix.

Public reader controls pass 22/22. Baseline candidate CTest passes 3/3 in 11.79 s;
Python discovery 81/81 with zero skips. No new native build was needed. P1 stays
open; runtime, game session, saves/locks, OpenCode branch, input, timing and
performance are unchanged.

## Slice 121 — Exact file map and serialized main headers

[Model Asset Probe](MODEL-ASSET-PROBE.md) and
[safe metadata](inputs/slice121-model-file-map.json) document the completed
offline CAR_VARIATION → VARIATION → ModelCode join. All 11 matching physical
variations identify `fdeu0006`; independent replay matches 12 selected row hashes.
Five selected database resources and two exact menu/LOD assets are extracted
into a fresh capsule, with whole-ISO and strict PS2ZIP verification.

Public original header tooling and independent byte-level review agree on both
CAR4/MDLS layouts, nested texture slots and their bounded headers. Actual files
contain signed -1 VM words and five zero-size texture slots: record them without
inventing runtime semantics. Seventeen public controls pass; CTest 3/3 and
Python 98/98, zero skips. No native rebuild needed.

The executable path builder and runtime SDB route are mapped, but the live car
getter → index argument link remains Unknown. Geometry, material/texture record
bodies, pixels, relocation, camera and live ownership remain open. No runtime,
game session, OpenCode, performance or input behavior changed. Next bounded
package inventories fixed shape/material/texture records on this exact pair.

## Slice 122 — Fixed records before packet semantics

[Model Records](MODEL-RECORDS.md) bounds and hashes the selected pair's fixed
shape/descriptors, materials and PGLU/transfer records. Garage/race: 22/113 shapes,
154/312 descriptors, 14/18 materials, 49/123 textures and 5/15 transfers. All
selected packets have nonzero QWC and aligned starts; raw shape+4 differs in
the racing file and is not treated as a size. No transfer pixels or VIF command
bytes are decoded. Field-level independent review is in the fresh capsule.

Eleven new public controls; CTest3/3 in11.02s, Python109/109, zero skips. No
native rebuild, owner session or runtime change. Next is bounded VIF command
framing, followed by separately qualified geometry and texture conventions.

## Slice 123 — Conditional VIF spans

[VIF Framing](VIF-FRAMING.md) reports exact-end command spans for all 466 selected
packets under caller-supplied CL/WL=(1,1). Incoming live cycle remains Unknown:
there is no STCYCL inside these spans. Eleven new controls; CTest 3/3 in 10.75 s,
Python 120/120, zero skips. Worker cycle/V4-5 mistakes corrected against pinned
donor source before integration. Packed/zero-cycle/unknown forms reject.
No VU/GIF execution, geometry/pixel decoder, live hook or runtime change.

## Slices 124–125 — Static state and controlled raw inputs

[Static state research](VIF-STATE-CONTRACT.md) confirms buffered cycle words,
shape argument-to-F12 flow and conditional matrix/buffer effects. Channel,
live cycle, relocation and selected ownership remain Unknown.

[Raw Lane Extraction](RAW-CHANNEL-LANES.md) now reads 157,441 scalar integer
inputs from 2,320 ordinary commands under explicit framing/signedness assumptions.
Literal controls protect signed narrow inputs, u32 bit preservation, grouping,
opaque padding and strict extents. No vertices/topology or VU results inferred.
CTest 3/3 in 12.24 s; Python 131/131 with zero skips. No native rebuild needed.
Next qualify candidate channel-selection coverage before semantic conversion.
