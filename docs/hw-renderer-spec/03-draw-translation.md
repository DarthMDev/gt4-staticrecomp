# 03 — Draw translation

The draw translator turns one flushed batch (vertices, indices, snapshot,
vertex trace) into GPU work: zero or more auxiliary passes, the main draw, and
optional second passes. This chapter covers the general flow, geometry,
texturing, depth and the pipeline/pass model. Blending, DATE, alpha test, FBMASK
and dither are in chapter 08; shuffles in chapter 09; target and source lookups
in chapters 04 and 05; scaling adjustments in chapter 06.

## 3.1 Per-batch sequence (Decision; the individual rules are Confirmed behaviour)

1. **Copy the snapshot** registers that the translator may simplify (TEX0,
   TEXA, CLAMP, TEST, FRAME, ZBUF, ALPHA) into a private draw description; all
   edits below apply to that copy.
2. **Reset artefact**: skip a batch that is a single black point at (0,0) with
   alpha 0 and constant colour when the current PRIM is no longer a point.
3. **DATE on CT24**: FRAME.PSM low nibble = CT24 and DATE = 1 → skip (nothing
   passes on hardware; chapter 01).
4. **Write masks**: fm = FBMSK restricted to the frame format's valid bits;
   zm = "no Z write" when ZMSK = 1, or ZTE = 0, or (ATE = 1, ATST = NEVER,
   AFAIL ≠ ZB_ONLY).
5. **Palette first**: for indexed textures obtain the current palette
   (chapter 07) so the texture alpha range is known.
6. **Alpha-test folding** (chapter 08.6): if the fragment alpha range proves
   all-pass or all-fail, turn ATE off and fold the outcome into fm/zm.
7. **AA1 without coverage support** (phase 1, Decision: HWR does not emulate
   AA1 coverage): fragment alpha of AA1 lines/triangles is the constant 128;
   decide the alpha test statically against 128.
8. **Nothing written**: "no colour" = DATE off and every channel masked (or a
   blend that leaves Cd unchanged with alpha masked); "no depth" = zm and the
   depth test always passes, or no colour and zm. If both: skip. If the depth
   test always passes or no depth: ZTST := ALWAYS.
9. **Draw rectangle**: vertex-trace min/max in pixels, plus 0.4, rounded to
   nearest, giving [x0, x1); if x0 = x1 extend by 1; same for y; intersect with
   the scissor [SCAX0, SCAX1+1)x[SCAY0, SCAY1+1); empty → skip. It is a
   conservative coverage estimate for sizing, clears and barriers, not for
   rasterization. Normative test table (position range → pixels):

   | Range | Pixels |
   |---|---|
   | −0.5..0 | 0..0 |
   | −0.5..0.5 | 0..0 |
   | 0..1 | 0..0 |
   | 0..1.5 | 0..1 |
   | 0.5..1.5 | 1..1 |
   | 0.5..1.75 | 1..1 |
   | 0.5..2.25 | 1..1 |
   | 0.5..2.5 | 1..2 |

10. **Texture needed?** Texturing is processed only when TME = 1, and dropped
    when neither texture colour nor texture alpha can influence the output
    (TFX/TCC, vertex colour max non-zero, blend uses Cs/As, alpha written or
    tested). A dropped texture uses the internal "no texture" function (output =
    vertex colour).
11. **Clear detection** (chapter 05.8) may turn the draw into a memory fill or a
    target clear and skip it.
12. **Shuffle detection** (chapter 09) and the GT4 effect patterns.
13. **Mip base level** (3.6).
14. **Lookups**: frame target, depth target, source (chapters 04, 05).
15. **Upscaling sprite fixes** (chapter 06).
16. **Method selection** (chapter 08): blend, DATE, alpha test, FBMASK, colour
    wrap, dither, feedback/barriers.
17. **Record** passes (3.10) and update cache bookkeeping (valid/drawn
    rectangles, alpha ranges, invalidation of sources overlapping the written
    frame and Z, type-exclusive invalidation, chapter 04/05).

## 3.2 Primitive expansion

Primitives arrive as four classes (points, lines, triangles, sprites) with
strips and fans already converted to lists (chapter 02).

| Class | 1x | Scale > 1 |
|---|---|---|
| Points | GPU point list, size 1 | Each point must cover a scale x scale block: GPU point size = scale when the device range allows, else vertex-shader expansion to a quad |
| Lines | GPU line list; Bresenham rasterization when `VK_EXT_line_rasterization` exists | Line width = scale when supported (`wideLines` and range), else expansion to a quad offset ±0.5 native pixel perpendicular to the line |
| Triangles | triangle list | triangle list |
| Sprites | expanded to two triangles | same |

