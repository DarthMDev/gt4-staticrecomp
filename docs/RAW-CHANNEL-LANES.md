# Ordinary UNPACK raw integer lanes

Slice 125 adds original offline `scripts/gt4_raw_lanes.py`. It reads integer
values from already bounded ordinary UNPACK input, before assigning game channel
roles. [Slice 125 metadata](inputs/slice125-raw-channel-lanes.json) records
selected input hashes, assumptions, controls and independent comparisons.

## API and supported subset

`decode_integer_lanes(payload, component_bits=..., components=...,
source_vectors=..., signed=..., alignment=...)` uses keyword-only preconditions.
Arguments specify 8/16/32-bit lanes, 1–4 components per source vector, 0–256
source vectors after fill suppression, explicit bool signedness, and a separate
immutable alignment-byte span. All parameters and exact extents are checked
before allocating lane tuples. The output is ordered immutable integer tuples.

Narrow lanes sign-extend when requested and zero-extend otherwise. Every 32-bit
lane preserves its unsigned bit pattern under either signedness choice; it is
never converted to a floating-point value. This representation is raw input
metadata, not the result of executing VIF sign/mask/mode operations.

The payload must contain exactly count × components × width/8 bytes; alignment
length must be exactly the padding needed to reach four-byte alignment. Padding
contents are opaque and never become another lane. Caps are 1,024 lanes and
4,096 data bytes per call. Packed VL=3, unsupported widths, invalid types/counts,
short/extra input and wrong alignment reject explicitly. An empty result requires
an explicit zero source count and empty data/alignment; raw command NUM0 does
not mean zero source vectors.

The helper does not resolve TOPS or VU addresses, apply cycle filling again,
write VU memory, apply STMOD/MASK/ROW/COL, interpret GIF, normalize values, flip
UVs, assign channels or build topology. These remain separate contracts.

## Confirmed selected raw observations

The two exact immutable Ford Ka files match published decoded size/SHA pins.
Fresh framing reproduces every frozen slice 123 packet hash and command boundary
under explicit CL/WL=(1,1) before lane extraction. Narrow signedness is selected
from the donor's USN bit convention as an explicit assumption; it is not proof
of the live pipeline's geometry meaning.

| Selected input | UNPACK commands | 8-bit lanes | 16-bit lanes | 32-bit patterns |
|---|---:|---:|---:|---:|
| Garage `car/menu/fdeu0006` | 752 | 11,348 | 0 | 47,047 |
| Racing `car/lod/fdeu0006` | 1,568 | 50,075 | 47,763 | 1,208 |
| Total | 2,320 | 61,423 | 47,763 | 48,255 |

All 157,441 scalar input values stay private, along with ordered command arrays.
Only hashes/counts/derived metadata are published. Array hashes use ASCII compact
JSON nested integer arrays, comma/colon separators and no newline. They identify
the explicitly interpreted raw arrays, not underlying game geometry correctness.

**Unknown:** actual incoming state, channel roles, material/camera ownership,
float units, mask/mode effects, topology, normals, UV convention and VU results.
No position array or renderable mesh is qualified by reading these numbers.

## Controls and next boundary

Eleven public tests use literal byte patterns for signed/unsigned extrema,
32-bit float-looking pattern preservation, component grouping, nonzero padding
markers, exact extents, invalid parameter types, zero counts, immutable input,
work caps and the framing-to-lane fill/NUM0 boundary. Baseline CTest passes 3/3,
Python 131/131 with zero skips. No native rebuild or runtime behavior change.

Primary pinned MIT format sources:
[PDTools VIFCommand](https://github.com/Nenkai/PDTools/blob/ffe0bb26377ac9ff62f40951c1c0b16c5d9a380f/PDTools.Files/Models/PS2/VIFCommand.cs),
[model template](https://github.com/Nenkai/GT-File-Specifications-Documentation/blob/05e52347890541758222d173dbb62d2b40581b19/Formats/PS2/PS2ModelCommon.bt)
and the [donor VIF decoder](https://github.com/snuri00/ps2-web-recomp/blob/1b4808793a12c45125262fd2d5f50a9ec9f7c020/runtime/src/vif.cpp).
No source implementation copied or newly linked. Refer to
[VIF State Contract](VIF-STATE-CONTRACT.md) before deriving geometry from raw lanes.

Next compare candidate channel-selection rules against actual raw header/type
patterns, preserving missing/multiple roles. Establish the selected relocation,
buffer ownership and matrix/mode context before adopting channel or topology
conversion. No owner session, OpenCode, performance, input, saves or timing change.
