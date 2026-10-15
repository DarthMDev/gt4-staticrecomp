# 05 — Render targets and depth

A **target** is a GPU image that stands for a GS buffer (base BP, width BW,
format PSM) at the internal scale. Colour targets are `R8G8B8A8_UNORM` (all GS
colour formats, 16 and 24 bit included); depth targets are `D32_SFLOAT`
(`_S8_UINT` when stencil DATE is used). This chapter defines target state,
lookup for a draw's FRAME and ZBUF, creation and preload, colour/depth aliasing,
invalidation, clears, moves and the display lookup.

## 5.1 Target state (Decision; fields are Confirmed concepts)

| Field | Meaning |
|---|---|
| identity | BP, BW, PSM (current interpretation) |
| unscaled size, scale | GS-pixel extent; image size = ceil(unscaled·scale) |
| valid rectangle | GS pixels holding meaningful data (drawn, loaded or copied); determines the end block |
| drawn rectangle | area drawn on the GPU since the last write-back (target newer than SLM) |
| dirty list | rectangles (with writer PSM, BW, channel mask, "linear on upscale" flag) where the SLM is newer |
| channel validity | RGB valid, alpha low nibble valid, alpha high nibble valid (16-bit: both nibbles together) |
| alpha range | known min/max of stored alpha (CT24 targets start at 128/128, others 0/0) and whether known |
| alpha-scaled | stored alpha uses the "0x80 maps to 1.0" encoding (chapter 08.3) |
| clear state | Cleared(value) / Invalidated / Dirty (chapter 03.10) |
| last batch | global batch number of the last draw into it (recency tie-break) |
| age, used, is-display, downscaled, readbacks-since-draw | bookkeeping |

Two lists, colour and depth, each most recently used first. Before a target is
deleted every source referencing it is removed (debug assertion).

**Dirty list merge** (Confirmed): ignore empty rectangles; scanning newest
first among entries with the same PSM, BW and channel mask, drop the new
rectangle if an entry contains it; otherwise merge (union, delete the old,
continue) when after clipping to the valid area they share both vertical
edges and touch vertically, share both horizontal edges and touch
horizontally, or the new one envelops the old. After insertion, if the first
entry covers all channels and the dirty total covers the drawn rectangle, clear
the drawn rectangle (the SLM is authoritative there). Vectors: (0,0)-(64,32) +
(0,32)-(64,64) → (0,0)-(64,64); (0,0)-(64,32) + (64,0)-(128,32) → merged
horizontally.

**Fully dirty**: exactly one dirty entry, with all requested channels, covering
the whole valid rectangle. A CT32 target with only alpha dirty is not fully
dirty; its alpha validity is cleared instead.

**Update** (applying dirty data): for each dirty entry, decode the SLM rectangle
(writer PSM/BW) and draw it into the target at scale (bilinear for colour when
scaled and the entry's linear flag is set, per `GT4_HW_DIRTY_FILTER=auto|linear|nearest`,
default auto = linear for colour when scaled, always linear for depth; nearest
at 1x); depth targets receive converted values (5.5). Then clear the list.

## 5.2 Target size (Confirmed behaviour, adopted)

- **Valid size of a draw**: height = min(scissor bottom, draw rectangle
  bottom); width = min(FBW·64, scissor right) (FBW 0 treated as 1). Exception:
  one-page-tall clears marching down page rows with the next draw continuing at
  the next row use the scissor height. Shuffles and page copies take the
  maximum with the source's valid size (page rounded). Aligned up to the frame
  format's page size, clamped to 2047. Texture-shuffle draws halve the height
  (or the width for double-width shuffles, chapter 09).
- **Size memory** ("height cache"): keyed by (BP, BW, PSM), stores the largest
  size ever requested; lookup returns max(stored, requested) and refreshes the
  entry; entries expire at age 120. New targets use the remembered size, so a
  buffer first drawn 416 lines tall is created 448 tall next time.
