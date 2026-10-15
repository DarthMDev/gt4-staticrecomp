# 08 — Blending, destination alpha test, alpha test, FBMASK, dither

This chapter decides, per batch, how the GS per-pixel operations that read or
conditionally write the destination are realised on the GPU: fixed-function
blending, shader computation with a framebuffer read (feedback), or extra
passes. GT4's profile recommends the equivalent of "High" blending accuracy
(car showroom banding, car sheen; Confirmed in the studied game database), so
HWR's default is exact.

## 8.1 Reference arithmetic (Confirmed)

Per RGB channel, integers: `Cv = (((A − B)·C) >> 7) + D` (chapter 01.5), then
dither (16-bit frames, DTHE), then clamp or wrap (COLCLAMP), then 16-bit
truncation, FBA on alpha, FBMSK merge. Written alpha is As (FBA applied); the
blend uses As before FBA.

Normative test vectors (CTest `hw_blend_vectors`, run through the actual
shaders on a 1x target):

| # | Mode / inputs | Expected |
|---|---|---|
| 1 | (Cs−Cd)·As + Cd, Cs 200, Cd 100, As 0x40 | 150 |
| 2 | (Cs−0)·As + 0, Cs 200, As 0xFF | 398 → clamp 255, wrap 142 |
| 3 | (Cd−Cs)·As + 0, Cd 10, Cs 20, As 0x80 | −10 → clamp 0, wrap 246 |
| 4 | (Cd−Cs)·FIX + 0, Cd 0, Cs 1, FIX 0x81 | −129 >> 7 = −2 (arithmetic shift; Hypothesis V-01-9) → clamp 0, wrap 254 |
| 5 | Cs·As, Cs 1, As 0xFF | 1 (truncation; a UNORM blend unit would give 2) |
| 6 | 16-bit frame, DIMX −4, blended 0x13 | 0x08 |

## 8.2 Equation simplification (Confirmed behaviour, adopted)

Before choosing a method, rewrite the mode when batch statistics prove an
equivalent:

1. Cs provably 0 (all vertex colour 0, no fog, TFX not DECAL, and MODULATE or
   no texture or vertex alpha 0): every Cs selector becomes 0.
2. C = Ad and the target's alpha range is a single value: C := FIX with that
   value; 24-bit target: FIX = 128.
3. C = As and the batch's alpha range is a single value: C := FIX.
4. A = B, or B = D with C exactly 1.0: the result is D (D takes A's old
   selector in the second case).
5. C provably 0: the multiplied term vanishes.
6. COLCLAMP = 1, A = 0 (Cs) … and the result is provably ≤ 0: write 0.

Per-target alpha range tracking (Decision: required) updates after each draw
from the written alpha range, FBMSK, FBA, 16-bit rules (only 0 or 128) and
shuffles; when primitives overlap, the Ad range seen during blending includes
the batch's own written alpha.

## 8.3 Fixed-function realisability (Confirmed classification)

The 81 modes group as follows (a = the chosen C):

| Group | Modes | FF exact? |
|---|---|---|
| A = B (27) | result D: Cs, 0, or Cd (RGB write disabled) | yes |
| Non-recursive (8) | Cs·(1+a), Cs·a, Cs·(1−a), −Cs·a | computed fully in the shader; no Cd needed |
| Accumulate (6) | Cs·a ± Cd (a = As/FIX), Cs·Ad ± Cd | shader outputs trunc(Cs·a), blend adds/reverse-subtracts with factor ONE: bit exact on 32-bit targets with clamp; Ad variants need DST_ALPHA |
| Interpolate (6) | Cs·a + Cd·(1−a), Cd·a + Cs·(1−a), Ad variants | exact only up to rounding and only for a ≤ 0x80 |
| Subtract (6) | (Cs−Cd)·a, (Cd−Cs)·a | a ≤ 0x80 only |
| Impossible (7) | Cs·(1+a) − Cd·a, Cd·(1+a) − Cs·a, Cs·(1+Ad) | never (factor > 1 on one side) |
| Cd multiplied (18) | Cs ± Cd·a, Cd·(1+a), Cd·a, Cd·(1−a), −Cd·a | some exact for a ≤ 0x80; Cd·(1+a) and Cd·a reachable with a "DST_COLOR" trick |
| Source × Ad (3) | Cs·Ad, Cs·(1−Ad), −Cs·Ad | need DST_ALPHA |

