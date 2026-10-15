# 09 — Shuffles and GT4 effects

Games move data between colour channels by re-reading a 32-bit buffer through
a 16-bit view (**texture shuffle**) or an 8-bit indexed view through a palette
(**channel shuffle**). A texture-cache renderer holds the buffer as an RGBA8
image, not as GS bytes, so these idioms need dedicated handling. GT4 (and every
Polyphony title since GT2000) uses channel shuffles through a ramp CLUT for
screen brightness and fades, plus sprite feedback for lens flare (chapter 02.6).

## 9.1 Reinterpretation maps (Decision)

The swizzle library (chapter 01) provides, for any pair of formats (P_view,
P_store) with the same page byte size, a **reinterpretation map**: for each
position (u, v) inside one page of P_view, the pixel (x, y) inside the
corresponding page of P_store and the byte lane (or nibble, or 16-bit half)
that holds that view texel. Maps are computed at start-up by composing "address
of (u, v) in P_view" with "pixel at address in P_store" and uploaded as small
integer textures (one per pair used: T8←CT32, T4←CT32, CT16←CT32, T8←CT16,
CT32←Z32 layouts, …). CTest `hw_reinterp_maps` checks them against brute force
over the full page.

With these maps a shader can read a view texel **directly from a target at its
internal scale**: for view texel (u, v) and sub-sample (i, j) of the output
pixel, read the target at native pixel (x, y), same sub-sample (i, j), and
extract the lane. This keeps upscaled detail for reinterpretations that would
otherwise drop to 1x, and it is exact at 1x. It is used by the texture-shuffle
and channel-shuffle executors below and by the "index from colour" fast path of
chapter 04.5.

Known consequences (Confirmed facts, derived from chapter 01):

- T8 over CT32: one CT32 page (64x32) = one T8 page (128x64); inside a
  16x16 T8 block, `(u & 8, v & 2)` selects the lane: (0,0) R, (0,2) G, (8,0) B,
  (8,2) A.
- CT16 over CT32: each 32-bit pixel is two 16-bit pixels; inside every 16-wide
  column group the view pixels at x mod 16 in 0..7 are the low halves (R, G) and
  8..15 the high halves (B, A) of the same eight 32-bit pixels; a 16-bit page is
  64x64 against 64x32.

## 9.2 Texture shuffles (16-bit view of 32-bit data) (Confirmed behaviour)

**Bit mapping**: a low half (R, G bytes) read as RGBA5551 gives r5 = R bits
0..4, g5 = R bits 5..7 and G bits 0..1, b5 = G bits 2..6, a1 = G bit 7; the high
half does the same with B and A. FBMSK conversion from 32 to 16 bits: 16-bit
bits 0..4 ← 32-bit bits 3..7, 5..9 ← 11..15, 10..14 ← 19..23, 15 ← 31; the
16-bit mask applies to both halves (its low byte covers R in the low half and B
in the high half, its high byte G and A).

**Detection** (all must hold): TME = 1; 16-bit frame; sprites, or triangles
forming quads in multiples of 6 indices; the first quad has x width = u width,
a multiple of 8 (two-pixel special case excepted); then the x-to-u offset modulo
16 classifies:

| Type | Condition | Meaning |
|---|---|---|
| copy | offset 0, next quad adjacent or 8 apart, FBMSK of form 0…01…1, texture ≠ frame, opaque, nearest | same-channel copy |
| offset | offset 8, no region repeat | RG ↔ BA |
| region-repeat-8 | WMS region repeat with mask bit 3 clear or fix = 8 | u bit 3 selects RG or BA source |
| region-repeat-16 | mask 0xF, fix low nibble 0, 16-wide quad at x = fix | |
| reverse | offset 0, width 16, x and u in opposite directions | swap halves |
| swizzle | 8x8 quads with x and u on swapped axes (FBMSK 0xFFFF for the 32-bit-texture variant) | usually B → A |

A second check after lookups: the frame target and the source must be 32-bit
targets (or the texture is the frame); otherwise, if the texture is not the
frame, the draw is opaque, nearest and FBMSK is a low-ones mask, it is a "real
16-bit source" draw; otherwise not a shuffle.