- **Growth after a draw**: colour and depth targets of the draw are resized to
  the maximum of the new size and both current sizes (limit 2048 per axis).
  The draw rectangle is added to valid and drawn rectangles; growth is allowed
  only when the frame is not fully masked and either the size may change or the
  draw's bottom is ≤ 2x the display height and it is not a texture shuffle
  (prevents runaway growth from garbage draws). When the remembered size is
  taller than anything drawn and the draw preserves contents, the newly exposed
  band is added as dirty (preloaded from the SLM).

## 5.3 Lookup for a draw (Decision on structure)

Inputs: FRAME or ZBUF identity, size, scale, type, FBMSK/ZMSK, whether the
draw preserves RGB/alpha (it reads the target: blending with Cd, partial masks,
DATE, partial coverage), draw rectangle, shuffle flag, possible-clear flag,
current source.

1. **Exact base, same type.** Among targets with the same BP, take the most
   recently drawn. Reject (and delete, unless it is the current source's
   origin) when the width changed and the draw preserves nothing and either
   the target is old (age ≥ 1) with new BW > 1, or the draw is taller than a
   page and BW shrinks, or EE uploads (dirty) cover the whole new draw range.
2. **Inside a target, same type** (draw base lies inside an existing target):
   accept when the page offset is horizontally 0 (or the draw fits in the
   remaining row with the same BW), the widths match, the one-pixel-shrunk
   valid area still covers the draw, and one corroboration holds: it is a
   shuffle; Z is offset too (or there is no depth); the base is exactly the
   target's midpoint (double buffer); the target was drawn by the previous
   batch. Then the draw is rendered **at an offset** into that target:
   vertical offset = (block delta / 32 / BW)·page height, horizontal =
   ((block delta / 32) mod BW)·page width. Double buffers taller than 896 lines
   with a base at or past the midpoint are instead split into a new target
   for the lower half. Swizzle mismatch or a draw spilling past the row
   deletes the old target (it was relocated).
