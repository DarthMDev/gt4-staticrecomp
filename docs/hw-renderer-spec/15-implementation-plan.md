# 15 — Implementation plan (TASK-027)

Milestones for the implementation team (Haiku implementers by default; the main
agent escalates to Opus or takes over when a milestone stalls, per project
rules). Each milestone is small enough for one branch-sized piece of work, has
exact acceptance checks, and leaves the game runnable with the donor renderer
unchanged when `GT4_RENDERER` is unset.

Rules for every milestone:

- Read README (clean-room rules), chapter 00, chapter 01, and the chapters
  listed for the milestone. Never open PCSX2 sources.
- One writer at a time; all new behaviour gets CTest or Python fixtures named as
  in chapter 14.7; run CTest and the Python suite against the build under test
  (`GT4_NATIVE_BUILD=<dir>`); new candidate builds use fresh `private/sliceNNN-*`
  directories and new `docs/inputs/*.json` pins.
- Every commit names the chapter sections it implements and carries the
  provenance line of chapter 12.11.
- Effort is in implementer-days (one focused agent day, including tests);
  "owner test" marks the points where the owner plays.

## 15.1 Milestones

| M | Title | Chapters | Effort |
|---|---|---|---|
| M0 | Scaffolding: backend skeleton, dispatch, options, device, black present | 12, 13.6 | 3 |
| M1 | Capture replay and comparison tooling | 14.3, 14.4 | 3 |
| M2 | Local memory model: swizzle library, SLM, transfers, reinterpretation maps | 01.3, 02.7, 09.1, 10.2 | 3 |
| M3 | Front end and CLUT unit | 02, 07.1–07.5 | 5 |
| M4 | Reference renderer I: geometry, texturing, depth, write-back mode | 03, 04.6 (memory sources only), 05.1 (minimal), 10.4 | 6 |
| M5 | Reference renderer II: exact blending, alpha test, DATE, FBMASK, colour wrap (copy feedback) | 08 (SHADER method, FULL DATE, PASS_THEN_FAIL), 03.10 | 5 |
| M6 | Output stage basic and first playable HWR (slow) | 13.1–13.3, 13.6, 12.4, 12.5 | 4 |
| M7 | Surface cache: persistent targets, dirty/drawn tracking, preload, memory sources, hash cache, invalidation, ageing, display lookup | 04, 05.1–05.6, 05.9, 05.10 | 8 |
| M8 | Readbacks, GPU moves, F8 block | 10, 05.7, 12.7 | 4 |
| M9 | Feedback mechanisms, draw lists, blend levels, DATE/alpha-test methods, auto-flush, first performance pass | 08, 02.6, 02.10, 03.9–03.11 | 6 |
| M10 | GLM, target fast paths, texture and channel shuffles, GT4 patterns | 04.5, 04.9, 09, 07.7 | 7 |
| M11 | Clears, colour/depth aliasing, offset targets, temporary Z | 05.3–05.5, 05.8, 05.11 | 5 |
| M12 | Upscaling 2x–8x | 06 | 5 |
| M13 | Mipmapping, dithering, AA1 constant alpha | 03.6, 08.10, 03.1 | 4 |
| M14 | Accuracy-preserving optimisations: alpha-scaled targets, PRIM_ID DATE, ACC/MIX, GPU decode | 08.3, 08.7, 04.6 | 5 |
| M15 | Deinterlacers, post-processing, sprite fixes, native post-fx | 13.4, 13.5, 06.4, 06.6 | 4 |
| M16 | Texture dumping and replacement, Format G | 11.1, 11.2, 11.4–11.7 | 5 |
| M17 | Performance and stability: budget, stutter, readback waits, 3x at 60 FPS | 04.10, 10.5, 12.7, 12.10 | 5 |
| M18 | PCSX2-pack compatibility, Format P | 11.3 | 5 |
| M19 | Qualification: owner approval to offer HWR in the launcher | 12.2, 14 | 2 |

Total ≈ 94 implementer-days.

## 15.2 Acceptance per milestone

**M0 — Scaffolding** (3 d)
- `native/hw_gs/` with `hw_gs_backend.hpp` (Vulkan-free), backend stubs for
  every operation of chapter 12.3; renderer dispatch front end; `GT4_RENDERER=hw`
  accepted, other unknown values abort.