**Sprite expansion** (Decision: vertex-shader expansion from a storage buffer
is the default; CPU expansion is the fallback for unsafe Q):

- Each sprite becomes 4 vertices / 6 indices from a fixed device-local index
  pattern (0,1,2, 1,2,3 per quad; 16383 quads pre-built).
- Corner c uses X and S/U of vertex (c & 1 ? second : first) and Y and T/V of
  vertex (c & 2 ? second : first); colour, Z, fog and Q always from the second
  vertex.
- Unsafe Q (chapter 02.9): expand on the CPU and pre-divide S and T by the
  second vertex's Q, setting Q = 1. Triangles whose Q is identical on all three
  vertices are pre-divided the same way.

**Flat shading** (IIP = 0, or constant colour): the GS uses the last vertex.
With `VK_EXT_provoking_vertex` (last-vertex mode) nothing is needed. Without it,
the batch is de-indexed and each primitive's vertices receive the last vertex's
colour (skipped when every primitive is already uniformly coloured; points and
sprites never need it). Required for correctness; test `hw_flat_provoking`.

## 3.3 Vertex data and the vertex stage

Vertex record (Decision; 32 bytes, matches the front end's record):

| Offset | Content | Vulkan format |
|---|---|---|
| 0 | S, T | 2 x float32 |
| 8 | R, G, B, A | 4 x uint8 (not normalized) |
| 12 | Q | float32 |
| 16 | X, Y (12.4, including XYOFFSET) | 2 x uint16 |
| 20 | Z | uint32 |
| 24 | U, V (12.4 texels) | 2 x uint16 |
| 28 | F in byte 0 | 4 x unorm8 |

Vertex stage behaviour:

- **Depth**: z = min(Zraw, max_depth); depth = float(z)·2^-32. max_depth is all
  ones unless the format clamp of 3.7 applies to flat Z.
- **Position**: P = (X − OFX)/16 in GS pixels. GS integer coordinate N must
  land on the centre of GPU pixel N (N + 0.5 in GPU terms), so that the GPU's
  top-left rule reproduces the GS rule start ≤ N < end. HWR (Decision) maps
  `gpu_x = scale·P + 0.5·scale_offset − ε` with ε = 1/320 of a native pixel
  (a tiny up-left bias that keeps exact-on-centre vertices on the inclusive
  side regardless of float rounding), where scale_offset depends on the
  half-pixel mode of chapter 06 (1x: 1, so N maps to N + 0.5). Y identical,
  with Vulkan's downward Y. W = 1, so GPU "perspective" interpolation equals
  screen-linear interpolation, matching the GS.
- Normative coverage test vectors at 1x, OFX = 0: edge range 0.0..2.0 covers
  pixels 0, 1; 0.5..1.5 covers 1; 0.0625..1.0 covers none.
- **Texture**: three interpolants — normalized UV for hardware sampling
  ((UV − tex_offset)·tex_scale with tex_scale = 1/(16·2^TW), 1/(16·2^TH));
  integer-style coordinates for shader-emulated sampling (FST: raw UV in 1/16
  texels; STQ: ST/tex_scale); and float S, T, Q for per-pixel division.
- **Colour** as 0..255 floats, flat or smooth by IIP; fog F/255.

## 3.4 Pixel stage: colour domain and texture function (Confirmed)

- All colour arithmetic is done on integer-valued floats in 0..255 and
  truncated where the GS truncates.
- Sampled texels are converted to integers with `trunc(t·255 + 0.05)`.
- TFX and TCC per chapter 01.5; MODULATE with all vertex colours exactly
  (128,128,128,128) may be compiled as DECAL (optimisation).
- Fog after TFX, before alpha test and blend, with the chapter 01 formula.
- Output alpha conversion: 16-bit destination: A = 128 if FBA = 1, else
  (A ≥ 128 ? 128 : 0); 32-bit destination with FBA = 1: A |= 0x80 (A < 128 →
  A + 128). The blend factor alpha is taken **before** FBA.
- 16-bit destination RGB: low 3 bits cleared (AND 0xF8) whenever the shader
  controls the final value (shader blending, dither, FBMASK, or no blending).
  With fixed-function blending into an RGBA8 target the low bits survive; that
  is a known inaccuracy accepted only at blend level `basic` (chapter 08).
- RGB then clamped to 0..255 (COLCLAMP = 1) or wrapped (chapter 08.4).

## 3.5 Texture sampling

### Coordinates

- FST = 1: normalized = UV/16 / 2^TW (and TH); Q unused.
- FST = 0: per pixel S/Q, T/Q.
- **Size remap**: when the source's real size differs from 2^TW x 2^TH (a
  target used as a texture), multiply the normalized coordinate by
  (2^TW / source unscaled width) (and height).
