# One-Car Asset Research Ledger

2026-10-13, slice 118. Luna performed bounded read-only catalogue research while
the parent built EE candidates. The parent independently checked ISO directory,
real header and offset-table metadata. No game navigation, asset extraction or
live car observation was performed. **No named car is qualified yet.**

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

**Unknown:** the version discrepancy (observed 1.3, documented 3.1) and the exact
page codec/layout. Luna's bounded XOR and common zlib-mode attempts did not decode
a valid page. No directory node/path was recovered; monotonic offsets alone do
not validate a complete reader. The offset-table extent end above follows the
observed offsets and is distinct from an assumed header-plus-length convention.

The [GT4 folder documentation](https://github.com/Nenkai/gt-modding-hub/blob/b312010b54c89e774a6c23f5d37a9f6de8606812/docs/ps2/gt4/file_structure.md)
provides candidate menu/LOD/open model directories and SpecDB data leads.
These paths have **not** been observed in this particular volume. A public
GT6-derived master car list must not be used as a GT4 vehicle/model-code crosswalk.
No GT4FS implementation was imported or bulk extraction performed.

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