- Option parser for every `GT4_HW_*` variable of chapter 12.8; unknown values
  abort; values logged and written to `result.json`.
- Vulkan instance/device/swapchain with the feature report (12.6) and the
  RDNA4 recreation rule (13.6); one present per VBlank of a black 16:9 frame.
- Checks: CTest `hw_options`, `hw_present_rect`; Python
  `test_hw_launcher_options`; the existing "donor path byte-identical when the
  variable is unset" test extended to HWR mode; the game boots with
  `GT4_RENDERER=hw` (black screen, audio runs) and closes normally; toggling
  fullscreen 5 times causes no stall > 100 ms (log).

**M1 — Replay and comparison tooling** (3 d)
- `tools/gs_replay`: reads `GT4_PGS_CAPTURE` files, drives the donor GS
  (and later HWR through the backend interface), honours 'R' records, writes
  VRAM-decoded display PPMs per frame.
- `scripts/hw_compare.py`: pct_bad, max_delta, PSNR per frame and per scene,
  thresholds from chapter 14.4, JSON report.
- Checks: donor replay of a menu capture is deterministic (two runs
  byte-identical); replayed frames equal the frames of the live donor run that
  recorded the capture (same frame numbers); Python `test_hw_replay_compare`
  with synthetic images (known PSNR).
- Dependency: one owner capture of S-02 (Q-01). Can run in parallel with M2.

**M2 — Local memory model** (3 d)
- Swizzle/unswizzle for every PSM, SLM, host->local (all bpp, 24-bit packed,
  odd-width 4-bit), local->local in the SLM with DIR semantics and X wrap,
  local->host from the SLM; reinterpretation maps; format predicate tables.
- Checks: `hw_swizzle_matches_donor` (all PSMs, random 4 MiB round trip, byte
  identical to the donor tables), `hw_reinterp_maps` (brute force), `hw_transfers`
  (vectors including odd-width T4 and wrap), V-01-3 lane test, V-01-4 recorded.

**M3 — Front end and CLUT** (5 d)
- GIF parser (PACKED/REGLIST/IMAGE, split at any qword), register file with
  normalisation, PRIM/PRMODE, vertex queue and kick, batches with snapshot,
  dirty bits and the flush rules, continuation across flushes, ST rounding,
  vertex trace, overlap and draw lists, transfer commit order, VBlank rule,
  inline SIGNAL/FINISH/LABEL to the donor, CLUT load/decode with CLD, filter,
  snapshot semantics. Draws are counted, not rendered.
- Checks: `hw_gif_split`, `hw_flush_rules`, `hw_vertex_trace`, `hw_st_rounding`,
  `hw_clut_load` (CLD sequence, CSA clamp, CSM1 positions, 16-bit TEXA decode),
  `hw_clut_snapshot`; replay of the S-02 capture: after each frame the SLM equals
  the donor VRAM in every block no draw wrote (mask from the donor's draw
  rectangles); batch counts within ±1% of the donor's primitive batching (report
  only).

**M4 — Reference renderer I** (6 d)
- Write-back mode only (`GT4_HW_SYNC_MODE=writeback` forced): per batch,
  target from the SLM (preload whole), draw, write back, drop target.
- Geometry (points, lines, triangles, sprite expansion, flat shading), vertex
  mapping at 1x, scissor, depth (format clamp, floor), TFX, fog, texture
  sampling from memory sources (direct RGBA8 and index + palette), wrap modes
  including region modes, filtering, no mipmaps; alpha test KEEP only; no
  blending yet (ABE treated as opaque, counted).
- Checks: `hw_flat_provoking`, coverage vectors of chapter 03.3, draw-rectangle
  table, TFX/fog vectors through real shaders; replay S-01 and S-02 vs donor
  (report-only metrics recorded in the journal).

**M5 — Reference renderer II** (5 d)
- SHADER blending (all 81 modes) with copy-based Cd reads (no feedback
  extension yet), PABE, COLCLAMP wrap in the shader, FBA, 16-bit output
  truncation, software FBMASK, DATE FULL, alpha test PASS_THEN_FAIL and
  SPLIT_* methods, static folding.