**Ad on the GPU** (Confirmed): RGBA8 stores 0x80 as 0.502, so DST_ALPHA gives
half the GS factor. Two remedies:

- **Alpha-scaled targets**: a target whose alpha max ≤ 0x80 may store alpha
  divided by (128.25/255) so that 0x80 reads as ≈ 1.0; every reader must know
  (alpha read ×128 instead of ×255; DATE thresholds 254.5/255 instead of
  127.5/255; sampling corrected; conversions "de-scale" before copies,
  write-backs, GLM swizzles and reinterpretation). A target leaves the scaled
  state when alpha > 0x80 is written, alpha is partially masked, or a shuffle
  touches alpha. Conversion passes switch states when needed.
- **Shader blending** (reads Ad exactly from the target).

Decision: HWR implements alpha-scaled targets only from milestone M14
(optimisation); before that every Ad mode uses shader blending.

## 8.4 Blend methods and levels (Decision)

Methods per batch:

- **FF**: fixed-function blend state only.
- **ACC**: shader computes the truncated product, FF adds/subtracts Cd (needs
  COLCLAMP = 1 and a 32-bit target).
- **MIX**: shader computes the Cs part, FF the Cd part through a dual-source
  factor (approximate; uses a ±124/256 bias to mimic truncation). Allowed only
  at level `basic`.
- **SHADER**: the shader reads Cd (feedback, 8.8) and computes everything,
  including dither, wrap and 16-bit truncation; FF blending off.

`GT4_HW_BLEND` levels:

| Level | Rule |
|---|---|
| `basic` | SHADER only for non-recursive modes and when a barrier is needed anyway; ACC where exact; FF/MIX otherwise |
| `medium` | `basic` + SHADER for sprite batches with fewer than 100 draw-list groups, and for Ad modes and factors > 0x80 when the batch has no overlap |
| `high` (default) | `medium` + SHADER whenever FF/MIX would not be exact: any factor > 0x80 with A ≠ B, Ad modes on targets that are not alpha-scaled or whose alpha may exceed 0x80, 16-bit targets with a Cd-reading mode, PABE, COLCLAMP = 0 with possible overflow |
| `full` | SHADER for every Cd-reading mode (ACC kept only on 32-bit targets with clamp) |

At every level: PABE = 1 with ABE = 1 uses SHADER (pixels with As < 0x80 output
Cs unblended); dithering on a 16-bit target with blending uses SHADER (the
shader must add the dither before truncation). Counters per method.

## 8.5 Colour wrap (COLCLAMP = 0) (Confirmed behaviour, adopted)

1. No overflow possible (D = Cd with a ≤ 1, interpolations with a ≤ 1, Cs·a
   with a ≤ 1): ignore.
2. SHADER blending computes the full result and keeps the low 8 bits — used
   whenever the method is SHADER (default at `high`).
3. ACC into an **RGBA16 side target**: convert the target region into a
   `R16G16B16A16_UNORM` image (value·255/65535, round to even; alpha copied),
   blend with FF there (sums above 255 survive), resolve back with
   (floor(v·65535 + 0.5) AND 255)/255. Negative sums still clamp to 0 on this
   path (known limitation; only used at `basic`/`medium`). The side target
   persists over consecutive wrap draws to the same frame; it is resolved when a
   draw targets another frame, samples this frame, or the next batch is not a
   wrap draw to the same frame (look-ahead on the next context).

## 8.6 Alpha test (Confirmed)

**Static folding**: from the fragment alpha range, classify the test:

| Test | All pass | All fail |
|---|---|---|
| LESS | max < ref | min ≥ ref |
| LEQUAL | max ≤ ref | min > ref |
| EQUAL | min = max = ref | min > ref or max < ref |
| GEQUAL | min ≥ ref | max < ref |
| GREATER | min > ref | max ≤ ref |
| NOTEQUAL | min > ref or max < ref | min = max = ref |

All-fail folds AFAIL into masks: KEEP → mask colour and Z; FB_ONLY → mask Z;
ZB_ONLY → mask colour; RGB_ONLY → mask alpha and Z. Further: RGB_ONLY with alpha
masked → FB_ONLY; RGB_ONLY on a non-32-bit frame → FB_ONLY; nothing written at
all → NEVER; AFAIL's channels already all masked → test is a no-op.