- **Sub-region**: when the source holds only a region of the texture (chapter
  04), `u' = (u − min/2^TW)·(2^TW / region width)`.
- **Region rectangle** (source is a sub-rectangle of a target): switch to
  integer texel fetches: wrap the normalized coordinate, convert to integer
  texels (·scaled size + rect origin), clamp into the rect (upper bound
  exclusive).

### Wrap modes: sampler or shader

Transformations, in order:

1. Source smaller than 2^TW (a target) with REPEAT that reaches the boundary:
   if coordinates never wrap back into data, use CLAMP; otherwise turn REPEAT
   into REGION_REPEAT with mask 2^TW − 1 and fix 0, so the wrap is computed in
   the shader against the declared size.
2. Redundant regions (sources from the SLM only): REGION_CLAMP with MIN = 0 and
   MAX ≥ size − 1 is CLAMP; REGION_REPEAT with mask = size − 1 and fix = 0 is
   REPEAT. When the source was already cut to the region, REGION_CLAMP becomes
   CLAMP and REGION_REPEAT becomes REPEAT.
3. Shuffles strip region modes (chapter 09).

| Effective mode | Sampler | Shader |
|---|---|---|
| REPEAT | repeat | — |
| CLAMP | clamp-to-edge | — |
| REPEAT/CLAMP with region rect | point | fract or clamp to [0,1], then integer rect clamp |
| REGION_CLAMP | point (shader filters) | clamp normalized coordinate to [(MIN + a)/size, (MAX + b)/size] |
| REGION_REPEAT | point (shader filters) | u_int = trunc(u·size); ((u_int AND MSK) OR FIX)/size; STQ coordinates are fract()'d first so negatives wrap |

Region-clamp margins (High, heuristic not hardware truth): a = 0.5, b = 0.1
texels; with half-pixel mode `native` and a source scale > 1: a = 1 + 0.1·scale,
b = 0.1 (avoids reading undrawn edge texels). Depth sources use raw MIN/MAX on
integer 1/16-texel coordinates.

**Shader-emulated sampling** (sampler forced to point, filter in the shader) is
used for: indexed sources sampled through a palette; target sources with a
16/24-bit colour format (TEXA expansion needed); any region wrap; depth-format
sources; region rectangles. Otherwise the hardware sampler filters.

### Filtering

- Linear or nearest per the vertex-trace decision (chapter 02.9), overridable by
  `GT4_HW_FILTER`: `ps2` (default, the GS decision), `nearest`, `forced`
  (bilinear everywhere except palettes, targets read as palettes, shuffles).
- Shader 4-tap bilinear: corners at st ± half texel, weights = fract(uv_lo·size)
  (clamped to [0, 0.9999999] for STQ), each corner through the wrap logic; for
  palettes the 4 indices are looked up first and the colours blended (GS
  filters after the CLUT lookup); TEXA expansion per tap before blending.

### Palette sampling

The index comes from an 8-bit index texture (R8_UNORM viewed so the value is
read as alpha, or read as R): index = trunc(a·255.5); T4HL uses the low nibble,
T4HH the high nibble. The palette is a 256x1 or 16x1 RGBA8 texture read with
integer fetches. For targets reinterpreted as T8H/T4HL/T4HH the index is the
target's alpha byte; forced bilinear is suppressed for them.

### TEXA in the shader (target sources only)

Sources decoded from the SLM have TEXA applied on upload. Sources that are
targets with a 24-bit format: A = TA0 unless (AEM and RGB = 0) → 0. 16-bit
format targets: A = TA1 if the stored alpha is ≥ 0.5 (the A bit), else TA0,
unless (AEM and the 5-bit-truncated RGB = 0) → 0.

### Depth targets as textures

32-bit: integer i = depth·2^32 split into four bytes (R = bits 0..7 …
A = bits 24..31). 16-bit: the low 16 bits split as 5:5:5:1 then expanded (×8
colour, ×128 alpha). Then TEXA/palette rules apply as for colour.

## 3.6 Mipmapping and LOD

Option `GT4_HW_MIPMAP=off|basic|full` (default `full`).

Base level for source lookup (High; also used by the pack hash, chapter 11):

