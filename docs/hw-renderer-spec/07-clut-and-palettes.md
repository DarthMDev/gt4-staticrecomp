# 07 — CLUT and palettes

The CLUT unit models the GS's 1 KiB palette buffer, loads it at TEX0/TEX2 write
time, decodes it for draws (applying TEXA for 16-bit palettes) and provides
GPU palette textures. GT4 relies on palettes for menu art (T8 backgrounds with
CLUT loads through TEX2, Confirmed in our captures), car and track textures,
and its brightness/fade passes (chapter 09).

## 7.1 Buffer model (Confirmed)

- 512 halfwords. A 32-bit entry i: low 16 bits at halfword i, high 16 bits at
  halfword i + 256. 16-bit entries: one halfword each.
- CPSM: CT32 (0), CT16 (2), CT16S (0xA); CT24 is stored as CT32 (TEX0.CPSM is
  masked with 0b1010). Any other CPSM: the load writes nothing (counter
  `clut_bad_cpsm`).
- Indexed PSMs: `(PSM & 7) ≥ 3` — T8 (256 entries), T8H (256), T4, T4HL, T4HH
  (16).

## 7.2 When a load happens (Confirmed)

Evaluated on every TEX0_1/2 and TEX2_1/2 write (TEX2 replaces only PSM, CBP,
CPSM, CSM, CSA, CLD of the context's TEX0, i.e. the TEX0 bits 20..25 and
37..63; it never triggers MTBA). Only when the resulting PSM is indexed;
otherwise nothing happens, and CBP0/CBP1 are **not** updated (a known game
depends on this).

| CLD | Load | Side effect |
|---|---|---|
| 0 | no | — |
| 1 | yes | — |
| 2 | yes | CBP0 := CBP |
| 3 | yes | CBP1 := CBP |
| 4 | only if CBP ≠ CBP0 | CBP0 := CBP |
| 5 | only if CBP ≠ CBP1 | CBP1 := CBP |
| 6, 7 | no (reserved; games write them) | — |

Vectors: CBP0 = 0x100, TEX0(CLD 4, CBP 0x100) → no load; CLD 4, CBP 0x200 →
load and CBP0 = 0x200; CLD 2 with a non-indexed PSM → CBP0 unchanged.

**Redundancy filter** (optimisation, Decision: implement, with an A/B switch
`GT4_HW_CLUT_ALWAYS_LOAD=1`): skip the copy when CBP, CPSM, CSM, CSA, the palette
size class and (for CSM2) TEXCLUT equal the last load and the CLUT source has not
been written since. "Written" covers: uploads and SLM moves whose page-rounded
block range overlaps [CBP, CBP + 3] of the pending load (wrap at 0x4000 handled);
batches whose frame writes overlap that range with the CLUT's bpp (marked "dirty
by draw"); GPU moves into it; resets.

## 7.3 Order of a load (normative)

1. If the pending batch is textured with an indexed format, or it writes the
   CLUT source ("dirty by draw"), flush it (it must render with the old
   palette, or its writes must land before the load). Commit pending uploads.
2. **Authority check** for the CLUT source rectangle (CSM1: 4 blocks of CPSM at
   CBP for 32-bit/T8, halved for 16-bit, halved again for 4-bit indices;
   CSM2: the row (COU·16 .. + entries, COV) at CBW): if a target is
   authoritative for those blocks, obtain the palette from the GPU (7.6, GPU
   CLUT) — Decision: HWR never reads stale SLM data for a palette.
3. Otherwise copy from the SLM into the CLUT buffer at the CSA offset.

Invariant: a draw sees the palette that existed at the TEX0 write that loaded
it; later writes to the palette memory do not affect it until the next load.
Test `hw_clut_snapshot`: upload palette A, TEX0 load, upload palette B, draw →
the draw uses A.

## 7.4 Memory layouts (Confirmed)

- **CSM1, 8-bit indices**: a 16x16 image at CBP (BW 1) in CPSM format; entry i
  at x = (i & 7) + 8·bit4(i), y = 2·(i >> 5) + bit3(i). With 32-bit CPSM and
  CSA ≠ 0 only the rows from group (CSA & 15) upward are loaded.
- **CSM1, 4-bit indices**: an 8x2 image; entry i at (i & 7, i >> 3).
- **CSM2**: a linear row of 256 or 16 pixels at (COU·16, COV) in a buffer of
  width CBW (TEXCLUT), stored at the CSA offset (32-bit split into halves).
  Hardware allows only CT16; HWR also accepts CT32 and CT16S (counter).
- Vectors (CSM1, 256): i = 0 → (0,0); 7 → (7,0); 8 → (0,1); 16 → (8,0);
  24 → (8,1); 32 → (0,2); 255 → (15,15).

## 7.5 Decoding a palette for a draw (Confirmed)

Produces 256 or 16 RGBA8 entries, cached until the CLUT buffer, the CSA/CPSM/
size, or the relevant TEXA fields change.