**Channel set**: RG written unless the 16-bit mask's low byte is 0xFF, BA
unless the high byte is 0xFF; 8-wide strips write only RG (x mod 16 = 0) or BA
(x mod 16 = 8); swapped halves map R→B, B→R, G→A, A→G; recursive draws (texture
= frame) drop identity copies.

**Execution** (one GPU draw replaces the batch):

- One quad over the union rectangle, snapped to 8-pixel columns then aligned to
  16; x (with u) or y (with v) halved because 16-bit coordinates cover twice
  the 32-bit area (swizzle types halve y when FBW is twice the target width,
  else x; real-16 sources keep coordinates).
- Region modes become clamp/repeat; no half-pixel adjustment.
- Shader, per 32-bit output pixel, reading the source pixel through the
  CT16←CT32 map: decode the chosen half to RGBA5551 expanded (alpha via TEXA:
  TA1 if the A bit, else TA0, or 0 with AEM and RGB = 0); run TFX, fog and blend
  on that 16-bit colour (Cd decoded the same way); re-encode: low byte =
  r5 | (g5 & 7) << 5, high byte = (g5 >> 3) | b5 << 2 | a << 7; place in R,G or
  B,A per the channel set; write mask from the 16-bit FBMSK. Within a shuffle,
  DATE reads G (the 16-bit A bit of the RG half is G bit 7).
- The target's alpha range becomes TEXA-derived and alpha scaling is undone.
- **Split shuffles**: when the next batch repeats the registers with FBP and
  TBP0 advanced by the pages just covered, skip intermediate batches and run one
  combined draw at the end.

Vector V-09-1: 16-bit sprite 16 wide, u = x + 8, FBMSK = 0x3FFF (only the
16-bit high byte written: G/A) → destination A equals source G for every pixel.

## 9.3 Channel shuffles (8-bit view through a palette) (Confirmed behaviour)

**Candidate**: TME, TEX0.PSM = T8, sprites; not a tiny draw (≤ 2 vertices within
8x8); and either the draw is within one page (≤ 64x64) with the first sprite
8 wide or 2 tall, or a region clamp/repeat mode is used; or it is one page high
and wider, with TBW = 2·FBW and page aligned.

**Channel choice**:

1. WMS region repeat with fix bit 3 set: B or A (A unless WMT is not region
   repeat and v bit 5 is clear, or WMT is region repeat with fix bit 1 clear).
2. WMS region repeat with mask bit 3 clear: R or G (G when WMT region repeat
   with fix bit 1 set, or integer coordinates with v bit 5 set).
3. Otherwise the minimum (u mod 16, v mod 4) of the first sprite per the lane
   rule of 9.1.
4. A 16-bit depth-like source target: depth bytes through the palette (two
   known variants; implement only if GT4 shows one — counter).

**Execution (Decision)**: a run of consecutive candidate batches with the same
FBMSK, the same TEX0/CLUT identity and FRAME/texture bases advancing within the
target is **coalesced**: the first batch of the run is replaced by one draw
covering the union of the run's page-aligned rectangles (or the whole target
when the run covers it), and later batches of the run are skipped. The draw's
shader fetches, for each output pixel, the selected lane of the **same pixel**
of the source target through the reinterpretation map (no UV sampling), looks it
up in the palette (or for RGB-fetch mode looks up R, G and B separately) and
writes per FBMSK. The run ends at the first non-matching batch, a CLUT load
that changes the palette, or a VBlank. Each run is verified in debug builds
against the GLM path at 1x (`GT4_HW_VERIFY_SHUFFLE=1`: execute both, compare,
count mismatches).

## 9.4 GT4 brightness and fade patterns (High)

The game database of the studied reference installs a Polyphony-specific hook
for GT2000, GT3, GT4 Prologue, GT4, GT4 Online and Tourist Trophy. Its observable
purpose (behaviour, not code):