- Checks: `hw_blend_vectors` (table 08.1), `hw_atest_fold`, V-01-8, V-01-9,
  V-08-1; S-01/S-02 vs donor meet the 2D thresholds (gating starts at M8, but
  must already pass here).

**M6 — Output basic, first playable** (4 d) — **owner test 1**
- Video mode, circuit rectangles, offsets, anti-blur, merge (both circuits,
  BGCOLOR), field phase from the donor's field flag, `auto`/`off` deinterlace,
  16:9 present, frame dumps (`vram`, `scanout`).
- Checks: `hw_pcrtc_rects` (vectors 13.2), V-01-12 counter; headless schedule
  with `GT4_RENDERER=hw` produces 42 dumps; owner: menus and one race at 1x
  look right (slow is expected; F8 shows the write-back cost).

**M7 — Surface cache** (8 d) — highest-risk milestone, escalate early
- Persistent targets and lists, valid/drawn rectangles, dirty lists with merge,
  update, size memory, preload via transfer log, channel validity, memory
  sources with incremental and hashed upload, hash cache, palette objects,
  invalidation (sources and targets, page-by-page), type-exclusive invalidation,
  ageing, memory budget, display lookup. Fast path: same-format target views
  only; everything else falls back to write-back (GLM comes in M10).
- Checks: `hw_cache_geometry` (overlap with wrap, translation, surface offset,
  dirty re-expression), `hw_dirty_merge`, `hw_ageing`; **cache mode equals
  write-back mode** on S-01..S-04 captures (chapter 14.4 gate); frame time on
  S-03 at 1x drops below 16.7 ms on the owner's PC (report).

**M8 — Readbacks, moves, F8** (4 d)
- Exact LOCAL->HOST flow (10.2–10.5), modes `exact|full|none`, GPU moves (shuffle,
  page, general) with fallback, transfer-log GPU-only records, F8 HWR section
  with all counters listed so far.
- Checks: V-10-1, Python `test_hw_f8_block`; S-05 capture replay shows the
  readback count and bytes; 2D gating against the donor becomes mandatory
  (S-01, S-02, S-07 menus).

**M9 — Feedback, barriers, methods, auto-flush, performance 1** (6 d) — **owner test 2**
- Feedback-loop layout / raster-order access / input attachment / copy
  fallbacks; one-barrier vs draw-list barriers; blend levels; DATE FULL,
  STENCIL, STENCIL_ONE; alpha-test FEEDBACK; auto-flush `sprites|all|off`;
  pipeline cache on disk; pass merging; ring buffers; push descriptors.