- **32-bit, 8-bit index**: 16 groups of 16 entries; group g reads halfwords
  starting at min(g·16 + (CSA & 15)·16, 240) — it clamps to the last group, it
  does not wrap. Entry = low | (high << 16). Vector: CSA = 8 → groups 0..7 read
  8..15, groups 8..15 all read group 15.
- **32-bit, 4-bit index**: 16 entries at (CSA & 15)·16. A special swizzle
  applies when a draw uses CSM2 but the buffer was last loaded with CSM1 (one
  known game; counter `clut_csm_mismatch`, implement only if GT4 hits it).
- **16-bit (CT16/CT16S)**: entries from halfword CSA·16, expanded:
  R = (c & 0x1F) << 3, G = ((c >> 5) & 0x1F) << 3, B = ((c >> 10) & 0x1F) << 3,
  A = TA1 if bit 15 set else TA0; with AEM = 1 an entry whose **whole 16-bit
  value** is 0 gets A = 0.
- 32-bit palettes keep their stored alpha (CT24 is folded into CT32 at load).
- The palette's alpha min/max is computed lazily (used by alpha-test folding
  and blend simplification). A GPU-sourced palette has unknown alpha (0..255).

## 7.6 Palette objects and GPU textures (Decision)

- Palette objects are shared by content: two maps (16 and 256 entries) keyed by
  a 64-bit hash of the RGBA8 entries plus full compare; capacity 4096 each,
  unreferenced entries purged when full.
- Each object owns its entries, alpha range and, lazily, a 256x1 or 16x1 RGBA8
  GPU texture.
- Indexed memory sources are R8 index textures (4-bit indices expanded to 0..15;
  T8H/T4HL/T4HH extract the relevant bits of the 32-bit word); the palette is
  bound at draw time, so palette swaps never re-upload texture data.
- Sampling: chapter 03.5 (index = trunc(a·255.5), lookup by integer fetch,
  bilinear filters after lookup).

## 7.7 GPU CLUT: palette from a target (Decision: always on)

When the palette's source blocks are owned by a target (the game drew the
palette, or the palette is part of a rendered image as in GT4's brightness
ramps if they are drawn):

1. Locate the palette rectangle in the target: exact base (target BP = CBP, same
   format as CPSM, same width when CBW ≠ 0) or inside (CBP within the target,
   position by the surface-offset search; palettes spanning more than one block
   must keep the same 4-block alignment).
2. Reject if any dirty rectangle of the target overlaps it (the SLM is newer:
   then the SLM path applies to those blocks).
3. Run a small GPU pass that writes a persistent 256x1 or 16x1 "GPU CLUT"
   texture: for each palette entry k written at the CSA offset (note below),
   compute the CSM1 position of k (or the CSM2 linear
   position — Decision: HWR handles CSM2 with its linear layout, correcting a
   probable reference gap marked Hypothesis by the research team), add the
   palette's offset in the target, multiply by the target scale, take the
   top-left sub-sample (nearest), decode 16-bit entries with the current TEXA.
4. Regenerate only when the source target, the offset or the target's last
   batch number changes.
5. When the target is not in an expressible layout (e.g. palette written in a
   format different from CPSM), use the GLM: swizzle the target's blocks into
   the GLM and decode the palette from there on the GPU.

Note on CSA in the GPU CLUT pass: the destination offset is CSA·16 entries for
16-bit palettes and (CSA & 15)·16 for 32-bit ones, consistent with 7.5 (the
studied reference masks differently; the research team noted the masks look
swapped; HWR follows the hardware model of 7.1 and test V-07-1 checks a 16-bit
GPU CLUT with CSA = 17).

Draws using a GPU CLUT get an unknown alpha range and never share a cached
expanded texture.

## 7.8 CLUT draws (High)

A batch that writes the area of the next pending CLUT (small, flat, unblended,
inside one page, right edge exactly 8, 16, 32 or 64, frame bpp equal to the
CLUT bpp) is a "CLUT draw". HWR draws it on the GPU like any other draw; the
following load then takes the GPU CLUT path (7.7). No CPU rendering and no
readback is needed. Counter `clut_draws`.

## 7.9 Counters (F8)

CLUT loads requested, performed, skipped by the filter, from SLM, from GPU
target, via GLM; palette objects live; palette textures uploaded.

## Sources

- Research report `upscaling-clut-readbacks.md` part B (B.1 to B.8) — both the
  earlier and the final version.
- Research report `gs-state-gif-flush.md` section 3 (CLD logic, dirty tracking,
  notification rectangles) and 8.5 (palette storage order).
- Research report `texture-cache-v2.md` 1.5 and 3.12 (palette objects, GPU CLUT).
- Research report `texture-replacement.md` 1.7 (CLUT expansion details).
- Research report `gt4-gamedb-hacks-donor.md` 3.2 (GT4 menu CLUT loads via TEX2)
  and 4.1 (donor's CLUT slots).
- Research report `ps2tek-gs-facts.md` section 4 (CLUT buffer).
