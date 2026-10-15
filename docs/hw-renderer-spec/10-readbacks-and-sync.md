# 10 — Readbacks and synchronisation

A **write-back** copies a target's drawn pixels into the SLM so the SLM
becomes authoritative again for those bytes. It is the only operation that
makes the CPU wait for the GPU, so HWR performs it only at the coherence points
below and keeps every one small.

## 10.1 Coherence points (Decision; triggers are Confirmed behaviour)

| Trigger | What must be coherent | Wait? |
|---|---|---|
| LOCAL->HOST transfer (TRXDIR = 1) | the source rectangle | yes: the EE is reading now |
| SLM move fallback (GPU move impossible) | the move's source rectangle | yes |
| `map_vram` / VRAM frame dump / `GT4_HW_SYNC_MODE=writeback` | requested range / every touched target | yes (diagnostic paths) |
| CLUT load from target blocks | palette area | **no**: GPU CLUT (chapter 07.7) |
| Reinterpretation without a fast path | read range | **no**: GLM (chapter 04.9) |
| Shutdown, renderer reset with `GT4_HW_READBACK_ON_RESET=1` | everything drawn | yes |

HWR never performs a write-back for texture conversion, CLUT draws or software
fallbacks (it has none).

## 10.2 LOCAL->HOST flow (Confirmed order, adopted)

1. TRXDIR = 1 arms the read. Expected size in qwords = RRW·RRH·bpp/128, bpp from
   SPSM & 7 (0 → 32, 1 → 24, 2 → 16, 3 → 8; 4-bit is illegal; unknown → 32).
2. On the first read request (`read_transfer_fifo`, called by the donor's FIFO
   read path), in this order: run the overlap check (chapter 02.7) and flush
   the pending batch if needed; commit pending uploads; **select and write back**
   the targets for the source rectangle (10.3, 10.4); submit, wait (10.5); read
   the whole rectangle from the SLM into a staging buffer honouring SPSM's
   swizzle (X wraps at 2048; reads past the end of memory are split).
3. Later requests copy out sequentially; when all data was read TRXDIR := 3.
   Reading past the end returns zeros and counts a fault (chapter 12.3).

**Shortcut** (Confirmed): if the EE uploaded exactly this rectangle (same base,
PSM and rectangle) since the last batch, nothing was drawn over it: skip the
write-back (the SLM already holds the data).

## 10.3 Selecting what to write back (Confirmed behaviour, adopted)

Inputs: base, width, PSM and rectangle of the read; its block range.

1. If the read PSM is ≥ 16 bpp: scan **depth** targets least recently used
   first. Candidate: same bpp with shared channels, and either the read's first
   page equals the target base or the read lies within the target's
   page-rounded range. A depth target flagged as having held 32-bit data while
   labelled Z24 is treated as Z32. If the read is inside a same-kind depth
   target ("found in Z"): if the area is entirely dirty or nothing was drawn,
   there is nothing to do; otherwise write back and **stop** (colour targets are
   not examined — handles a buffer used as Z and as a reflection map).
2. Scan **colour** targets least recently used first in two passes: exact (same
   page base and inside, or same bpp, width and inside) then loose (any overlap
   with shared bits).
3. Per candidate, the rectangle to write back is the intersection of the read
   (translated into the target, page aligned when layouts differ) with the
   drawn rectangle, **unless** the target has been read more than once since its
   last draw (repeated strip reads), the read is a full-flush request, or
   `GT4_HW_READBACK=full`: then the whole drawn rectangle.
4. Skip when: nothing drawn since the last write-back; the read is wholly
   dirty; every touched page lies inside the dirty area (the SLM is newer).
5. If the target has dirty rectangles intersecting the area, apply them first
   (update), then write back.
6. After writing back, shrink the drawn rectangle: clear it if covered; if the
   read was a full-width band at the top or bottom edge, trim vertically; if a
   full-height band at the left or right edge, trim horizontally.
7. Stop after an exact-base target that fully contains the read.

## 10.4 The write-back itself (Confirmed behaviour, adopted)

- Downloadable formats: CT32, CT24, Z32, Z24 (32-bit path), CT16, CT16S, Z16,
  Z16S (16-bit path). Others are no-ops (and should never be authoritative).
