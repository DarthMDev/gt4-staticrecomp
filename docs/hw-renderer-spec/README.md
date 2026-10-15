# HWR specification (TASK-026)

Behavioural specification of **HWR**, GT4Recomp's own MIT hardware GS
renderer, written by the specification team under the clean-room process of
[decision 0069](../decisions/0069-own-hardware-renderer-clean-room.md). TASK-027
implements it.

## Chapters

| # | File | Content |
|---|---|---|
| 00 | [00-overview.md](00-overview.md) | Goals, architecture, data-authority rule, GPU memory mirror, reference mode, frame order, non-goals |
| 01 | [01-gs-hardware-model.md](01-gs-hardware-model.md) | GS registers, local memory layout and swizzles, pixel pipeline formulas, primitives, CLUT buffer, display registers |
| 02 | [02-gs-front-end-and-batching.md](02-gs-front-end-and-batching.md) | GIF parser, register writes, vertex kicks, batches, flush rules, auto-flush, transfers |
| 03 | [03-draw-translation.md](03-draw-translation.md) | One batch to GPU work: early-outs, simplification, primitive expansion, vertex/pixel stages, textures, filtering, mipmaps, depth, pipeline keys, passes |
| 04 | [04-texture-cache.md](04-texture-cache.md) | Sources, hash cache, palettes, source lookup, invalidation, GLM reinterpretation, ageing |
| 05 | [05-render-targets-and-depth.md](05-render-targets-and-depth.md) | Targets, sizes, lookup and creation, preload, colour/depth aliasing, moves, clears, display lookup |
| 06 | [06-upscaling.md](06-upscaling.md) | Internal resolution, scale per target, half-pixel alignment, sprite fixes, native post-effects |
| 07 | [07-clut-and-palettes.md](07-clut-and-palettes.md) | CLUT loading, CSM1/CSM2, decode with TEXA, palette objects, GPU CLUT from targets |
| 08 | [08-blending-date-alpha-test.md](08-blending-date-alpha-test.md) | Exact blend, blend levels, colour wrap, DATE, alpha test, FBMASK, dithering, feedback and barriers |
| 09 | [09-shuffles-and-gt4-effects.md](09-shuffles-and-gt4-effects.md) | Texture shuffles, channel shuffles, the Polyphony brightness/fade patterns, double clears |
| 10 | [10-readbacks-and-sync.md](10-readbacks-and-sync.md) | Write-back rules, LOCAL->HOST flow, GPU moves, coherence points, SIGNAL/FINISH/LABEL |
| 11 | [11-texture-replacement.md](11-texture-replacement.md) | Dumps and replacement: own format (first), exact PCSX2-pack compatibility (later milestone) |
| 12 | [12-integration-with-gt4recomp.md](12-integration-with-gt4recomp.md) | Hooks, backend interface, device ownership, presentation, options, F8 counters, build |
| 13 | [13-output-and-postfx.md](13-output-and-postfx.md) | PCRTC, merge, interlace and deinterlace, post-processing, presentation rectangle |
| 14 | [14-gt4-specifics-and-validation.md](14-gt4-specifics-and-validation.md) | GT4 facts, scenes, oracle (donor at 1x, capture replay), metrics, thresholds, test vectors |
| 15 | [15-implementation-plan.md](15-implementation-plan.md) | Milestones M0..M19 with acceptance checks, effort, dependency graph, risks, open owner questions |

## How to use this specification

- Chapter 00 then chapter 01 first; then the chapters your milestone lists in
  chapter 15.
- Statements carry labels: **Confirmed**, **High**, **Hypothesis**,
  **Decision**. A Hypothesis is always paired with a verification test; the
  implementation must include that test (CTest fixture or chapter 14 scene) and
  report the result before the behaviour is relied upon.
- "Must" is normative; "should" is a strong recommendation that can be
  overridden with a recorded reason; "may" is optional.
- Test vectors in this specification are normative: each becomes a CTest case.
- Where the specification is silent or ambiguous, the implementer asks the
  specification team (through the main agent) and the answer is added to the
  relevant chapter in prose. Do not fill gaps by reading other renderers.

## Clean-room rules for implementers (TASK-027)

1. **Never open PCSX2 GS sources, PCSX2 shaders, GSdx-derived code, ARMSX2's
   GS code or any other GPL GS renderer**, including through search results or
   copied snippets. The PCSX2 checkout under `private/tooling/` is off limits
   for TASK-027 agents.
2. Allowed inputs: this specification; public hardware documentation (GS
   User's Manual knowledge, ps2tek, ps2sdk headers); our MIT donor renderer
   (`private/references/ps2-web-recomp`, MIT, Copyright 2026 Samir Nuri; its
   swizzle tables may be reused with attribution); the Vulkan and GLSL
   specifications; permissively licensed libraries listed in chapter 12
   (xxHash, a PNG codec, VMA, AMD FidelityFX CAS headers, NVIDIA's public FXAA
   3.11 release or our own FXAA).
3. Every HWR source file carries the MIT header plus the line "Written from the
   TASK-026 specification (docs/hw-renderer-spec)". Every commit message names
   the chapter sections it implements.
4. Names in code are our own. Do not try to guess PCSX2 identifiers; this
   specification deliberately uses its own vocabulary (target, source, dirty
   list, drawn rectangle, transfer log, GLM, SLM).
5. If a behaviour seems to need knowledge not in this specification, stop and
   ask. Do not reconstruct it from memory of other emulators.

## Clean-room rules for the specification team

- Describe behaviour, invariants, formulas, tables and test vectors in our own
  words and structure. No code, no pseudocode mirroring a studied function, no
  copied comments or shader text. Hardware facts may be precise.
- Every chapter ends with a **Sources** section listing the research reports
  and public documents it draws on. The research reports themselves (in the
  TASK-026 session scratchpad, not in the repository) list the studied PCSX2
  file ranges; they were written as prose by the research team.
- Where research reports disagree or a statement is a Hypothesis, keep the
  label and add a verification test.

## Glossary

| Term | Meaning |
|---|---|
| SLM | Shadow local memory: the CPU-side 4 MiB image of GS memory |
| GLM | GPU local-memory mirror: an optional 4 MiB device buffer used for exact reinterpretation at 1x |
| Target | A GPU colour or depth image representing a GS buffer (base, width, format) at the internal scale |
| Source | A GPU texture sampled by a draw: decoded from the SLM, or derived from a target |
| Dirty list | Rectangles of a target where the SLM is newer |
| Drawn rectangle | Area of a target drawn since its last write-back (target newer than SLM) |
| Batch | The primitives accumulated between two flushes, drawn with one register snapshot |
| Block / page | 256-byte / 8 KiB units of GS memory |
| BP, BW, PSM | Block pointer, buffer width (64-pixel units), pixel storage format |
| Scale | Internal resolution multiplier of a target |
| Write-back | Copying a target's drawn pixels into the SLM (a readback) |

## Sources

- Decision 0069; TASKS.md TASK-026 and TASK-027.
- All nine research reports (listed per chapter).
