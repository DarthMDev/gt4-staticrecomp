# 04 — Texture cache (sources)

The surface cache has two halves: **targets** (chapter 05) and **sources**
(this chapter). A source is what a draw samples: a GPU texture decoded from the
SLM, or a view or conversion of a target. This chapter also defines the shared
geometry primitives, invalidation, the GLM reinterpretation path and ageing.

## 4.1 Geometry primitives (Confirmed behaviour; implement as a tested library)

All cache decisions reduce to these operations. Each gets CTest vectors.

- **Block range of a rectangle** in (BP, BW, PSM): first block of the top-left
  pixel, last block of the bottom-right pixel. The **unwrapped end** is not
  reduced modulo 16384, so ranges that cross the end of memory compare
  correctly. When a rectangle's right and bottom edges are page-aligned, the
  end is rounded up to the last block of its page (Z layouts are not
  sequential within a page). If the end is below the start but within 32
  blocks (intra-page inversion), swap them.
- **Range overlap**: [a0, a1] and [b0, b1] overlap when both are well formed
  and intersect; when one range extends past 0x3FFF (unwrapped) and the other
  lies entirely before its start, lift the other by 0x4000 first.
  Vector: A = [0x3F00, 0x4100], B = [0x0000, 0x0050] → overlap.
- **Inside**: a rectangle's first and last blocks lie in [base, unwrapped end]
  of a surface.
- **Rectangle translation** between layouts ("translate by page"), from
  (BP_s, BW_s, PSM_s) to a surface (BP_t, BW_t, PSM_t):
  - Allowed when BP_s is page aligned, equals BP_t, or the formats share block
    size with equal BW; and the widths in pages match, or the rectangle is a
    single row within one page height, or a single page-aligned page, or full
    page-aligned rows. Different bpp additionally requires a page-aligned
    rectangle.
  - page offset = (BP_s − BP_t) >> 5, block offset = (BP_s − BP_t) & 31
    (unwrap BP_s by 0x4000 when the target wraps).
  - Equal page widths: offset by (page offset mod pages-per-row) pages
    horizontally and (page offset div pages-per-row) pages vertically.
  - Different formats or a block offset: decompose into whole pages plus blocks
    of the source format and rebuild in the target's page/block sizes; a
    remaining block offset with the same layout is resolved by the surface-offset
    search.
  - Different page widths: a one-page-wide source maps pages along the target
    row; a single row or page goes to (offset mod row, offset div row); full
    rows round up to whole target rows (refused for texture reads).
  - Results spilling past the row: wrap to the next page row when starting past
    the edge, else widen to the full row and extend one page down.
  - Vector: CT32 target BP 0x1000 BW 10; source BP 0x1160 (page 11), same BW,
    rect (0,0)-(64,32) → (64,32)-(128,64).
- **Surface-offset search** (fallback): align both rectangles outward to
  blocks; require overlapping ranges; step through the target in block steps
  (starting at the page row implied by the BP difference; BW/2 for 8/4-bit
  targets) until the block number equals the source start; same for the end
  (or the target's end). Memoise results (bounded cache, cleared when full or
  when all targets are removed). When used against a target, reject if any
  dirty rectangle overlaps the found range.
- **Dirty rectangle re-expression** in another PSM: scale each coordinate by
  (target block size / writer block size) per axis, then align outward to the
  writer's block. Vector: CT32 dirty (8,0)-(16,8) on a CT16 target →
  (16,0)-(32,8).

## 4.2 Source kinds (Decision)

| Kind | Content | Owned texture | Eligible for hash cache and replacement |
|---|---|---|---|
| Memory source | decoded from the SLM | yes (or shared via hash cache) | yes |
| Target view | samples a target's image directly, optionally with a region | no (shared) | no |
| Target conversion | a converted copy of a target (format reinterpretation, 1x downsample, index texture) | yes | no |
| GLM source | decoded from the GLM after composing SLM and targets (4.9) | yes | no |
| Temporary | any of the above, used for this draw only (self-reference, shuffles) | — | no |