- **Variant A — RGB tone curve** (FBMSK ≠ 0x00FFFFFF): the game extracts R of
  the frame into a page-sized temporary through a T8 view and a ramp CLUT, then
  G and B with additive blending into the same temporary, then copies it back;
  the CLUT base changes in the middle of the sequence, so naive per-batch
  translation creates wrong sources and offsets. Net effect per frame pixel:
  R' = CLUT[R].r, G' = CLUT[G].g, B' = CLUT[B].b, alpha unchanged.
- **Variant B — alpha stashes** (FBMSK = 0x00FFFFFF, only alpha written): R, G,
  B are extracted into the alpha bytes of three 32-bit buffers of half the
  display height (a few hundred batches later they are used to rebuild a faded
  image). Net effect: destination k (k = 0, 1, 2) alpha' = CLUT[source channel
  k].a, colour untouched. In NTSC the studied reference places them at blocks
  0x0, 0xA00, 0x1400 (for a 512-line PCRTC height). Our own register logs show
  GT4's 480i menu buffers at FBP 0x00 and 0x8C (blocks 0x0 and 0x1180), FBW 10,
  CT24, 224 lines: with H = 448, the stride would be 0x8C0 blocks, giving 0x0,
  0x8C0, 0x1180 — two display buffers and one more buffer in between
  (**Hypothesis**: GT4 stashes R, G, B in the free top bytes of CT24 buffers and
  reads them back, probably through T8H).

HWR policy (Decision):

1. Execute these sequences with the **generic** machinery: channel-shuffle runs
   (9.3), T8H "alpha as index" sources (chapter 04.5), CT24 targets that keep a
   valid alpha byte (channel validity, chapter 05), additive blending (chapter
   08). No address is hard-coded.
2. Add a **pattern HLE** only if the generic path fails the validation of
   chapter 14 (scene S-07 fade, S-08 race start) or is too slow: detect the run
   as in 9.3 with "CLUT base changed mid-run" allowed, compute the net effect of
   variant A in place on the source target (one pass, RGB write mask, reading the
   pre-pass values), and for variant B write the three alpha stashes into the
   targets the game's own FRAME registers name (never fixed addresses).
3. Either way, `GT4_HW_VERIFY_SHUFFLE=1` compares the result against the GLM
   path at 1x.

Owner-visible acceptance: race-start transition, menu fades and the in-race
brightness match the donor at 1x (chapter 14 thresholds) and show no blockiness
at 3x.

## 9.5 Lens flare and sun intensity (High)

GT4's engine needs sprite auto-flush (chapter 02.6): sprites that sample the
frame or depth buffer they write must see earlier sprites' results. HWR splits
such batches per primitive. The night-race measurements (GS wait ≈ 4.1–4.3 ms
per frame with the donor, steady 60 FPS with paraLLEl-GS page-tracked
readbacks) suggest the effect also reads a small area back to the EE
(**Hypothesis**, chapter 10 counters will show it). Scene S-05 (night race) is
the acceptance scene.

## 9.6 Other GT-family patterns

- **Striped double clear** and **double-half clear**: chapter 05.8.
- **Odd-width 4-bit uploads**: chapter 02.7.
- **Menu backgrounds**: T8 640x480 uploaded at DBP 0x1A40 (DBW 10), drawn with
  REGLIST quads (ST, RGBAQ, XYZF2), TEX0 T8 with TW 10, TH 9, CLUT at CBP 0x1EA8
  loaded through TEX2 with CLD 1 into 224-line CT24 field buffers (Confirmed in
  our captures) — exercises memory sources, index textures, CLUT loads and the
  interlaced output.

## Sources

- Research report `blending-date-shuffles-dither.md` sections 5, 6, 10 and test
  vectors 7, 8.
- Research report `gt4-gamedb-hacks-donor.md` sections 1 and 2 (game database
  chain, the Polyphony hook, variants A and B, our register logs) and 3.2.
- Research report `upscaling-clut-readbacks.md` A.14, B.8.
- Research report `texture-cache-v2.md` section 7 (shuffle-related cache rules).
- Research report `draw-translation-ubershader.md` 1.1 item 4 (channel-shuffle
  coalescing).
- Research report `gs-state-gif-flush.md` 1.9 (shuffle exclusions from
  auto-flush).
- Donor renderer (MIT) swizzle tables for the reinterpretation maps.