3. **Draw before a target** (negative offset, found through step 2's search):
   move the target's base back to the draw base, grow its image upward, shift
   contents, valid, drawn and dirty rectangles and dependent source regions down
   by the page-row delta.
4. **Opposite type at the same base** ("colour/depth aliasing", 5.5): if a
   depth target exists where a colour target is requested (or vice versa), with
   valid channels, compatible bpp and acceptable width, and age ≤ 1 (age 0
   preferred), create the requested target from it by conversion when the draw
   preserves contents; otherwise delete it if the draw overwrites it.
5. **Nothing found**: create (5.4). A "large clear" (≥ 1023 rows, gap-free,
   height ≥ width) with no target is done as a memory clear (5.8) and skipped.

After a hit, **post-lookup processing**:

- Scale mismatch with the desired scale (and not "preserve scale"): resample
  into a new image (downscale to 1x with the box filter of chapter 06,
  otherwise bilinear for colour, nearest for depth); repoint sharing sources.
- Z32 requested as Z24 with a non-zero top byte: conversion copy zeroing it.
- 16 ↔ 32 bpp reinterpretation of a used target (not a shuffle): update dirty
  data first; 32→16 doubles the valid height (same bytes, twice the rows),
  16→32 halves it; copy through a format-converting pass into a resized image
  (via the GLM when the layouts do not line up page for page). Vector: CT32
  valid (0,0)-(640,224) looked up as CT16 → valid (0,0)-(640,448).
- RGB needed but invalid (preserving draw): take RGB from an opposite-type
  target at the same base with valid RGB (convert), else reload from the SLM
  (fresh image, preload, then copy the old alpha back with an alpha-only mask).
- Full overwrite (preserves nothing, covers the whole valid area): drop dirty
  entries (only RGB-only entries when alpha must survive: 24-bit write or alpha
  masked) and mark the image Invalidated — except when scaled and the half-pixel
  mode is below `native`, where it is Cleared(0) (top/left rows may not be
  rasterized).

## 5.4 Creation and preload

- A zero-size request creates nothing.
- Image allocated at ceil(size·scale), initially Invalidated; valid rectangle =
  the valid size; end block computed.
- **Preload decision** (does the new target start from SLM contents?):
  - Display targets or forced preload: mark the whole target dirty (loaded on
    first use). A "clear" record for this exact BP/BW in the transfer log of the
    previous batch is consumed and the target is Cleared(value) instead.
  - Others: walk the **transfer log** (5.9) newest first, at most 500 batches
    back; for each upload/move/clear overlapping the new block range with a
    compatible format, translate its rectangle into the target; accumulate.
    Stop when one record covers the whole range (if it was an exact (BP, BW,
    PSM) upload it becomes the valid rectangle; if it ends exactly at the target
    end it is consumed from the log) or the union covers the target. Add the
    union as dirty, or the whole target if the draw preserves contents.
  - When the SLM authority rule says no target and no upload touched the area,
    the SLM content is still authoritative: a preserving draw marks the whole
    target dirty (Decision: correctness over speed; counter `preload_full`).
- **Channel validity at creation**: RGB valid for depth formats, or when RGB is
  not fully masked, or when dirty data includes RGB; alpha validity from the
  unmasked alpha nibbles (16-bit: both); when alpha comes only from dirty data,
  from the most recent matching upload in the last 100 log records (a T4HL
  upload validates only the low nibble, T4HH only the high).
- **Reconcile overlapping older targets** (both lists, colour formats):
  - Older target with the same base that the new one fully covers: delete
    (unless it is the current source's origin).
  - New target starts inside an older one at a row boundary: trim the older
    valid height to the new base.
  - New target starts before an older one and engulfs its start (whole rows,
    same BW): grow the new image, copy the older contents at the page offset
    (channel-masked by its validity) when scales match, repoint the current
    source, delete the older target.
  - **Double buffer**: the new base equals an older target's midpoint with the
    same BW: halve the older valid height; if the draw preserves and the older
    drawn area extends into the lower half, copy that half into the new target.
  - Depth: a new depth target inside a same-width older colour target at a row
    boundary trims the older valid height (or, when the older one is clearly a
    former Z reused as colour, adopts its base and contents by conversion).
- **Combine inside targets**: after creating or growing a target, any same-type
  target wholly inside it with the same BW (or BW 1) and bpp is merged in: its
  drawn area copied at its page offset (channel-masked), validity OR-ed, valid
  and drawn rectangles extended, sources repointed, then deleted.

## 5.5 Colour and depth at the same memory (Confirmed)

On the GS, a colour buffer and a depth buffer at the same address are the same
bytes; on the GPU they are two images. Value conversions:

- Colour → depth: 32-bit d = c·2^-32; 24-bit d = (c & 0xFFFFFF)·2^-32; 16-bit
  from a 16-bit colour: pack R bits 3..7, G bits 11..15, B bits 19..23, A bit 31
  into a 5:5:5:1 integer (R at bit 0, G at 5, B at 10, A at 15), then ·2^-32;
  16-bit from 32-bit: (c & 0xFFFF)·2^-32.
- Depth → colour: i = trunc(d·2^32); 32-bit: i as RGBA bytes; 16-bit: unpack the
  5:5:5:1 fields back to 8-bit positions plus alpha bit 31.
- These conversions are only valid when both images use the **same layout**
  for the bytes. Colour and Z swizzles differ (block XOR 24), so a colour read
  of depth memory at the same base sees scrambled blocks. HWR rule (Decision):
  same-base colour↔depth conversion of a target is allowed only when the draw
  uses the matching format pair coupled by FRAME/ZBUF (chapter 01.1: a Z-format
  FRAME implies colour layout for ZBUF and vice versa) — otherwise the GLM path
  produces the scrambled data exactly.
- **Type-exclusive invalidation** after every draw: the frame write degrades the
  depth target at FBP, the Z write degrades the colour target at ZBP. For the
  other-type target at exactly that base: alpha survives if the write is 24-bit
  or 32-bit with that nibble masked; RGB survives if the write is ≥ 24-bit with
  RGB fully masked. Keep the target with reduced validity if anything survives
  (for depth only a 24-bit depth whose RGB survives); otherwise delete it.

## 5.6 Invalidation of targets by memory writes (Confirmed behaviour, adopted)

For an upload (or SLM move destination) rectangle W in (BP, BW, PSM), for every
target overlapping W's block range:

- Same base, compatible bits, equal BW: add W to the dirty list in the writer's
  coordinates (small Z-format writes of at most half a page per axis are
  translated page by page). Fully dirty → delete the target and its sources.
- Otherwise overlapping with shared bits: **page-by-page dirtying** — if W's
  range covers the whole target, dirty the whole valid area with the writer's
  channels; else express W in the target's format (outward to pages when formats
  differ, to blocks for the same bpp with a block offset, to columns for small
  same-kind rectangles), handle page and block offsets (surface-offset search
  for in-page offsets), and add either one clipped rectangle (layouts line up)
  or one rectangle per touched page (whole target pages; pages spilling past
  the row wrap to the next row). Delete when fully dirty for the written
  channels.
- Alpha-only write (T8H/T4HL/T4HH) over an RGB-only target (CT24/Z24): clear the
  touched alpha validity nibbles.
- Writes are split at the 2048 wrap in X or Y before processing.

## 5.7 Moves (local->local) on the GPU (Confirmed behaviour, adopted)

TRXDIR = 2 first tries a GPU move; on success the SLM is **not** updated, the
destination target becomes authoritative and the move is logged as
"GPU-only". On failure: write back the source rectangle (chapter 10),
invalidate the destination (5.6), copy in the SLM.

- **Shuffle move**: same BP, same colour PSM, C16 with a ±8 pixel horizontal
  offset and equal y, inside a C32 target → an RG↔BA channel copy draw; alpha
  range becomes unknown when BA is written.
- **Page move**: both BW 1, same PSM, page-aligned → copy whole pages between
  containing targets.
- **General move**: same bpp, no palette formats (except T8H→T8H alpha only):
  source target exact (same BP and BW, or a whole page row inside); destination
  exact, or created when the copy is to (0,0) / in place with the same BW, or the
  size memory knows the destination (age ≤ 10). Scales must match, rectangles
  must lie within the source image. Update dirty data first, copy (through a
  temporary when source and destination overlap), OR validity by the
  destination channels, extend valid/drawn rectangles, invalidate sources over
  the destination, combine inside targets. Sequences of page or strip moves
  remember the expected next bases so later moves translate relative to the
  first one's targets.
- Overlapping same-buffer moves honour TRXPOS.DIR semantics; X wraps at 2048.

## 5.8 Clears (High; GT4 relevant)

- **Target clear**: a draw that writes a constant colour (and/or constant Z)
  without texture, blending, tests that read the target, or partial masks,
  covering the whole valid area without gaps (chapter 02.10), becomes
  Cleared(value) on the target(s) — no draw. A 32-bit clear of a 16-bit buffer
  packs the colour and mask to 5:5:5:1 first.
- **Memory clear**: a gap-free constant clear at least (FBW − 1)·64 wide with
  no target: write the constant into the SLM, invalidate sources, dirty or
  reduce contained targets, log a "clear" record; later target creation sees it
  and starts Cleared instead of preloading.
- **Double-half clear**: one draw clears FRAME and ZBUF where ZBUF = FRAME +
  half the buffer (vertically or horizontally), with ZTST ALWAYS, colour and Z
  constant and numerically equal, no partial masks, matching bit classes, and
  the next batch is not a non-clear draw to the same pair: double the
  rectangle, clear only the base buffer, delete targets at the half point.
  Refuse when the doubled height is ≥ the display height or > 300 lines per
  half. Vector: FBP 0, ZBP page 70 (FBW 10, 224 rows of CT32 = 7 page rows ·
  10), clear 640x224 → one 640x448 clear at FBP 0.
- **Striped double clear** (named for GT4 Online's engine, so High for retail
  GT4): colour and Z cleared in one draw of vertical strips, ZBP = FBP or one
  page apart (FBW > 1), Z unmasked, same bit class, Z constant and equal to the
  colour word, colour constant, strip width one page (one page apart) or half a
  page: replace with one colour clear of the full rectangle widened by the
  strip width (left or right depending on which buffer is ahead); drop Z.
  Test V-05-1 in chapter 14 compares with the donor at 1x.

## 5.9 Transfer log (Confirmed)

Records of recent memory writes: destination BP/BW/PSM, rectangle, batch
number, kind (upload, move, clear), GPU-only flag. Consecutive writes to the
same BP of the same kind merge by rectangle union. At each non-idle VBlank: if
the last write was fewer than 5 batches ago, drop records older than 50
batches; otherwise clear the log. Idle VBlanks leave it untouched. Used by
target preload (5.4), memory-source size inference (04.6), display lookup
(5.10), readback shortcuts (chapter 10).

## 5.10 Display lookup (Confirmed behaviour, adopted)

Each enabled display circuit's buffer (DISPFB BP, FBW, PSM, size from chapter
13) is resolved:

