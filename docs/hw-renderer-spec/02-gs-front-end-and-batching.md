# 02 — GS front end and batching

The front end consumes GIF data and produces three kinds of events in strict
stream order: **batches** (primitives plus a register snapshot), **memory
events** (uploads, moves, reads, CLUT loads) and **sync events**
(SIGNAL/FINISH/LABEL, VBlank). It owns the live register file, the vertex
queue and the pending batch.

## 2.1 GIF tags and paths (Confirmed)

A GIF tag is 128 bits:

| Field | Bits | Meaning |
|---|---|---|
| NLOOP | 0..14 | loop count (0 = no data) |
| EOP | 15 | end of packet |
| PRE | 46 | PRIM field valid (PACKED only, see below) |
| PRIM | 47..57 | 11-bit PRIM value |
| FLG | 58..59 | 0 PACKED, 1 REGLIST, 2 and 3 IMAGE |
| NREG | 60..63 | descriptor count, 0 means 16 |
| REGS | 64..127 | sixteen 4-bit descriptors, low nibble first |

- Reading a tag with NLOOP > 0 resets the internal Q latch to 1.0.
- PRE = 1 with FLG = PACKED applies PRIM as a PRIM register write. HWR ignores
  PRE in REGLIST and IMAGE modes (**Hypothesis** for REGLIST on hardware;
  counter `pre_in_reglist`; if GT4 ever sets it, test V-02-1 compares with the
  donor).
- Paths 1, 2 and 3 have independent parser state (current tag, loop counter,
  descriptor index, partial qword). HWR must accept data split at **any qword
  boundary** across calls (GT4 feeds DMA chunks and VIF DIRECT pieces).
  Fixture `hw_gif_split`: feed a captured frame in pieces of 1, 2, 5 and 7
  qwords; the resulting event stream must be identical to one call.
- A path-1 packet that ends without EOP (VU1 XGKICK of an incomplete BIOS
  packet) ends with the loop counter forced to 0.