Every source records: TEX0 identity (TBP0, TBW, PSM, TW, TH), TEXA (when it was
baked in), region, LOD range, palette handle, alpha min/max, the page set it
covers, and for target-derived kinds the originating target and the target's
identity at creation. Non-temporary sources are registered in a per-page index
(512 lists, most recently used first); a target-derived source with a region is
registered under the target's pages so invalidating the target finds it.

## 4.3 Region from CLAMP (Confirmed)

| Mode | Condition | Region on that axis |
|---|---|---|
| REGION_CLAMP | MAXU ≥ MINU and (width < 2^TW or MAXU ≥ 2^TW) | [MINU, MAXU + 1) |
| REGION_REPEAT | MINU ≠ 0, w = ((MINU \| MAXU) − MAXU) + 1, and (w < 2^TW or (MAXU ≠ 0 and w ≤ 2^TW)) | [MAXU, (MINU \| MAXU) + 1) |

"Fixed TEX0": the region extends past 2^TW (the region, not TW, sizes the
texture). Mip levels shift the region right by the level (at least 1 texel).

## 4.4 Source lookup (Decision on structure; rules are Confirmed behaviour unless marked)

Input: TEX0, TEXA, CLAMP, the base level and LOD range, the palette, the read
rectangle from the vertex trace (texel min/max, widened by 1 for bilinear,
clamped by the wrap mode; repeat narrowed only if it does not wrap).

**Step 0 — preconditions.** TW or TH > 10 without a region on that axis: skip
the draw (counter). Invalid PSM: skip.

**Step 1 — authority test.** Compute the read's block range (all levels). Find
every target (colour and depth) whose block range overlaps it **and** whose
drawn rectangle or valid channels make it authoritative for some of those blocks
(chapter 00 data-authority rule). This yields three cases:

- **(A) No authoritative target**: the SLM is the truth → memory source
  (step 2).
- **(B) One target, a fast path applies** (4.5) → target view or target
  conversion.
- **(C) Anything else** (several targets, unaligned offsets, unsupported
  reinterpretation, read extending past the target's valid area into
  SLM-owned blocks) → GLM source (4.9), or, when the GLM is disabled
  (`GT4_HW_GLM=0`, diagnostic), write back the targets (chapter 10) and use a
  memory source. Counter `glm_sources`.

Before deciding, apply the dirty rules: if the target's dirty list overlaps the
read, upload the dirty data into the target first ("update"), unless the read is
wholly inside dirty areas (then the SLM is the truth for those blocks — the
target is not authoritative there).

**Step 2 — memory source hit.** Search the per-page index of the start page
(and of the region-origin page first when the region starts elsewhere), most
recent first. A hit requires the same TBP0, TBW, PSM, TW, TH; for indexed
formats either an index texture (palette bound at draw time) or an expanded
texture whose palette content equals the current one; for 16/24-bit formats the
same TEXA; equal regions (or, under clamp-clamp with a cached region starting at
(0,0), a cached region covering the read); the same LOD range. A hit moves to the
front. A miss creates a memory source (4.6).

## 4.5 Fast paths from targets

All require: the target is authoritative for the whole read range, the needed
channels are valid (table below), and no dirty data remains in the read.

| Fast path | Condition | Result |
|---|---|---|
| Same-format view | same BP, same swizzle family (CT32/CT24 with C32/C24 read; Z32/Z24; CT16/CT16S/Z16/Z16S exact match), compatible width (page columns equal, or BW ≤ 1 within one page) | target view, region = texture extent when the logical size differs |
| Inside view | read starts inside the target at a page-aligned offset, same format, same BW (or BW 1 within the row), rectangle translatable | target view with region [offset, offset + size) |
| Texture shuffle | 16-bit read of a 32-bit target recognised as a shuffle (chapter 09) | target view in shuffle mode (temporary) |
| Index from colour | T8 read of a CT32/CT24 target, same BP or page-aligned inside, or a channel-shuffle pattern | target conversion into an R8 index texture through the T8 layout (chapter 09), at 1x (downsample first when scaled), or the channel-fetch shader path for channel shuffles |
| Alpha as index | T8H/T4HL/T4HH read of a CT32/CT24 target, same BP | target view, palette from alpha byte/nibble |
| 16/24-bit with TEXA | C24 read of a C32 target, C16 read of a C16 target | target view, TEXA in shader |
| Depth as depth | Z-format read of a depth target, same BP and width | target view of the depth image (feedback rules of chapter 08 if it is the bound depth) |
| Depth as colour (scrambled) | colour read at a depth target's BP | GLM path (Decision: exact), because the Z swizzle differs from the colour swizzle |