- k = (K + 8) >> 4; mxl = min(MXL, 6).
- FST forces constant LOD (LCM = 1 behaviour).
- If the traced minimum LOD ≥ mxl: constant LOD = mxl.
- Constant LOD: base = max(k, 0).
- Otherwise: trilinear MMIN (bit 0 of MMIN set, i.e. 3 or 5): base =
  floor(min LOD); else base = round(min LOD + 0.0625), plus 0.01 when
  ceil(min LOD) < max LOD; max level = ceil(max LOD).
- Clamp both to [0, mxl]. Clamp bounds and coordinates are halved once per
  level of base.

Modes:

- `off`: sample level 0 only (sampler LOD clamp 0; maxLod 0.25).
- `basic`: GPU-generated mip chain from the base level (successive linear
  blits), hardware LOD selection with the MMIN-derived mip filter.
- `full`: every level uploaded from the SLM (MIPTBP1/2, TW/TH reduced), sampled
  at an explicit LOD computed in the shader:
  lod = min(K/16 − log2(|Q|)·2^L, max level) − base level (LCM = 0) or
  K/16 − base (LCM = 1). Never applied to target sources or palettes.

Draws with mipmapping are "mip draws" when MXL > 0, MMIN ∈ 2..5 and max LOD > 0.

## 3.7 Depth

- Depth image: `D32_SFLOAT` (or `D32_SFLOAT_S8_UINT` when a stencil DATE method
  is in use), one format for all GS Z formats; depth = Z·2^-32; clear value 0.
- Compare: NEVER, ALWAYS, GEQUAL → GREATER_OR_EQUAL, GREATER → GREATER.
  Test enabled when ZTST ≠ ALWAYS or Z is written.
- Precision (High): all Z24 and Z16 values are exact in float; Z32 values
  ≥ 2^24 lose up to 8 low bits (accepted; counter `z32_high_values` for scenes
  where it could matter).
- **Format clamp**: when Z is written and the batch's max Z exceeds the format
  maximum (Z24 0xFFFFFF, Z16/Z16S 0xFFFF): flat Z is clamped in the vertex stage;
  interpolated Z is clamped in the pixel stage (depth output).
- **Z floor**: for interpolated Z that is written, or tested with GREATER,
  the pixel stage outputs floor(z·2^32)·2^-32 (the GS truncates interpolated Z;
  without it GREATER can pass falsely against stored integer values).
- **Software depth test** (only when chapter 08 needs depth feedback): the
  hardware test is set to ALWAYS and the shader discards using the stored depth
  (GEQUAL: discard if z < stored; GREATER: discard if z ≤ stored), writing the
  stored value for fragments whose Z must not change. Requires sampling the
  bound depth image (feedback-loop layout) — otherwise a parallel R32_SFLOAT
  colour copy of depth is written and read.
- The depth target tracks the range of its top byte for later reinterpretation
  as colour (chapter 05).

## 3.8 Scissor and viewport

- GPU scissor = (SCAX0, SCAY0, SCAX1 + 1, SCAY1 + 1)·target scale (truncated),
  intersected with the target size.
- Draw area = GPU scissor ∩ scaled draw rectangle (used for copies, DATE setup,
  colour-wrap regions and render area).
- The viewport covers the whole target.
- SCANMSK 2/3: discard fragments on even/odd rows. **Decision**: HWR counts
  rows in native pixels (row = floor(gpu_y / scale)), which is a fidelity
  improvement over counting upscaled rows; test V-03-1 at 2x on a synthetic
  SCANMSK draw.

## 3.9 Pipeline state and keys (Decision)

Shaders are our own GLSL compiled offline (chapter 12.10); variants are chosen
by specialization constants or a small number of compiled permutations.

**Vertex key**: TME, FST, IIP, expansion kind (none, point, line, sprite),
point-size output.

**Pixel key** (logical fields; packing is an implementation choice):

| Group | Fields |
|---|---|
| Texture | texture on/off, FST, TFX (0..3, 4 = none), TCC, wrap S/T emulation (none/clamp/repeat/region-clamp/region-repeat), sub-region remap, region rect, shader bilinear, palette format (none/8/4HL/4HH), source colour format for TEXA (32/24/16), depth-source format (none/32/16), AEM, mip mode (none/auto/explicit), point-sampler workaround |
| Colour | IIP, fog, FBA, destination format (32/24/16), dither mode, colour wrap mode, PABE |
| Tests | alpha test op and fail behaviour, DATE mode, software depth test, Z clamp, Z floor, SCANMSK |
| Blend | shader blend A/B/C/D, blend kind (fixed, shader, accumulate, mixed variants), Ad-from-RT, alpha-scaled target flags |
| Shuffles | texture-shuffle flags, channel-fetch mode (chapter 09) |
| Output | no colour, second (dual-source) output used, FBMASK in shader |