**Shader comparison** (alpha is fractional after interpolation; ε = 2^-15):
LESS a ≤ ref − ε; LEQUAL a ≤ ref + 1 − ε; GEQUAL a ≥ ref − ε; GREATER
a ≥ ref + 1 − ε; EQUAL |a − ref| ≤ 0.5; NOTEQUAL |a − ref| ≥ 0.5. Inverted tests
for second passes: NEVER↔ALWAYS, LESS↔GEQUAL, LEQUAL↔GREATER, EQUAL↔NOTEQUAL.

**Methods** (first applicable):

| Method | When | Passes |
|---|---|---|
| NONE | ATE off / ALWAYS | — |
| KEEP | AFAIL = KEEP | discard failing fragments |
| NEVER | ATST = NEVER | single pass with AFAIL's masks, no test |
| FEEDBACK | a barrier is already needed, or framebuffer fetch exists, or `GT4_HW_ACCURATE_ATEST=1`, and depth feedback exists when needed | one pass: failing FB_ONLY fragments write the old depth (software depth); ZB_ONLY rewrite the old colour; RGB_ONLY rewrite the old alpha (and old depth if Z written) |
| SPLIT_FB_ONLY | FB_ONLY and Z independent | pass 1 colour for all fragments, no Z write, no test; pass 2 Z only for passing fragments |
| SPLIT_RGB_ONLY | RGB_ONLY, Z and RGB independent | pass 1 colour for all fragments, written alpha chosen by dual-source blending (src1 alpha 1 = pass, 0 = fail; alpha factors SRC1_ALPHA / ONE_MINUS_SRC1_ALPHA); pass 2 Z for passing fragments |
| PASS_THEN_FAIL | otherwise | pass 1 normal test with discard and full writes; pass 2 inverted test with only AFAIL's channels |

"Z independent": ZTST GEQUAL with constant Z, or ZTST ALWAYS, or no Z write, or
no overlap. "RGB independent": C ≠ Ad, or alpha not written, or no overlap.
If pass 1 writes nothing, only pass 2 runs; if neither writes, skip.

## 8.7 Destination alpha test (DATE) (Confirmed behaviour; method set is a Decision)

Early outcomes from the target's alpha range: DATM = 0 needs a test only if the
target max ≥ 0x80 (or overlapping writes could raise alpha to ≥ 0x80); if the
target min ≥ 0x80 every pixel fails (skip the draw). DATM = 1 symmetric. Passing
pixels narrow the Ad range for blending.

Methods (HWR phase 1 implements FULL, STENCIL_ONE and STENCIL; PRIM_ID is a
later optimisation, M14):

| Method | Mechanism | Exact when |
|---|---|---|
| FULL | shader reads destination alpha and discards; draw-list barriers when primitives overlap and alpha is written | always |
| STENCIL_ONE | stencil cleared to 1 over the draw area; test EQUAL 1, pass op sets 0 (first writer wins); with a cheap single barrier the shader also tests the pre-draw alpha | every write flips the pixel to "fail" (DATM = 1 with written alpha max < 0x80; DATM = 0 with min ≥ 0x80, or FBA forcing ≥ 0x80) |
| STENCIL | setup pass writes stencil 1 where the pre-draw alpha passes (threshold 127.5/255, 254.5/255 scaled); draw tests EQUAL 1 | alpha is not changed by the draw itself (alpha masked) or no overlap |
| PRIM_ID | R32 image of "first failing primitive id" (init from pre-draw alpha; geometry pre-pass with MIN blending writes the id of fragments whose output alpha would fail); main draw discards fragments with id > stored | in-order primitives without discards |

Selection: framebuffer fetch → FULL; no overlap → FULL with one barrier; shuffle
→ FULL; complex alpha test with overlap → FULL; FBA or the alpha range proves
single writes → STENCIL_ONE; alpha written and few primitives (sprite draw list
< 10 groups or < 30 indices) or a full barrier already needed → FULL; alpha
written otherwise → PRIM_ID when available, else FULL; alpha masked → FULL with
one barrier (pre-draw alpha is the truth) or STENCIL. Within texture shuffles the
test reads G instead of A (chapter 09). Vector V-08-1: DATM = 0, two overlapping
sprites, the first writes alpha 0x80 → the second is rejected where they
overlap (STENCIL gets it wrong by design; FULL and PRIM_ID right).

## 8.8 Framebuffer reads (feedback) and barriers (Decision)

