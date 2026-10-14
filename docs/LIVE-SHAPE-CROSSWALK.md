# Live shape / offline asset crosswalk

Slice 140 connects the first owner capture to the immutable Ford Ka '01 asset
pair studied in slices 121/122. It changes no runtime or rendering behavior.
See [published evidence](inputs/slice140-shape-crosswalk.json),
[capture contract](SCENE-OBSERVATION.md) and
[serialized records](MODEL-RECORDS.md).

## Result and confidence

**Confirmed:** one candidate translation aligns 18 distinct captured shape
arguments with 18 of the 22 serialized shapes in `car/menu/fdeu0006`.
For each pair, the opaque word at shape+4 and u16 descriptor count at shape+0xA
match, and `guest_argument0 - serialized_MDLS_relative_shape_offset` equals
`0x01748740`. These addresses account for 97 of 256 completed call records.
The other 12 distinct captured addresses account for 159 calls and lie outside
this candidate MDLS interval. No conflicting captured address lies inside it.

There are 54 menu candidate translations in total. The other 53 each explain
only one distinct address. The race-LOD asset produces 60 candidates, each
explaining only one address. The race comparison is a negative control for this
specific shared-layout hypothesis; it does not prove that race assets are absent
from the scene or that the method works for every load/relocation strategy.

**High confidence:** this cluster is the loaded menu Ford Ka shape arrangement.
The owner screenshot supplies compatible Arcade-selection context. Neither the
screenshot nor this layout comparison proves object ownership by itself.

**Unknown:** live resource identity/lifetime, relocated descriptor addresses,
packet content, car-part names, camera/model transforms, submission roles and
geometry. This is an asset association candidate, not renderer-ready data.

| Serialized shape index | Captured argument0 | Descriptor count | Calls |
|---:|---|---:|---:|
| 1 | `0x0174C570` | 9 | 4 |
| 3 | `0x01750ED0` | 3 | 4 |
| 5 | `0x01752730` | 7 | 4 |
| 6 | `0x01754850` | 6 | 4 |
| 7 | `0x017562B0` | 1 | 4 |
| 8 | `0x01756370` | 3 | 4 |
| 9 | `0x01757560` | 1 | 4 |
| 10 | `0x017577B0` | 37 | 4 |
| 11 | `0x01765830` | 14 | 4 |
| 12 | `0x0176B090` | 15 | 4 |
| 13 | `0x0176F300` | 8 | 4 |
| 14 | `0x01772880` | 6 | 4 |
| 15 | `0x01774EC0` | 6 | 4 |
| 16 | `0x01776FC0` | 3 | 4 |
| 17 | `0x01777340` | 1 | 4 |
| 18 | `0x01777430` | 1 | 4 |
| 19 | `0x01777540` | 18 | 29 |
| 20 | `0x0177B900` | 1 | 4 |

Indices 0, 2, 4 and 21 have no captured address at their predicted position.
Their absence in this bounded sample does not establish invisibility, culling,
unused content or a particular material/pass. Indices are serialized records,
not identified body, wheel or window components. Calls are not frame counts.

## Method and controls

The parent rechecks exact input sizes/SHA256 before parsing. It rebuilds the
offline inventory through the existing bounded `gt4_model_records` API; Luna
independently parses the fixed fields directly from the pinned binary pair and
checks the frozen slice122 inventory. No game extraction or process-memory read.

For each original argument0, both before/after samples across all occurrences
must agree on the two-field signature. All 30 agree in this capture, and all
opaque words are zero. That zero value alone is therefore a weak discriminator.
Menu counts alone give 19 matching addresses, but only six have a unique shape
candidate. One-address/signature matches are retained rather than called proof.

Enumerate every signature-compatible disk/live pair, subtract the disk shape
offset without wrapping, and group pairs by candidate MDLS base. Require the
candidate declared MDLS extent to fit 32 MiB RAM. Rank by **distinct addresses**,
not call frequency or offset aliases; preserve all tied candidates. No threshold
automatically certifies identity. The full extent is hypothetical, not sampled.

Ten authored controls cover the known translation, multi-address score, changed
word/count rejection, negative-base rejection, one-past/end-exact RAM bounds,
order independence, duplicate-offset scoring and retained ambiguity. Existing
runtime code and slice136 CTest7/7/Python164/164 qualification remain unchanged;
no build or runtime test was needed for this offline evidence slice.

## Next observation boundary

Extend the read-only entry observation to the bounded 16-byte shape prefix and
descriptor array, with explicit truncation/invalid-span reporting. Preserve the
old schema/capture and all lifecycle/default-off safeguards; qualify a new schema
and candidate separately before an owner-led capture. Do not embed the current
absolute base as a production identity or read live process RAM externally.

Compare immutable prefix fields, descriptor QWC/flags and the relationship
between runtime addresses and serialized offsets. The
[submission audit](VIF-STATE-CONTRACT.md) shows runtime descriptors use their
first word directly after masking, whereas disk packet offsets are shape-relative.
Do not add the shape base twice or assume relocation preserves packet contents.
Additional descriptor evidence may strengthen resource association; packet,
channel, transform and ownership contracts still need their own checks before
native drawing can replace the original path. No performance or timing conclusion.
