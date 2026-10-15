# 00 — Overview

Status: specification for TASK-027 (implementation of **HWR**, the GT4Recomp
hardware GS renderer, MIT, clean room per decision 0069). Labels used in all
chapters: **Confirmed** (hardware fact from public documentation, our own
measurements, or behaviour read in the studied reference and recorded by the
research team), **High** (high confidence), **Hypothesis** (must be verified by
the test named next to it), **Decision** (a choice made by the specification
team for HWR; it is not a claim about hardware or about any other renderer).

## 0.1 What HWR is

HWR turns the GS command stream produced by the recompiled game (GIF paths 1,
2 and 3, plus privileged register writes) into Vulkan work that renders at an
**internal resolution** of 1x to 8x the PS2 resolution, keeps the result on the
GPU, and presents it once per guest VBlank. It must:

1. Produce a 1x image that matches our MIT donor renderer on GT4 scenes within
   the thresholds of chapter 14 (the donor executes GS semantics on an exact
   GS memory image, so it is the oracle for memory semantics).
2. Scale geometry, render targets and post-processing to Nx without the classic
   upscaling artefacts (seams, bloom offsets, depth bleed) in GT4 scenes.
3. Support texture dumping and replacement packs (an own format first,
   PCSX2-pack compatibility later, chapter 11).
4. Run GT4 at 60 FPS at 100% speed on the owner's PC (Ryzen 7 5700X,
   RX 9060 XT) at 3x or more, with no CPU-GPU wait on the normal path except
   the LOCAL->HOST transfers the game itself requests.

HWR is **not** an emulator of the GS pipeline at the pixel level (that is what
the donor and paraLLEl-GS do). It is a translator: GS state becomes GPU
pipelines, GS memory regions that are drawn become GPU images, and GS memory
is reconstructed only when someone needs to read it.

## 0.2 Architecture (Decision)

```
          GIF qwords (path 1/2/3)       privileged writes      LOCAL->HOST reads
                  |                            |                        ^
                  v                            v                        |
  +----------------------------------+   +-------------+                |
  | 02 Front end                     |   | display regs|                |
  |  GIF parser, register file,      |   +-------------+                |
  |  vertex queue, batch builder,    |          |                       |
  |  flush rules, auto-flush         |          |                       |
  +----------------------------------+          |                       |
     | uploads/moves/CLUT      | batches        |                       |
     v                         v                |                       |
  +---------------------+   +------------------------------+            |
  | 01/07 Local memory  |   | 03 Draw translator           |            |
  |  shadow image (CPU) |<->|  classify, simplify, choose  |            |
  |  swizzle library    |   |  methods (08, 09), keys,     |            |
  |  CLUT unit          |   |  passes                      |            |
  |  transfer log       |   +------------------------------+            |
  +---------------------+        |  lookups        | GPU passes         |
        ^      |                 v                 v                    |
        |      |      +--------------------+   +--------------------+   |
        |      +----->| 04/05 Surface cache|   | Device layer (12)  |   |
        | write-back  |  targets (Nx),     |-->|  ring, passes,     |   |
        +-------------|  sources, dirty,   |   |  barriers, pipelines|  |
          (10)        |  drawn-since-read  |   +--------------------+   |
                      +--------------------+            |               |
                               |                        v               |
                               |              +------------------+      |
                               +------------->| 13 Output stage  |------+ (10 readbacks)
                                              | merge, deinterl.,|
                                              | post-fx, present |
                                              +------------------+
```

Main components and their single responsibilities:

| Component | Chapter | Owns |
|---|---|---|
| Front end | 02 | GIF tag state per path, the GS register file (two contexts + environment), the vertex queue, the pending batch and its register snapshot, flush decisions |
| Local memory model | 01, 07 | The **shadow local memory** (SLM): a 4 MiB CPU image of GS memory; swizzle/unswizzle for every PSM; host->local, local->local, local->host transfers; the CLUT buffer and CLD logic; the transfer log |
| Draw translator | 03, 08, 09 | Per batch: early-outs, register simplification, primitive expansion, texture/filter/wrap decisions, blend/DATE/alpha-test methods, shuffle detection, pipeline key, pass list |
| Surface cache | 04, 05 | Render targets and depth targets at internal scale, sources (textures) uploaded from SLM or derived from targets, dirty rectangles, drawn-since-read rectangles, channel validity, ageing |
| Upscaling rules | 06 | Scale per target, half-pixel alignment, sprite fixes, native-resolution post-effects |
| Readbacks and sync | 10 | Making SLM coherent with targets for LOCAL->HOST transfers, CPU moves, CLUT loads and diagnostics |
| Texture replacement | 11 | Content hashing, dumps, replacement lookup, asynchronous loading |
| Integration | 12 | Hooks into GT4Recomp, Vulkan device ownership, options, F8 counters, build |
| Output stage | 13 | PCRTC: video mode, circuits, merge, deinterlace, post-processing, presentation |
| GT4 specifics and validation | 14 | Scenes, test vectors, the oracle, thresholds |
| Plan | 15 | Milestones for TASK-027 |