- GIF reset (from the donor's GIF reset) clears the selected paths' parser
  state, sets TRXDIR to 3 and the Q latch to 1.0.

### PACKED mode (Confirmed)

Each descriptor consumes one qword:

| Descriptor | Data | Effect |
|---|---|---|
| 0x0 PRIM | bits 0..10 | PRIM write |
| 0x1 RGBA | R, G, B, A in the low byte of each 32-bit word | vertex RGBA, and **vertex Q := Q latch** |
| 0x2 STQ | S bits 0..31, T 32..63, Q 64..95 | vertex S, T; Q only into the latch |
| 0x3 UV | U bits 0..13, V bits 32..45 | vertex UV |
| 0x4 XYZF2 | X 0..15, Y 32..47, Z 68..91, F 100..107, ADC bit 111 | vertex kick; drawing kick if ADC = 0 |
| 0x5 XYZ2 | X 0..15, Y 32..47, Z 64..95, ADC bit 111 | same, no fog |
| 0x6..0x9 | 64-bit value in the low half | TEX0_1/2, CLAMP_1/2 write |
| 0xA FOG | F bits 100..107 | vertex F |
| 0xB | — | ignored |
| 0xC XYZF3, 0xD XYZ3 | as 0x4/0x5 | kick without drawing |
| 0xE A+D | data bits 0..63, address bits 64..71 | general register write (address masked to 7 bits) |
| 0xF NOP | — | ignored |

Q sanitising (Decision, follows the studied reference to keep later divisions
finite): PACKED STQ Q of exactly +0.0 becomes the smallest positive normal
float; NaN Q becomes the largest finite float. A+D RGBAQ Q of +0.0 becomes 1.0;
NaN becomes the largest finite float. −0.0 is not changed. Counter
`q_sanitised`.

### REGLIST mode (Confirmed)

NLOOP·NREG 64-bit words, each written to the register whose address is the
descriptor value (0x1 writes RGBAQ including Q, 0x2 writes ST). Descriptors 0xE
and unassigned addresses are no-ops. An odd word count is padded to a qword.

### IMAGE mode (Confirmed)

Qwords go to the active host->local transfer when TRXDIR = 0; otherwise they are
discarded (TRXDIR 2 has already completed at the TRXDIR write).

## 2.2 Register file and writes

The live register file holds the environment registers (PRIM, PRMODE,
PRMODECONT, TEXCLUT, SCANMSK, TEXA, FOGCOL, DIMX, DTHE, COLCLAMP, PABE,
BITBLTBUF, TRXPOS, TRXREG, TRXDIR) and two contexts (XYOFFSET, TEX0, TEX1,
CLAMP, MIPTBP1, MIPTBP2, SCISSOR, ALPHA, TEST, FBA, FRAME, ZBUF), with the write
normalisation of chapter 01.1.

PRIM/PRMODE (Confirmed for the stated behaviour):

- PRIM write with PRMODECONT.AC = 1: all 11 bits used; with AC = 0 only the
  type bits, attributes come from PRMODE. HWR keeps PRMODE as a separate
  register (**Hypothesis**: hardware keeps it separate; the studied reference
  merges them; test V-02-2 counts AC transitions in GT4 captures — if GT4
  never writes AC = 0 the difference is moot).
- Any PRIM write discards a partially assembled primitive (queue reset).
- PRMODE writes while AC = 1 are ignored for attribute purposes.

Writes never flush by themselves; they mark the batch **dirty** (2.5).

## 2.3 Vertex kick and primitive assembly (Confirmed)

Per XYZ write:

1. Type 7: drop the vertex, reset the queue, draw nothing.
2. Store the vertex (S, T, Q, RGBA, U, V, F latched; X, Y, Z from the write).
3. If fewer vertices than the type needs are queued (1 point, 2 line/sprite,
   3 triangle), stop.
4. **Skip** the primitive when: the kick does not draw (XYZ3/XYZF3 or ADC = 1);
   the scissor is empty (SCAX1 < SCAX0 or SCAY1 < SCAY0); the primitive's
   bounding box misses the cull rectangle (below); a triangle or sprite covers
   no pixel centre; a triangle has two vertices with identical X and Y.
5. On skip: lists and sprites drop the queued vertices; strips advance by one
   vertex; fans keep the centre and advance.
6. On emit: if the batch is empty take the **snapshot** (2.5); append the
   primitive's indices; grow the batch's draw rectangle by the primitive's
   pixel rectangle clipped to the scissor; flush if a capacity limit is hit.

Index patterns (h = first queued vertex, t = last): point (h); line list
(h, h+1); line strip (h, h+1) and advance 1; triangle list (h, h+1, h+2);
strip (h, h+1, h+2) advance 1 (no winding flip, the GS does not cull); fan
(centre, t−1, t); sprite (h, h+1).

Coverage box at 1x (12.4 window coordinates): min = (min + 15) & ~15,
max = ((max − 1) & ~15), non-empty when min ≤ max on both axes. Points and lines
use the raw box. Cull rectangle = [SCAX0·16 − 8, SCAX1·16 + 8] (half a pixel of
slack), same for Y.

Sprites with FST = 0: when emitted, the first vertex's Q is replaced by the
second's (sprites use the last vertex's Q).

**Continuation across flushes**: when a batch is flushed in the middle of a
strip or fan, the vertices the next primitive needs (last 1 for strips of
lines, last 2 for triangle strips, centre and last for fans) are carried into
the new batch. A strip therefore continues seamlessly across a state change,
as on hardware.

Capacity limits (Decision, 16-bit indices with expansion headroom): points and
lines 16379 vertices, sprites 32765, triangles 65532; reaching the limit
flushes.

## 2.4 Texture-coordinate rounding at flush (High)