Channel validity needed:

| Read format | Requirement |
|---|---|
| T4 | RGB and both alpha nibbles |
| T8H | either alpha nibble |
| T4HL | low nibble |
| T4HH | high nibble |
| others | RGB when colour is used; alpha when alpha is used |

If a 24-bit target is read as C32 and alpha is needed, the target's alpha is
reloaded from the SLM (mark the valid area alpha-dirty, update) and treated as
valid.

Alpha range of a target-derived source (recomputed on every reuse): 24-bit:
[AEM ? 0 : TA0, TA0]; 32-bit target: the target's tracked range; 16-bit target:
map the tracked range's top bit to TA0/TA1, with min 0 when AEM is set.

## 4.6 Memory sources: creation and upload

- **Size**: region size when fixed, else 2^TW x 2^TH. Inference from uploads
  (High): with no region, clamp-clamp addressing and a read smaller than
  TW x TH, if an exact (BP, BW, PSM) upload happened in the last 10 batches,
  shrink the texture to max(upload extent, read rounded up to pages) when both
  dimensions shrink.
- **Representation**: indexed formats as an R8 index texture plus a shared
  palette texture (Decision: HWR always uses index textures for indexed
  formats — palette swaps then cost nothing; exception: when replacement or
  dumping needs the expanded image, chapter 11); direct formats as RGBA8 with
  TEXA applied on the CPU.
- **Upload modes**:
  - Whole-texture with hash: textures whose largest side ≤ 512, or ≤ 1024 with
    the other ≤ 256 ("preloadable"), are decoded whole and their content hash
    is kept per level; a re-validation re-uploads only if the hash changed.
  - Incremental (larger textures): a per-block validity bitmap (512 pages x 32
    bits); only blocks the read touches and that are invalid are decoded,
    queued as block rectangles that merge when adjacent (right or down), flushed
    in small groups. Repeating textures index validity by tile coordinates
    modulo the address space.
- Mip levels are separate TEX0 values (from MIPTBP1/2) uploaded into array or
  mip layers; a level whose TEX0 did not change is not re-uploaded.
- Decoding runs on the CPU from the SLM (the M2 swizzle library) or, when
  `GT4_HW_GPU_DECODE=1` (later optimisation), by compute from the GLM.

## 4.7 Hash cache (Confirmed behaviour, adopted)

Content-addressed sharing of memory-source textures:

- Key: 64-bit content hash of all levels used, 64-bit palette hash (0 for index
  textures), TEX0 masked to PSM/TW/TH (no address), TEXA masked to TA0/AEM/TA1
  only for 16/24-bit direct formats, region width and height.
- Hashing: XXH3-64 over raw blocks or over expanded texel rows, exactly as
  chapter 11 defines (the same rules serve replacement lookup).
- Entries are reference-counted by sources; an unreferenced entry ages and is
  evicted after 30 non-idle VBlanks; above 800 entries the oldest unreferenced
  ones are purged; if hash-cache GPU memory exceeds a budget (default 1 GiB, or
  25% of the device-local heap budget when `VK_EXT_memory_budget` exists) the
  cache is dropped and only incremental uploads are used for the session.
- Invalidation never edits an entry: a write over a hash-backed source drops
  the source; the next draw re-hashes.

## 4.8 Invalidation of sources

On every memory write event (upload commit, SLM move destination, GLM-composed
write-back, memory clear) and after every batch for its own frame and Z
rectangles:

- **Memory sources** with shared bits whose pages are touched: hash-backed or
  repeating-without-partial-invalidation → removed; others → validity bits of
  the touched blocks (or tiles) cleared and all layers marked incomplete.
- **Target-derived sources**: removed when the write overlaps their area or
  equals their base or their origin target's base.
- After a batch: memory sources overlapping the written frame/Z block ranges are
  removed (the target is now authoritative there); additionally, when FBW ≥ 16,
  sources at BP + BW·16 blocks with shared bits are purged (second half of a
  double-height buffer; High).