1. Colour target at the exact base. If its BW differs, it is old, and its only
   dirty entry has exactly the display BW: re-label it with that BW and take the
   dirty rectangle as valid; else if dirty, delete it.
2. Else a target containing the display range (wrap aware), or a same-BW,
   same-PSM target where the display base is exactly the start of row "display
   height" (the second half of a double buffer). Newest wins.
3. Else same base and BW with no validity requirement.
4. Grow the found target if the display reads beyond it (needed height = display
   height + y offset, width = min(display width, FBW·64)), preloading the new
   strips from the SLM.
5. None found: look in the transfer log (500 batches, ignoring GPU-only
   moves): a clear in the latest batch → show black; an exact upload →
   create a display target, mark the uploaded channels valid (so a later
   overlay draw does not treat it as garbage); any other overlapping upload →
   create; nothing → decode straight from the SLM (Decision: HWR shows SLM
   content rather than nothing, which matches the donor).
6. The output stage reads the target (after updating dirty data) at y offset
   (page delta / FBW)·page height when DISPFB points inside it.

## 5.11 Temporary depth for offset draws (High)

When the colour target is offset into a larger target (5.3 step 2) but ZBUF is
not offset the same way, a temporary depth image aligned to the colour offset is
used for the draw and merged back afterwards (whole, or the drawn sub-rectangle
when the context changes). Writes to its address range invalidate it.

## 5.12 Counters (F8)

Targets created/deleted/resized, target MiB, preloads (rectangles, bytes),
updates (dirty uploads), clears converted, memory clears, GPU moves by kind,
CPU moves, display lookups by path.

## Sources

- Research report `texture-cache-v2.md` sections 1.2, 1.6, 1.7, 1.9 to 1.11,
  2.1 to 2.10, 4.1 to 4.5, 6, 7, 8.
- Research report `texture-cache.md` sections 1.3, 2, 4, 6 (earlier pass).
- Research report `gt4-gamedb-hacks-donor.md` section 2.5 (striped double clear,
  GT4 Online).
- Research report `upscaling-clut-readbacks.md` A.5 (dirty upload filter on
  scaled targets) and C.1.
- Research report `output-postfx-vulkan-perf.md` B.5, B.6 (formats, lazy clear
  states).
- Research report `draw-translation-ubershader.md` 1.1 item 17 (clear
  detection).
