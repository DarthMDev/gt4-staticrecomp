# Bounded GT4 Volume Reader

Slice 120 adds offline `scripts/gt4_volume.py` with hand-packed controls in
`tests/python/test_gt4_volume.py`. It supports the pinned USA v2.00 ISO only.

## Contract

The tool hashes the whole ISO against the unchanged disc pin before opening
ISO9660 on the same handle. It checks GT4.VOL size/position, then both stored
and decoded catalogue hashes against slice 119. Whole-ISO hashing is separate
from the 2 MiB input and decoded catalogue caps. It validates page framing,
record bounds/types, link/index ranges, sole root, unique siblings/directory
IDs, connected parent graph, cycles/depth and every file extent.

This enumerates leaves. B-tree routing, reciprocal link ordering, exhaustive
malformed-format coverage and an independent PS2 oracle remain **Unknown**.
Non-final ToC segments accept the pinned flush suffix after bounded raw DEFLATE,
then XOR decoded bytes by `0x55`; full DEFLATE grammar/producer flush policy
are not qualified. See [primary sources](NENKAI-REFERENCES.md).

Extraction requires explicit catalogue paths: eight maximum, 16 MiB per file
and 32 MiB aggregate, for both stored and decoded data. All selected extents
and payloads pass before output creation. Kind 1 supports exact-length raw data,
rejecting PS2ZIP markers as unqualified. Kind 2 supports PS2ZIP only: matching
size complement, bounded raw DEFLATE, exact output size, EOF and no trailing
input. Other codecs/combinations fail closed. This is a supported subset.

Output is a fresh ignored capsule below this checkout's `private/`. Traversal,
existing capsules and symlink/junction ancestors are rejected. Generated flat
filenames avoid using game paths as host write paths; file opens are exclusive.
This assumes a trusted local worktree, not hostile concurrent path replacement
or TOCTOU resistance. Failures preserve partial capsules; no old data is deleted.
Assets, complete catalogues and extracted payloads remain private.

## Usage

Choose an unused capsule each time:

```powershell
private/tooling-venv/Scripts/python.exe scripts/gt4_volume.py `
  --iso "Gran Turismo 4 (USA) (v2.00).iso" `
  --select specdb/GT4_US2560/GENERIC_CAR.idi `
  --select specdb/GT4_US2560/CAR_NAME_american.dbt `
  --select specdb/GT4_US2560/american_StrDB.sdb `
  --output private/new-one-car-identity
```

Private `manifest.json` maps flat filenames to catalogue paths and records
extents, kinds, sizes and stored/decoded SHA-256 hashes. No full catalogue is
written. Never use extraction capsules as mutable card/save directories.

## Evidence

**Confirmed:** public reader matches both slice 119 hashes and 407 pages,
18,562 records, 210 directories and 18,352 files. Five selected SpecDB resources
were extracted into a fresh capsule; all are kind 2 with validated PS2ZIP.
[Slice 120 metadata](inputs/slice120-car-identity.json) records hashes/extents
and subsequent identity evidence.

**Confirmed:** 22 synthetic tests pass; baseline-candidate CTest 3/3, Python
81/81, no skips. Tests cover framing, malformed headers/pages/records, codecs,
truncation/trailing input, caps, graphs, pins, selection and private output.
No runtime/shader/C++ change or new native build was needed. Offline extraction
does not qualify live car ownership, geometry/textures or drawing replacement.
P1 remains open; game sessions, cards, rendering, timing and input are unchanged.