Invariant (debug builds): no source references a deleted target.

## 4.9 GLM reinterpretation path (Decision)

Purpose: exact 1x results for any read the fast paths cannot serve, without a
CPU stall.

GLM = a 4 MiB device-local storage buffer (plus a 2-bit per-block "content
generation" array on the CPU for bookkeeping). To build a GLM source for a read
range R:

1. Copy the SLM bytes of R's blocks into the GLM (upload through the ring; only
   blocks whose GLM generation is older than the SLM's).
2. For every target authoritative for blocks in R, oldest first: run the
   **target swizzle kernel** — for each native pixel of the target's drawn
   rectangle inside R, take the target sample at the native pixel's top-left
   sub-sample (Decision: point sampling, same as the write-back rule of
   chapter 10), convert it to the target's GS format (CT32 raw RGBA; CT24 low
   24 bits; CT16 packed 5:5:5:1; depth formats: integer depth, 24/16-bit
   truncated) and write it into the GLM at the target's own (BP, BW, PSM)
   address, honouring channel validity (write only valid bits: RGB bits if RGB
   valid, alpha nibbles per validity; 16-bit writes whole halfwords).
3. Run the **unswizzle kernel** for the requested (TBP0, TBW, PSM, TW, TH,
   region, levels), producing either an RGBA8 texture (direct formats, TEXA
   applied) or an R8 index texture (indexed formats).
4. The GLM's content for those blocks now equals what the SLM would hold after a
   write-back; record it so step 1/2 can be skipped next time if nothing
   changed (generations).

The GLM path never changes authority (targets stay authoritative; the SLM is
not updated). It is a pure function of SLM + targets. Fixture
`hw_glm_equals_writeback`: for random target contents and every PSM pair, the
GLM-decoded texture must equal the texture decoded from the SLM after a CPU
write-back.

Counters: `glm_sources`, `glm_bytes_swizzled`, GPU time of GLM kernels.

## 4.10 Ageing (Confirmed values, adopted)

Ageing runs once per **non-idle** VBlank (a VBlank with at least one batch or
transfer since the previous one); idle VBlanks age nothing (loading screens
that fade from old targets rely on this).

| Object | Rule |
|---|---|
| Memory source, preloadable size, not hash-backed | removed when age > 30 |
| Other sources | removed when age > 3 |
| Hash-cache entry with refcount 0 | removed when age > 30; > 800 entries → purge oldest |
| Target | when age > 60: deleted only if another target overlaps its block range or its whole valid area is dirty; otherwise age := 10 (keeps GPU-only data that has no other copy) |
| Size memory entry (chapter 05) | removed at age 120 |
| Palette objects | when more than 4096 per size class, unreferenced ones are purged |

Any use of a source resets its age and its origin target's age. Vectors: a
non-preloadable source is removed at age 4, a preloadable one at age 31; a target
unused for 61 non-idle VBlanks and overlapped by nothing survives with age 10.

**Memory budget (Decision)**: HWR tracks target and source memory. If the total
exceeds the device-local budget minus 256 MiB, it (1) drops the hash cache,
(2) deletes sources, (3) writes back and deletes the least recently used
targets that are not displayed and not used in the last 2 VBlanks, in that
order. Counter and log line when it happens.

## 4.11 Counters (F8)

Source lookups, hits, memory-source creations (bytes decoded and uploaded),
hash-cache hits/misses/entries/MiB, target views, target conversions, GLM
sources, temporary sources, sources dropped by invalidation, budget evictions.

## Sources

- Research report `texture-cache-v2.md` sections 0, 1.3 to 1.8, 1.11, 2.8, 3,
  4.1, 7 and 8 (supersedes `texture-cache.md` where they differ).
- Research report `texture-cache.md` sections 1, 3 and 4 (earlier pass).
- Research report `upscaling-clut-readbacks.md` A.5 and B.5 (target-derived
  sources, index textures).
- Research report `draw-translation-ubershader.md` section 2.6 and 2.7
  (sampling of target sources).
- Research report `gt4-gamedb-hacks-donor.md` section 2.4 (why exact
  reinterpretation matters for GT4) and 4.1 (donor's exact-memory model, which
  inspired the GLM decision).