- Checks: `hw_autoflush` (synthetic feedback sprites: each sprite must see
  the previous sprites' pixels; expected image computed by the fixture), S-05
  night race vs
  donor; owner plays S-03, S-04, S-05 at 1x: 60 FPS, effects present, F8
  "pipelines compiled" drops to ~0 on a second run.
- Note: the donor snapshots feedback per batch, so for auto-flush cases the
  oracle is the synthetic fixture's expected image, not the donor.

**M10 — GLM, fast paths, shuffles, GT4 patterns** (7 d)
- GLM buffer and kernels (target swizzle, SLM copy, unswizzle), GLM sources,
  all fast paths of 04.5, texture shuffles, channel-shuffle runs with
  reinterpretation maps, GPU CLUT from targets (including via GLM),
  `GT4_HW_VERIFY_SHUFFLE`, the GT4 pattern HLE only if needed (9.4).
- Checks: `hw_glm_equals_writeback`, V-09-1, V-07-1, channel-shuffle vector of
  chapter 08 research (identity CLUT, A → A); S-07 and S-08 vs donor and with
  verify mode (0 mismatches); 3D gating becomes mandatory (S-03..S-11).

**M11 — Clears, aliasing, offsets** (5 d)
- Target clears, memory clears, double-half and striped double clears,
  colour↔depth conversions, 16↔32 reinterpretation, offset/negative-offset
  targets, double-buffer midpoint, combine inside targets, temporary Z; log
  GT4's 480p DISPLAY values.
- Checks: V-05-1, double-half vector (FBP 0, ZBP page 70), 32→16 vector
  (640x224 → 640x448); all scenes still pass the 1x gates; `preload_full` and
  write-back counts do not increase versus M10.

**M12 — Upscaling** (5 d) — **owner test 3**
- Scale per target, device ceiling, half-pixel modes, native compensations,
  points/lines at scale, box downsample, dirty-upload filtering, index from
  scaled targets, dither modes, SCANMSK native rows.
- Checks: every 1x gate unchanged; s x s box-downsampled 2x/3x/4x frames vs 1x
  (thresholds 14.4, tuned and recorded); half-pixel and mod vectors; owner plays
  S-12 at 3x and 4x: no seams, bloom aligned, no edge halos.

**M13 — Mipmaps, dither, AA1 constant alpha** (4 d)
- `GT4_HW_MIPMAP=off|basic|full` with the base-LOD rule, MTBA, explicit LOD;
  dithering on 16-bit frames; AA1 constant-alpha handling.
- Checks: MTBA vector, V-01-10 and V-01-11 counters/fixtures; comparisons vs
  paraLLEl-GS at 1x on S-03 and S-10 (if TASK-012 is merged; else owner
  screenshots, Q-09).

**M14 — Accuracy-preserving optimisations** (5 d)
- Alpha-scaled targets with conversions, PRIM_ID DATE, ACC and MIX methods for
  `basic`/`medium`, GPU decode of memory sources from the GLM.
- Checks: all gates unchanged at `high`; frame time on S-10 at 3x improves
  (report); `basic` level documented with its known differences.

**M15 — Deinterlace and post-processing** (4 d)
- weave/bob/blend/adaptive (our algorithm), ShadeBoost, FXAA (public 3.11 or
  own), CAS (AMD MIT headers vendored with notice), scanlines; sprite fixes and
  native post-fx options.
- Checks: deinterlace on synthetic field sequences (static image stays
  static, moving bar interpolates); provenance entries in
  `native/THIRD_PARTY.md`; owner looks at menus with each deinterlacer and
  picks a default (Q-13).

**M16 — Texture replacement, Format G** (5 d)
- Key computation, dumping (PNG writer), folder scan, async loader with
  placeholders and injection, PNG/DDS loaders, BC support with CPU fallback.
- Checks: dump → edit → reload round trip on a menu texture (owner sees the
  edited texture); Format G key stability across scales and options; V-11-1.

**M17 — Performance and stability** (5 d) — **owner test 4**
- Memory budget behaviour, pipeline-compile stutter (precompile recorded keys
  at start), readback wait tuning (night race < 1 ms `gs_wait` per frame), GPU
  timing queries, long-session leak check (1 hour, VRAM stable).
- Checks: owner plays every scene at 3x: 60 FPS, smooth frametime (F8), no
  hitch > 50 ms after warm-up.

**M18 — PCSX2-pack compatibility, Format P** (5 d)
- Exact hashing (paths A/B, byte-offset quirk, mips, CLUT hash), name parsing
  incl. legacy forms, folder/zip rules, DDS alpha quirks.
- Checks: the owner's PCSX2 dump names for a replayed GT4 capture (Q-07)
  reproduced bit for bit by HWR; a community pack (if the owner has one) loads.

**M19 — Qualification** (2 d)
- README/STATUS/journal updates, `GT4_HW_GS` CMake default decision, launcher
  integration notes for TASK-019, known issues list.
- Owner approval decides whether HWR becomes selectable (and/or default) in the
  release (Q-02).

## 15.3 Dependency graph

```
M0 ──┬──> M3 ──> M4 ──> M5 ──> M6 ──> M7 ──> M8 ──> M9 ──> M10 ──> M11 ──> M12 ──> M17 ──> M19
     │                                  │              │                     │
M2 ──┘ (M2 before M3)                   │              ├──> M13              ├──> M15
M1 ───────────────(needed by M4 acceptance)            └──> M14              │
                                        └──> M16 ──> M18 ────────────────────┘
```

M1 and M2 can proceed in parallel after M0 (different files; still one
repository writer at a time). M13, M14 and M16 can be reordered with M11/M12
when the owner prefers (for example replacement packs before upscaling).

## 15.4 Risks

| # | Risk | Impact | Mitigation |
|---|---|---|---|
| R1 | Surface-cache complexity (the studied renderer accumulated many heuristics) | wrong images, long debugging | authority rule (chapter 00), write-back reference mode, GLM fallback instead of guessing, cache-vs-write-back gate from M7 |
| R2 | Oracle gaps: donor has no mipmaps, dither, merge, per-primitive feedback | undetected errors in those features | paraLLEl-GS as secondary oracle, synthetic fixtures, owner checks |
| R3 | Capture availability (owner time; captures are private game data) | late validation of race scenes | ask early (Q-01); headless schedule covers menus |
| R4 | Clean-room contamination (implementers recalling other renderers) | licence risk | rules in README, provenance lines, review of commits by the main agent; this spec uses its own structure and names (not legal advice) |
| R5 | Feedback/barrier cost on AMD RDNA4; raster-order extension availability unknown | frame time at high blend level | measure in M9; draw-list grouping; copy fallback; `basic` level as escape hatch |
| R6 | Auto-flush splits produce thousands of tiny batches (lens flare) | CPU cost | keep pass and pipeline state across splits; measure in M9 on S-05 |
| R7 | GT4 shuffle sequences not handled by the generic path | wrong fades or brightness | verify mode against GLM; pattern HLE fallback (09.4) |
| R8 | Readback waits in night races | frame drops | exact small write-backs, timeline waits, spin option, M17 budget |
| R9 | Pipeline compilation stutter on first use | hitches | disk pipeline cache, key recording and precompile (M17) |
| R10 | Format P hash mismatches (mip/base-LOD, region inference, TEXA) | packs do not load | owner-generated vectors (Q-07), try full-chain and base-only keys |
| R11 | Hypotheses wrong (FFMD naming, merge alpha, mip rounding, dither on 32-bit, negative shift) | small visual errors | each has a counter or test (chapter 14.6); fix when observed |
| R12 | Haiku implementers stall on M7/M10 | schedule | escalate to Opus early (project rule); split milestones further if needed |
| R13 | Z32 precision loss above 2^24 | rare z-fighting | counter `z32_high_values`; revisit only if seen |
| R14 | VRAM use at 6x–8x with many targets | allocation failures | budget eviction (04.10), device-derived ceiling (06.1) |

## 15.5 Open questions for the owner

| # | Question |
|---|---|
| Q-01 | Can you record GS captures (`GT4_PGS_CAPTURE`) of scenes S-03 to S-11 (120–300 frames each, 480i and 480p where relevant)? When? |
| Q-02 | After M19, should HWR be offered as an option next to paraLLEl-GS, or become the default PC renderer? Which default scale for the release (suggestion: 3x)? |
| Q-03 | Do you accept the GLM design: rare exotic reinterpretations are exact but rendered at 1x detail inside upscaled frames (instead of stalling the CPU)? |
| Q-04 | Should "native post-effects" (bloom/blur at native resolution) be on by default for GT4 after we compare at 3x? |
| Q-05 | Dither pattern at high resolution: `native` (PS2-like, default proposal) or `fine` (PCSX2's default)? |
| Q-06 | Blend accuracy default `high` (exact, GT4's recommendation) even if it costs some frame time at 4x+? |
| Q-07 | Can you run PCSX2 (your own install) with texture dumping on a GT4 save to give us reference file names for Format P tests? Is there a specific community pack you want supported? |
| Q-08 | If a GT4 scene dithers onto 32-bit buffers (counter), do you have a real-PS2 photo or capture to compare? |
| Q-09 | For mipmap rounding, can you take matching PCSX2 screenshots of a mip-heavy track scene at 1x? |
| Q-10 | Only if the counter shows GT4 uses per-pixel display merge alpha: compare one frame with PCSX2. |
| Q-11 | Where should texture packs live by default: next to the game data, or in the user data folder chosen by the launcher? |
| Q-12 | Is phase 1 on the emulation thread acceptable, or do you want the GS-thread design (GS-ASYNC-DESIGN) earlier than M17? |
| Q-13 | After M15: which deinterlacer should be the default for menus (`adaptive` proposed)? |

## Sources

- All chapters 00 to 14 of this specification.
- Research report `gt4-gamedb-hacks-donor.md` sections 3 to 5 (requirements,
  comparison procedure, measured scenes).
- Research report `output-postfx-vulkan-perf.md` B.13 (invariants and tests).
- Research report `upscaling-clut-readbacks.md` acceptance checklist.
- Project rules: `CLAUDE.md`, `TASKS.md` (task lifecycle, delegation).