### Data authority rule (Decision, central invariant)

At any moment every byte of GS memory has exactly one **authoritative copy**:

- the SLM, or
- one GPU target (colour or depth) that covers it with valid channels and has
  drawn it since the last write-back.

The surface cache tracks this with two per-target structures: the **dirty
list** (areas where the SLM is newer than the target) and the
**drawn-since-read rectangle** (areas where the target is newer than the
SLM). Every operation in this specification either preserves this rule or
explicitly transfers authority (upload: SLM wins; draw: target wins; write-back:
SLM becomes equal to the target and the drawn area shrinks). A test that
breaks the rule is a bug, even when the image looks right.

### GPU local-memory mirror (Decision)

Some GS idioms reinterpret drawn pixels in a layout no conversion shader can
reproduce cheaply (for example a 32-bit colour target read as PSMT4, or a Z
buffer read as colour in its scrambled layout). PCSX2-style renderers solve
those with a CPU readback. HWR instead keeps an optional **GPU local-memory
mirror** (GLM): a 4 MiB device-local storage buffer holding GS memory at 1x,
with compute kernels that (a) swizzle a target's drawn area into it
(downsampling to 1x by taking the top-left sample of each native pixel),
(b) copy SLM pages into it, and (c) unswizzle any PSM rectangle out of it into
a texture. The generic reinterpretation path (chapter 04, section 4.9) builds
the GLM content for the read range from SLM plus every overlapping target, then
decodes. It is exact at 1x, never stalls the CPU and is used only for rare
cases; the common cases have direct fast paths.

### Reference mode (Decision)

`GT4_HW_SYNC_MODE=writeback` is a debug mode in which, after every batch, all
targets touched by the batch are written back to the SLM (chapter 10) and every
source is decoded from the SLM. It is slow but has almost no cache logic, so it
is the first thing built (milestone M4) and it remains the in-tree oracle for
the full cache: any scene where `cache` and `writeback` modes differ at 1x is a
cache bug.

## 0.3 Processing of one frame (normative order)

1. The front end receives GIF data and privileged writes in guest order.
2. Register writes update the live register file; a vertex kick may complete a
   primitive and append it to the pending batch; a state change that matters
   to the pending batch flushes it first (chapter 02).
3. Host->local transfers are applied to the SLM in stream order; before that,
   a pending batch that reads or writes the destination is flushed, and the
   surface cache marks the destination dirty (chapter 02, 04).
4. A TEX0/TEX2 write with a loading CLD snapshots the palette from local memory
   at that moment (chapter 07). If the palette source area is owned by a
   target, the CLUT unit obtains it from the GPU (GPU CLUT) or forces a
   write-back (chapter 07, 10).
5. A flushed batch goes to the draw translator: lookups of frame target,
   depth target and source; method choices; GPU passes recorded into the
   current command buffer (chapter 03 to 09).
6. LOCAL->HOST transfers flush pending work, write back the needed target
   areas, wait for the GPU, then serve data from the SLM (chapter 10).
7. At each guest VBlank the output stage reads the display circuits from the
   cache (or the SLM), merges, deinterlaces, post-processes and presents
   (chapter 13). The cache ages its objects (chapter 04).

SIGNAL, FINISH and LABEL are reported to the donor CSR logic inline, in stream
order, when the GIF parser reaches them (chapter 12). They never wait for the
GPU.

## 0.4 Non-goals for phase 1 (Decision)

- No software rasterizer fallback inside HWR (the donor renderer remains the
  fallback renderer, selected by `GT4_RENDERER=donor`).
- No save states (refused, same as paraLLEl-GS).
- No CPU CLUT rendering, CPU sprite rendering, or other "draw on the CPU"
  shortcuts; the GLM covers those needs on the GPU.
- No per-game hack table beyond the GT4 behaviours of chapter 14, and those are
  keyed on observed GS patterns, never on a CRC or a game serial.
- No ROV (fragment shader interlock) path in phase 1; it may be added later as
  an optional accelerator (chapter 08).
- No HDR or 10-bit output; RGBA8 targets and an RGBA16 colour-wrap side target
  only.

## 0.5 Reading order for implementers

Chapter 01 (hardware model) is the foundation; every implementer reads it.
Then: front end implementers read 02; draw translation 03, 08, 09; cache 04, 05,
10; scaling 06; palettes 07; replacement 11; device and output 12, 13; testers
14. Chapter 15 says which chapters each milestone needs.

## Sources

- Research report `gs-state-gif-flush.md` (section 0, model overview).
- Research report `draw-translation-ubershader.md` (section 0, layers).
- Research report `texture-cache-v2.md` (sections 1 and 5, data authority).
- Research report `upscaling-clut-readbacks.md` (part C.1, coherence points).
- Research report `output-postfx-vulkan-perf.md` (A.1, pipeline order).
- Research report `gt4-gamedb-hacks-donor.md` (sections 3 to 5, GT4
  requirements and the donor renderer).
- Decision 0069 and chapter 12 of this specification.