For a textured batch with FST = 0 that is a sprite batch or has constant Z,
before translation each vertex's S, T and Q are rounded towards zero to model
the GS's reduced precision: let e be a float's exponent; clear the low m bits
of S's mantissa with m = min(9 + (max(eS, eQ) − eS), 23), the same for T with
eT, and clear the low 8 mantissa bits of Q (for sprites only the second
vertex's Q). Test vector: S = 0.5, Q = 1.0 → m = 10. HWR applies this only in
that case, as the studied reference does (stated there as matching hardware
tests); fixture `hw_st_rounding`.

## 2.5 Batches, snapshot and the flush decision (Confirmed)

The **snapshot** is a copy of the environment registers and the active context
taken when the first primitive of an empty batch is emitted. The whole batch is
drawn with the snapshot, never with live registers.

Each relevant register write recomputes a dirty bit: set when
`(new XOR snapshot) AND relevance mask ≠ 0`, cleared otherwise (so writing the
old value back clears it). Only the snapshot's context is tracked.

Relevance masks: PRIM all 11 bits; TEX0: TBP0, TBW, PSM, TW, TH, TCC, TFX, plus
CPSM and CSA only for indexed PSM (CBP, CSM, CLD never — palette changes go
through the CLUT path); TEX1 all used fields; CLAMP all; MIPTBP1/2 all;
XYOFFSET; SCISSOR; ALPHA; TEST; FRAME (FBP, FBW, PSM, FBMSK); ZBUF (ZBP, PSM
low nibble, ZMSK); TEXA; FOGCOL; DIMX; DTHE; COLCLAMP; PABE; FBA; SCANMSK.

The decision is taken at the next vertex kick (always on a drawing kick; on a
non-drawing kick only if the primitive class changed or XYOFFSET is dirty),
when the batch is non-empty and some bit is dirty. The batch is flushed if any
of these holds:

1. PRIM dirty and the primitive class (point/line/triangle/sprite) differs, or
   any non-type PRIM bit differs (a triangle list may continue as strip or fan).
2. TEST, SCISSOR, XYOFFSET, SCANMSK or DTHE dirty.
3. DIMX dirty and the snapshot dithers.
4. ALPHA or PABE dirty and the snapshot has ABE = 1.
5. FOGCOL dirty and the snapshot has FGE = 1.
6. FRAME, COLCLAMP or FBA dirty and the snapshot can write colour (not:
   ATE = 1, ATST = NEVER, AFAIL bit 0 = 0, DATE = 0).
7. ZBUF dirty and the snapshot can write Z (ATST ≠ NEVER, or ATE = 0, or
   AFAIL = ZB_ONLY).
8. Snapshot TME = 1 and TEX0, TEX1, CLAMP or TEXA dirty; or MIPTBP1/2 dirty
   with MXL > 0.

Otherwise the dirty set is cleared and the batch keeps growing.

Other flush triggers: a CLUT load that matters (chapter 07); a transfer that
overlaps the batch (2.7); VBlank (2.8); auto-flush (2.6); capacity; GS reset.
A batch with ZTE = 1 and ZTST = NEVER is discarded at flush.

TEXFLUSH does not flush. It sets a "texture flushed" flag (unless TEX0 is
already dirty), cleared when a textured batch is flushed or the snapshot
context's TBP0 changes. While set, auto-flush becomes aggressive (2.6).

Flush reasons are counted per F8 interval (`flush_reason_*`).

## 2.6 Auto-flush: per-primitive feedback (Confirmed; GT4 needs mode "sprites")

Problem: a draw samples memory that the same batch writes (frame or Z). The GS
rasterizes primitives in order through a small texture cache, so a later
primitive sees earlier primitives' pixels once its fetch reaches a new cache
page. A GPU draw of the whole batch would sample stale data. GT4's lens flare
and sun intensity depend on this (High). Option `GT4_HW_AUTOFLUSH=sprites|all|off`,
default `sprites` (GT4's known requirement).

On a drawing kick that completes a primitive in a non-empty batch (strips and
fans: only every second triangle, unless the texture-flushed flag is set), the
front end splits the batch **before** storing the new primitive when all of the
following hold (pixel coordinates are (XY − XYOFFSET) >> 4):

1. TME = 1 (and in `sprites` mode the primitive is a sprite; point lists are
   always tested).
2. The texture's channel set intersects the frame's written channel set (frame
   set minus byte lanes fully masked by FBMSK).
3. The draw is not a recognised shuffle (chapter 09 conditions on the first
   queued sprite).
4. Some candidate level's TBP equals FRAME's base block or ZBUF's base block
   (level 0, or with MXL > 0 and a mip MMIN: LCM = 1 → floor(K/16)..ceil(K/16)
   clamped to MXL; LCM = 0 → 0..MXL), and that buffer is actually written.
5. The formats are not a co-resident pair (CT24/Z24 with T8H/T4HL/T4HH; T4HL
   with T4HH).
6. The new primitive's texel rectangle (FST: U>>4; STQ: S/Q clamped to 1.0
   times 2^TW, rounded) with clamp modes applied, intersects the batch's draw
   rectangle (when both are colour or both Z), and either the texture-flushed
   flag is set (flush now) or the new texel rectangle starts on a different
   texture page than the previous primitive's last texel coordinate and an
   earlier primitive's screen rectangle (inclusive sprite ends; colour/Z block
   swap modelled by XOR of half a page when kinds differ) intersects the new
   texel rectangle within the scissor. With different layouts (bpp or widths
   differ) the comparison is done at page granularity, and when even that
   cannot be expressed the batch is flushed unconditionally.