**Pipeline key** = vertex key + pixel key + fixed-function blend state (blend
enable, factors, op; the blend constant is dynamic and excluded) + colour write
mask + depth/stencil state (test, write, stencil mode) + topology + render-pass
compatibility class (attachment formats, feedback flags).

Normalisation before lookup: when no RGB channel is written, drop blending and
the dual-source output; zero every field that cannot affect the result
(e.g. wrap fields with texture off). Pipelines are compiled lazily on first use
from the pipeline cache (chapter 12); F8 counts "pipelines compiled in the
interval". Expected count: hundreds to a few thousand for GT4 (Hypothesis;
measured in M10).

Dynamic state: viewport, scissor, blend constants (FIX/128), line width.

## 3.10 Passes for one batch (normative order)

1. Pending colour-wrap resolve if the new draw targets another frame or samples
   the wrap frame (chapter 08.4).
2. Hazard copy (texture = current target, chapter 08.8) or target-to-texture
   conversion.
3. DATE preparation: primitive-ID image init and pre-pass, or stencil setup
   (chapter 08.5).
4. Colour-wrap convert into the RGBA16 side target (first draw of a run).
5. Stencil "first writer" clear (stencil-one DATE).
6. **Main draw**: no barrier, one barrier, or one barrier per draw-list group.
7. Blend second pass (when a blend is split in two).
8. Alpha-test second pass.
9. Colour-wrap resolve, unless the next batch continues the run.

Render pass management (Decision):

- Use `VK_KHR_dynamic_rendering` when available, else a render-pass cache keyed
  by (formats, load ops, store ops, feedback flags); pipelines are built against
  a LOAD variant (load/store ops do not affect compatibility).
- Lazy clears: a target has a state Cleared(value) / Invalidated / Dirty.
  Cleared becomes a CLEAR load op at the next pass, an explicit clear command
  when sampled or copied, or is carried through copies without GPU work.
  Invalidated becomes DONT_CARE. Store op is always STORE for colour and depth.
- Consecutive draws stay in one pass unless the colour target, depth target or
  feedback flags change. A draw that does not need the depth target keeps the
  previously bound one attached when sizes match and it is not being sampled
  (and vice versa), to avoid pass restarts.
- Mid-pass flushes (ring full, readback) resume with a pass whose CLEAR load
  ops are rewritten to LOAD.
- Small copies are snapped outward to 4-pixel alignment; a copy covering ≥ 95%
  of a target copies the whole target.

## 3.11 Streaming resources (Decision)

Per frame slot (3 slots, chapter 12.5): persistently mapped ring buffers —
vertex 32 MiB (also bound as a storage buffer for expansion), index 16 MiB,
vertex uniforms 8 MiB, pixel uniforms 8 MiB, texture upload 64 MiB (larger
uploads use a one-shot staging buffer). Reserve/commit with fence tracking;
when a ring is full, wait for the oldest fence that frees enough space, else
submit and retry. Uniform blocks are uploaded only when their bytes change.
Descriptors: per-draw texture, palette, target-read, primitive-ID image and
depth-read slots through push descriptors (fallback: per-frame descriptor
pools); unused slots point at a 1x1 null texture. Uploads to a texture not used
by the open command buffer go into an "init" command buffer that executes
first, avoiding a pass break.

## 3.12 Counters (F8, per interval)

GS batches, primitives, GPU draw calls, render passes begun, pipelines bound,
pipelines compiled, barriers, hazard copies, auto-flush splits, skipped batches
by reason, unsafe-Q batches, flat-shading de-index batches.

## Sources

- Research report `draw-translation-ubershader.md` sections 0 to 6 and addendum
  A1 to A8 (sequence, expansion, vertex format, TFX, sampling, wrap, filters,
  palettes, mipmaps, depth, scissor, keys, passes, limits).
- Research report `output-postfx-vulkan-perf.md` part B.3 to B.7 (streaming,
  render passes, lazy clears, descriptors).
- Research report `upscaling-clut-readbacks.md` A.3 to A.6 (region-clamp
  margins, point/line scaling).
- Research report `texture-cache-v2.md` section 3 (source sizes).
- Research report `ps2tek-gs-facts.md` section 3 (texture addressing, LOD).
