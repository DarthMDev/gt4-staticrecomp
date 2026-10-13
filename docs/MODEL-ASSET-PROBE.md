# One-car model mapping and serialized header probe

Slice 121 identifies the offline Ford Ka '01 asset pair and adds an original,
bounded Python header inventory. This is P1 groundwork, not a native car renderer.
Evidence: [slice 121 metadata](inputs/slice121-model-file-map.json).

## Confirmed offline identity

The pinned USA `GT4_US2560` database supplies this exact chain:

| Join | Observed value |
|---|---|
| Label / car ID | `ka_01` / 59 |
| CAR_VARIATION_american IDI table | 41 |
| Selected 8-byte row's DBKey | Row 444, table 42; LE row first, table second |
| VARIATIONamerican label | `fdeu0006_0` |
| Matching physical variation rows | 11, indexes 1708–1718, each 52 bytes |
| Every selected ModelCode string index | 1598 in `american_StrDB.sdb` |
| Exact basename / catalogue matches | `fdeu0006`; `car/menu/fdeu0006`, `car/lod/fdeu0006` |

Repeated DBT row IDs are valid here: VARIATION has 3,849 physical rows and 722
unique IDs. Preserve physical order and each next-row compressed extent. A
dictionary keyed only by row ID would silently lose variations. The ModelCode
does not follow the guessed Ford prefix from slice 120; those historical leaves
remain unassigned. No MODEL_INFO or DEFAULT_PARTS bridge was needed.

Luna's bounded private probe and the parent's independent bit-by-bit full
Huffman code-list implementation agree on all 12 selected row hashes, physical
indexes, model indexes and variation order. Eleven private controls cover literal,
template, patch, missing/truncated rows, hand-authored DBKeys and repeated IDs.
Format sources remain shared; this is not an independent PS2 fidelity oracle.

## Executable connection and its boundary

The pinned generated function `0x004541E0` constructs `car/menu/`, `car/open/`
or `car/lod/`, then concatenates a string supplied by `0x00447D18`. Its input
is a string-table/model index, not generic car ID 59. The helper chooses a
runtime-initialized SDB object at global `0x006235A8` plus decimal 480/484
(`0x1E0`/`0x1E4`) after selector 42, then calls `0x00449CD8`. The inspected
selector list contains 39, 40, 41 and 42. SDB lookup uses buffer+16, a relative
32-bit offset and a two-byte string prefix. Used generated units match frozen
slice 116 size/SHA pins; independent ELF disassembly confirms the prefix route.

**Unknown:** the live argument chain from the car-parameter getter through
variation selection to index 1598. The getter has state effects; do not call it
again merely to inspect it. Offline identity does not establish the current
guest object's lifetime, selected pass, camera, callback or VM behavior.

## Selected file observations

The existing volume tool verifies the whole unchanged ISO on the same open
handle before extraction. Five explicitly selected variation resources and
only these two exact car files go into a fresh ignored slice 121 capsule.
Strict PS2ZIP framing, EOF, lengths and hashes pass. No game bytes are published.

| Main serialized model | Garage asset | Racing LOD asset |
|---|---:|---:|
| Decoded CAR4 bytes | 561,864 | 487,112 |
| CAR4 main MDLS base | `0xC00` | `0xC00` |
| Declared MDLS bytes | 416,657 | 402,240 |
| Models / shapes / materials | 4 / 22 / 14 | 4 / 113 / 18 |
| Colors / texture slots per color | 1 / 6 | 1 / 6 |
| Nonzero-size Tex1 containers | 2 | 5 |
| Zero-size Tex1 headers | Slots 1–4 | Slot 4 |

All three serialized VM words in each inspected model are signed `-1`, with
the MDLS VM bytecode field zero. PDTools' model writer also defaults these
signed words to `-1`. They must not be treated as MDLS-relative unsigned pointers.
Their execution semantics remain Unknown and this probe does not dereference them.

The zero-size Tex1 headers have zero supported counts and subrecord pointers.
The probe reports **header only; slot semantics Unknown**, bounds their 0x30
header against MDLS, and rejects zero size with nonzero references. It never
invents a body extent or claims runtime-empty behavior. This observed subset
differs from an assumption that every valid Tex1 declares at least 0x30 bytes.

## Tool contract and controls

`scripts/gt4_model_probe.py` exposes `probe_asset(data: bytes)`; callers verify
their input first. It has no ISO access, game hook, relocation or rendering.
CAR4 and standalone MDLS are the only accepted root signatures, capped at
16 MiB. Fixed tables, bounds vectors, model command starts, shape starts and
nested per-color texture headers are checked before access. Declared nonzero
MDLS/Tex1 extents are bounded against their immediate parent.

**Unknown:** shape/command bodies, geometry, material record extents, VM words,
texture subrecords/pixels, other CAR4 containers and relocation. Material and
Tex1 subtable checks validate starts only; they are not full table validation.
Animation/unknown Tex1 words are reported as raw metadata, not executed pointers.

Seventeen hand-packed public controls exercise nested bases, per-color
indirection, shared headers, signed VM metadata, zero-size slots, truncation,
invalid signatures/extents/count caps and one-past-end pointers. Existing
baseline CTest passes 3/3; Python 98/98 with zero skips. No native rebuild needed.

Primary pinned format references: [GT4 model template](https://github.com/Nenkai/GT-File-Specifications-Documentation/blob/05e52347890541758222d173dbb62d2b40581b19/Formats/PS2/GT4/GT4_MDLS_ModelSet.bt),
[PDTools ModelSet2Model](https://github.com/Nenkai/PDTools/blob/ffe0bb26377ac9ff62f40951c1c0b16c5d9a380f/PDTools.Files/Models/PS2/ModelSet/ModelSet2Model.cs)
and [TextureSet1](https://github.com/Nenkai/PDTools/blob/ffe0bb26377ac9ff62f40951c1c0b16c5d9a380f/PDTools.Files/Textures/PS2/TextureSet1.cs).
This original probe uses format facts; no reference implementation is linked.

Next: bounded fixed shape/material/texture records on this exact car, then
geometry and coordinate conventions. Live ownership/camera/side-effect contracts
must pass before substituting game graphics. P0/P1 remain open; no performance,
input, timing, session, save or native renderer behavior changed.