The split produces many one-primitive batches; chapter 03 must keep them cheap
(same pipeline, no pass restart). F8 counter `autoflush_splits`.

## 2.7 Transfers (Confirmed)

Register writes:

- BITBLTBUF, TRXPOS, TRXREG: a value different from the current one first
  commits any buffered partial upload. They do not flush draws.
- TRXDIR always commits a pending upload, then: 0 arms host->local (capture
  BITBLTBUF/TRXPOS/TRXREG; start at DSAX, DSAY); 1 arms local->host; 2 runs the
  overlap check (below) for source and destination and performs the move
  immediately (chapter 05 GPU move first, then SLM move); 3 nothing.
- HWREG writes 8 bytes of image data only when XDIR = 0.

Host->local:

- Size = ceil(RRW·RRH·bpp/8) bytes (bpp 32, 24 packed, 16, 8, 4), capped at
  4 MiB; extra data is ignored.
- Data may arrive in several IMAGE pieces. HWR buffers it and commits when the
  transfer completes or on: a differing BITBLTBUF/TRXPOS/TRXREG write, a TRXDIR
  write, a CLUT load, any batch flush, a VBlank. A partial commit covers only
  the rows received (rows = ceil(pixels/RRW), at least one block height).
- Commit order: (1) overlap check with the pending batch (flush it if it reads
  or writes the destination); (2) surface-cache invalidation for the destination
  rectangle (chapter 04/05: sources dropped, targets dirtied) — split at the
  2048 wrap in X or Y; (3) swizzle into the SLM; (4) append a record to the
  **transfer log** (chapter 05) and to the CLUT dirty tracker.
- 4-bit uploads with an odd row width continue the nibble stream across rows
  (GT3-era data does this; High).

Local->host and local->local: chapter 10.

Overlap check (before every upload commit, read, and move, against the pending
batch): flush the batch if

- it is textured and the written pages overlap the texture's pages (all levels
  1..MXL too, each at its own TBP/TBW with the area shifted by the level; area
  = [0, 2^TW)x[0, 2^TH) for repeat/clamp, the region for region modes);
- its frame is written (or DATE is on) and the transfer's pages overlap the
  batch's draw rectangle in frame coordinates (or the bases are equal);
- its Z is written or tested (ZTST ≠ ALWAYS) and the pages overlap in Z
  coordinates.

"Pages overlap" uses page-rounded block ranges with wrap handling (lift the
lower range by 16384 when one range wraps). For reads the **source** base is
compared (the studied reference compares the destination base in one place,
which the research team flagged as a probable oversight; HWR uses the source;
test V-02-3).

## 2.8 VBlank (Confirmed)

At each guest VBlank (called by the donor through the existing hook):