- Conversion pass into a native-size temporary (skipped for an unscaled colour
  target with no alpha scaling, which is copied directly):
  - colour 32-bit: RGBA bytes, alpha de-scaled first when alpha-scaled;
  - depth 32/24: integer d·2^32;
  - colour 16-bit: pack 5:5:5:1 (top 5 bits per channel, alpha bit 7);
  - depth 16: integer depth masked to 16 bits;
  - scaled targets: **point sample** the top-left sub-sample of each native
    pixel (Decision; the same rule as the GLM swizzle kernel, so GLM and
    write-back agree bit for bit).
- Copy to a host-visible staging buffer (one per format class: RGBA8, uint16,
  uint32; grown to the largest size requested, never shrunk; row pitch per
  device alignment), barrier transfer → host read.
- Swizzle into the SLM with the target's own (BP, BW, PSM). **Channel mask for
  32-bit paths**: write RGB bits only if RGB is valid, bits 24..27 only if alpha
  low is valid, 28..31 only if alpha high is valid; nothing if none — memory
  bits the GPU never owned must not be overwritten. 16-bit writes are whole.
- Refuse (and log) when dirty data intersects the rectangle at this point.
- Vector V-10-1: 2x CT32 target, rectangle 64x32 at (0,0) → a native 64x32
  point-sampled image in the SLM; with alpha invalid only the low 24 bits change.

## 10.5 Waiting (Decision)

- The write-back copies are recorded into the current command buffer; HWR
  submits and waits on the **timeline semaphore value** of that submission
  only (not a queue idle). If the copies are in an already-submitted batch,
  wait for its value.
- Invalidate the mapped range when memory is not host-coherent.
- The wait is accounted as `gs_wait` in F8 and also histogrammed
  (`readback_wait_us`, count, bytes per interval).
- Optional `GT4_HW_READBACK_SPIN=1` busy-polls the semaphore instead of
  sleeping (lower wake-up latency; measure before enabling by default).
- Pending GPU work recorded before the read but not needed for it still
  executes before the copy (in-order queue); keeping batches small near reads
  is a later optimisation (submit at the last flush before a FIFO read).

## 10.6 Modes (Decision)

`GT4_HW_READBACK=exact|full|none`:

- `exact` (default): 10.3 as written.
- `full`: always the whole drawn rectangle (fewer, larger waits; diagnostic).
- `none`: no write-backs; FIFO reads return SLM contents as they are (stale);
  an A/B switch for performance investigation only. Never default.

## 10.7 SIGNAL, FINISH, LABEL (Confirmed)

These are handled by the donor's CSR logic when the GIF parser reaches them,
inline and in stream order (chapter 12.3): SIGNAL merges ID into SIGLBLID.SIGID
under IDMSK, sets CSR.SIGNAL, raises the interrupt unless masked, and a second
SIGNAL while CSR.SIGNAL is set stalls the path until the EE clears it; FINISH
sets CSR.FINISH when the GIF is idle and interrupts once unless masked; LABEL
merges into LBLID without interrupt. They mean "the GS consumed the commands",
not "pixels are in memory": HWR reports them after recording (not executing)
preceding draws and never waits for the GPU. Whether GT4 uses them is Unknown;
F8 counts them.

## 10.8 GPU-side moves avoid write-backs

Local->local transfers are executed between targets on the GPU whenever chapter
05.7 allows; the SLM is not updated and the move is logged as GPU-only. Only
when that fails does HWR write back the source and move in the SLM.

## 10.9 Expected GT4 behaviour (Hypothesis, measured in M9)

Normal gameplay should need ~0 write-backs per frame, except effects that read
the frame or Z to the EE (lens flare / sun intensity in night races is the
suspect: donor GS wait 4.1–4.3 ms per frame there). Acceptance: F8 shows the
count and wait per scene; at night race the total `gs_wait` per frame must be
below 1 ms on the owner's PC (paraLLEl-GS achieves steady 60 FPS there with
page-tracked readbacks).

## Sources

- Research report `upscaling-clut-readbacks.md` part C (C.1 to C.8) — final and
  earlier versions.
- Research report `texture-cache-v2.md` sections 4.5 and 5.
- Research report `texture-cache.md` section 5.
- Research report `gs-state-gif-flush.md` 4.3, 4.4.
- Research report `output-postfx-vulkan-perf.md` B.3, B.4, B.10 (staging,
  submission, waits).
- Research report `gt4-gamedb-hacks-donor.md` 3.3, 3.4 (night-race waits,
  SIGNAL/FINISH/LABEL usage unknown).
