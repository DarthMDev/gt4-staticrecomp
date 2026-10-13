# Bounded fixed model records

Slice 122 extends the [one-car header work](MODEL-ASSET-PROBE.md) with original
offline `scripts/gt4_model_records.py`. Its `inventory_asset(bytes)` API consumes
separately verified CAR4/MDLS bytes. It does not execute VIF/VU, reconstruct
vertices, interpret material values, decode images or replace live graphics.

## Record contract

**High confidence from pinned primary sources, Confirmed on the selected pair:**

| Record | Bound and relative base |
|---|---|
| Shape prefix | 0x10 bytes inside the owning MDLS |
| VIF descriptor array | Count at shape+0xA; 8-byte records immediately after prefix |
| Packet extent | Shape base + relative offset; u16 QWC × 16 bytes inside MDLS |
| Material array | MDLS-relative offset; count × 0x50 |
| PGLU texture array | Tex1-relative offset; count × 0x28 |
| Transfer array | Tex1-relative offset; count × 0x0C |
| Transfer data offset | Tex1-relative start only; storage extent Unknown |

The shape word at +4 is opaque. PDTools' writer leaves its size write disabled;
the real racing asset also contains a nonzero value there. It cannot delimit
the shape. Bound each prefix, descriptor array and packet against MDLS instead.
Nonzero packets cannot overlap their own prefix/descriptors. Aliases between
packets are preserved; alignment is reported rather than imposed as a format rule.
Zero-QWC descriptors would access no packet bytes and preserve their raw offset
without interpretation. None were observed in this selected pair.

Descriptor flags use low 9 bits for texture and high 7 for material. Texture 0
means none, 1–510 means index minus one, and 511 means external; material 0 means
none, positive values mean index minus one. These are metadata, not proof of
which texture LOD, pass or material variation the game selects.

Materials and packed PGLU records are bounded and hashed, not semantically
decoded. Transfer records expose raw dimensions, format, buffer/block fields and
offsets. Even a known format does not justify reading width × height × bpp bytes:
transfers can be swizzled pieces of an atlas, rather than complete linear images.
Unknown formats are reported explicitly; no decoder accepts or executes them.
CLUT patches, animation and texture payloads remain outside this contract.

Caps: 16 MiB root input inherited from the header probe, 4,096 descriptors per
shape, 65,536 total descriptors and 16,384 texture/transfer records. Existing
header caps still apply. All traversed fixed arrays are bounded before access.

## Confirmed selected observations

Both immutable slice 121 payloads are matched against the public decoded
size/SHA pins before reading. Parent inventory and independent Luna fixed-record
parsing use fresh slice 122 outputs; no extraction or producer pin changes.

| Observation | Garage `fdeu0006` | Racing LOD `fdeu0006` |
|---|---:|---:|
| Shapes / descriptors | 22 / 154 | 113 / 312 |
| Sum of declared packet spans | 205,728 bytes | 167,104 bytes |
| Material records | 14 | 18 |
| PGLU texture / transfer records | 49 / 5 | 123 / 15 |
| Zero-QWC / unaligned packet starts | 0 / 0 | 0 / 0 |
| External-texture descriptor sentinel | 0 | 0 |
| Transfer format codes | 0 (PSMCT32) | 0 (PSMCT32) |
| Shape +4 raw values | 0 | 0 and 963,912,704 |

Packet byte sums are descriptor sums, not file coverage or GPU traffic measures.
These raw counts do not demonstrate correct topology, materials, pixels or
performance. See [slice 122 metadata](inputs/slice122-shape-records.json) for
hashes, independent comparisons and limits.

## Verification and next boundary

Eleven hand-authored public tests cover three relative bases, texture/material
sentinels, full table bounds, one-past-end packets, self-header overlap, aliases,
unaligned and zero-QWC metadata, unknown formats, zero-size slots and work caps.
Baseline CTest passes 3/3, Python 109/109 with zero skips. No native rebuild.

Primary sources are frozen MIT [PGLUshape](https://github.com/Nenkai/PDTools/blob/ffe0bb26377ac9ff62f40951c1c0b16c5d9a380f/PDTools.Files/Models/PS2/PGLUshape.cs),
[VIFDescriptor](https://github.com/Nenkai/PDTools/blob/ffe0bb26377ac9ff62f40951c1c0b16c5d9a380f/PDTools.Files/Models/PS2/VIFDescriptor.cs),
[PGLUmaterial](https://github.com/Nenkai/PDTools/blob/ffe0bb26377ac9ff62f40951c1c0b16c5d9a380f/PDTools.Files/Models/PS2/PGLUmaterial.cs),
[PGLUTexture](https://github.com/Nenkai/PDTools/blob/ffe0bb26377ac9ff62f40951c1c0b16c5d9a380f/PDTools.Files/Textures/PS2/PGLUTexture.cs)
and [GSTransfer](https://github.com/Nenkai/PDTools/blob/ffe0bb26377ac9ff62f40951c1c0b16c5d9a380f/PDTools.Files/Textures/PS2/TextureInfo.cs).
Only format facts are used; no implementation/dependency is copied or linked.

Next: qualify bounded VIF command framing on exact packet spans, then channel,
topology and coordinate contracts with independent fixtures. Material/CLUT/pixel
semantics and the live instance/camera/side-effect gates remain open. This slice
does not change runtime, owner session, OpenCode, timing, input or performance.

[Slice123 VIF framing](VIF-FRAMING.md) now bounds all selected packet commands
under an explicit initial-cycle assumption. Live cycle and geometry semantics
remain Unknown; the conditional scanner does not satisfy the next geometry gate.