1. Refresh the display state from the privileged registers.
2. Commit pending uploads. Flush the pending batch **only if** its frame block
   range overlaps an enabled display circuit's buffer range; otherwise keep it
   (preserves batching across VBlank).
3. Output stage (chapter 13) and ageing (chapter 04).

## 2.9 Batch facts computed at flush ("vertex trace")

Computed over all vertices of the batch with the snapshot context; used by
chapters 03 to 09:

- x, y min/max in pixels; z min/max (unsigned); fog min/max (for sprites Z and
  F from the second vertex).
- Texel coordinate min/max (FST: U/16, V/16; STQ: (S/Q)·2^TW etc., NaN results
  excluded and flagged); Q min/max.
- RGBA min/max (zero when TME, DECAL and TCC = 1); flat shading uses only the
  provoking vertices.
- Equality mask: which of R, G, B, A, x, y, z, f, s, t, q are constant. When
  ZTE = 1 and ZTST is GEQUAL or GREATER, "constant Z" is re-checked bit-exactly
  on integer Z (floats lose the lowest bit).
- AA1 on lines/triangles without ABE forces alpha min = max = 128.
- Filter: mag-linear = MMAG; min-linear = MMIN ∈ {1, 4, 5}; if MXL = 0 the
  draw is linear iff MMAG; otherwise the LOD range (LCM = 0, FST = 0:
  `−log2(Q)·2^L + K/16` at Q min and max; else K/16) decides MMAG (max LOD ≤ 0),
  MMIN (min LOD > 0) or "either is linear".
- "Unsafe Q": the minimum Q-derived coordinate exceeds 1e30 (chapter 03 then
  pre-divides on the CPU).
- Alpha range of the fragment (vertex alpha combined with the texture alpha
  range through TFX; texture alpha from TEXA for 16/24-bit, from the palette for
  indexed formats, from the target's tracked range for target sources).

Fixture `hw_vertex_trace`: synthetic batches with known extremes.

## 2.10 Primitive overlap and draw lists

Some methods (chapter 08) need to know whether primitives of a batch overlap.

- Fewer than 4 vertices referenced: no overlap.
- Triangles: "no overlap" only when exactly two triangles form an
  axis-aligned quad; otherwise unknown.
- Lines and points: unknown.
- Sprites: exact — walk sprites in order keeping the union of the current
  group's boxes; a sprite whose box intersects the union starts a new group.

The same walk builds the **draw list** (group sizes and per-group bounding
boxes, in scaled pixels, floor/ceil with ±1 pixel margin for bilinear) used for
full-barrier drawing. For triangles HWR may merge strips, fans and quad pairs
into one bound before the walk (Decision: phase 1 does only quad pairs; strip
and fan merging is an optimisation for later). Bounding boxes may be refined
with a 45-degree rotated box (octagon) to avoid false overlaps of diagonal
triangles (optional).

"Covers without gaps" (used by clear detection, chapter 05): one point; triangles
forming only quads; one sprite; full-width sprites stacked with no vertical gap;
full-height sprites side by side; page-height rows of equal sprites.

## 2.11 Reset

GS reset (CSR.RESET or donor reset): registers to power-on values, vertex queue
and batch cleared, CLUT buffer cleared, transfer state cleared, and (Decision)
the cull rectangle set to "nothing" until the game writes SCISSOR (avoids
drawing with stale state right after reset). `clear_vram` additionally zeroes
the SLM and drops all targets and sources.

## Sources

- Research report `gs-state-gif-flush.md` sections 0 to 7 (GIF modes, register
  writes, dirty tracking, auto-flush, kick, transfers, vertex trace, overlap,
  VSync).
- Research report `draw-translation-ubershader.md` section 6 and addendum A4
  (vertex limits).
- Research report `upscaling-clut-readbacks.md` C.2 (FIFO read order).
- Research report `gt4-gamedb-hacks-donor.md` sections 1.3 (auto-flush for
  GT4), 2.5 (odd-width 4-bit uploads), 3.5 (split GIF packets).
- Research report `ps2tek-gs-facts.md` section 1 (fields).