Mechanisms, best first, detected at device creation:

1. `VK_EXT_rasterization_order_attachment_access`: read the attachment in the
   shader with raster-order guarantees; no barriers. (Whether the owner's
   RX 9060 XT driver exposes it is Unknown; logged at start.)
2. `VK_EXT_attachment_feedback_loop_layout`: the target stays in the
   feedback-loop layout and is sampled as a normal image with texel fetch; a
   by-region barrier (colour output → fragment shader read) before each
   draw-list group.
3. Input attachment in GENERAL layout with a subpass self-dependency and
   barriers inside the pass (fallback for drivers without 2).
4. Copy: copy the draw area (plus the sampled area when the texture is the
   target) into a temporary before the draw (no barrier support).

Barrier policy: **one barrier** before the draw when no fragment can read a
value written by the same draw (no overlap, or accepted hazard such as
unblended FBMASK where masked bits are constant); **full barriers** when
overlapping primitives read Cd: split by the draw list (chapter 02.10), one
barrier per group. Full barriers degrade to one barrier for shuffles and
non-overlapping draws.

Things that need colour feedback: SHADER blending reading Cd/Ad; software
FBMASK; FULL DATE; alpha-test FEEDBACK for ZB_ONLY/RGB_ONLY; texture = current
target at the same pixel. Depth feedback: software depth test, alpha-test
FEEDBACK for FB_ONLY/RGB_ONLY with Z.

**Texture equals the current colour target**:

- Read the target at the current pixel through feedback when the batch's
  texel-to-pixel mapping is 1:1 (|position − texcoord| < 1 texel at every
  corner, no bilinear) or it is a palette reinterpretation handled by chapter
  09, or region modes do not leave the drawn texel.
- Else, if the sampled and drawn rectangles do not intersect: sample the target
  directly after one barrier.
- Else: copy the sampled region (expanded by 1 texel for bilinear) to a
  temporary texture first.

**Texture equals the current depth target**: sample directly when Z is not
written and the device can test and sample depth simultaneously
(feedback-loop layout); otherwise copy.

## 8.9 FBMASK (Confirmed)

Effective mask = FBMSK AND the frame format's valid bits (32-bit all, 24-bit
RGB, 16-bit 0x80F8F8F8). Per 8-bit channel: fully masked → write mask off;
zero → normal; partial → **software FBMASK**: read the old pixel (feedback),
`out = (new AND NOT mask) OR (old AND mask)` per channel after blend, dither and
clamp. One barrier suffices when blending is off, or the partial mask is only
on alpha (masked bits are constant across overlapping primitives); otherwise
full barriers. All four channels masked → no colour output (DATE kept). A
partial alpha mask makes the tracked alpha range conservative and prevents
alpha scaling.

## 8.10 Dither (Confirmed; modes Decision)

Enabled when DTHE = 1, the frame is 16-bit and `GT4_HW_DITHER` ≠ `off`. Value
DIMX[y & 3][x & 3] (y, x in native pixels for `native`, scaled pixels for
`fine`) added to RGB before clamp/wrap and the 0xF8 truncation. Requires the
shader to control the final value, so blended 16-bit dithered draws use SHADER
(8.4). Counter `dither_on_32bit` (chapter 01 Hypothesis).

## 8.11 Alpha output (Confirmed)

16-bit destinations write 128 if As ≥ 128 else 0 (FBA forces 128); 32-bit with
FBA: alpha |= 0x80; the blend factor alpha is taken before FBA; AA1 without
coverage emulation forces alpha 128; alpha blend factors are ONE/ZERO (written
alpha = As), except SPLIT_RGB_ONLY.

## 8.12 Counters (F8)

Batches per blend method, per DATE method, per alpha-test method; barriers;
draw-list groups; feedback copies; colour-wrap side-target conversions and
resolves; software FBMASK batches.

## Sources

- Research report `blending-date-shuffles-dither.md` sections 0 to 4, 7 to 9, 11.
- Research report `draw-translation-ubershader.md` 2.8 to 2.13, 4, 5.3 and
  addendum A5 to A7.
- Research report `output-postfx-vulkan-perf.md` B.6/B.8 (feedback mechanisms,
  DATE options, colour clip).
- Research report `gt4-gamedb-hacks-donor.md` 1.3 (recommended blending High and
  why).
- Research report `ps2tek-gs-facts.md` section 3 (blend, dither, clamp facts).
