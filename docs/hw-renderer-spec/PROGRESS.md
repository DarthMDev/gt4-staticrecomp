# CoreGS 2.0 (HWR, TASK-027) progress log

Running log of the TASK-027 implementation on `task/027-coregs2`. The implementer updates it with every pushed
step; the same state goes to the ai-memory page `decisions/coregs2-implementation`. Clean room: written from
`docs/hw-renderer-spec` only. Brief: [AGENT-BRIEF-COREGS2.md](../../AGENT-BRIEF-COREGS2.md).

## Milestones

| M | State | Evidence |
|---|---|---|
| M0 scaffolding | Done; game hookup moved to the GS stream seam on 2026-10-21 | CTest `hw_options`, `hw_present_rect`, `hw_smoke_*`, `hw_stream_sink` |
| M1 tools | Native replay supports legacy and GSSTREAM v2; real menu parity confirmed | slice219: 69/69 CPU-library tests; 1,101/1,101 identical PPMs in legacy, converted v2 and fresh donor v2 replays |
| M2 memory | Done | CTest `memory_*` |
| M3 front end, CLUT | Done; A+D vertex kicks fixed (IN-326) | CTest `m3_*`, `m3_hw_gif_ad_xyz` |
| M4 reference I | CPU reference done; the GPU write-back path is ported from `task/027-m4-gpuref`, built, wired into the backend and green in 11 fixtures (IN-329). The S-01/S-02 parity replay is still missing (no HWR replay tool) | CTest `cpuref_*`, `hw_flat_provoking`, `hw_sprite_corners`, `hw_gpu_coverage_vectors`, `hw_gpu_equivalence_*` |
| M5 reference II | Arithmetic done on CPU and GPU (bit-exact) and wired into the raster path: blend unit (81 modes), COLCLAMP, PABE, alpha test (8 methods x 4 AFAIL), DATE, FBA, FBMASK, DIMX dither | CTest `pixel_*`, `hw_gpu_*`, `hw_gpu_raster_*` |
| M6 output | Display library done; CPU VRAM diagnostic added; IN-315 comparison resolved and IN-316 closed (garage samples pass after IN-326); game dumps still pending | slice222:76/76 CPU-library tests; 11/11 aligned menu samples inside the 2D gate, worst 0.2051% / 45.992 dB |
| M7 cache | Bookkeeping core done | cache CTest |
| M8, M12, M14, M15, M17, M19 | Not started | |
| M9-M11, M13, M16, M18 | Pure references only (decide, lod, replace) | decide, lod, `hw_replace_*` CTest |

## Log

- 2026-10-21: branch `task/027-coregs2` from `main` (db99f9e) merging `task/027-hw-renderer` (80c87a2) and the
  cpuref docs checkpoint (03bdd0d). paraLLEl-GS and its pins dropped (decision 0070). `GT4_RENDERER=hw` now rides
  `main`'s GS stream seam through `native/gs_stream/gs_stream_sink.hpp`. Check build slice216 (see
  `docs/BUILD.md`). Handed to the external implementation agent.

## Open questions

- IN-322 (local-to-local DIR mirroring) and IN-320 (composed scanout phase) are open; IN-321 is a negative-result
  record for the garage defect that IN-326 turned out to explain. IN-315 was a frame-pairing error for the VRAM
  comparison; actual scanout phase still needs M6 qualification. IN-316 and IN-326 are resolved (slice222). See
  `IMPLEMENTATION-NOTES.md`.

## Known failures at handover (slice216)

- `hw_gpu_pixel_directed`: resolved on 2026-10-21 in slice217; the 07.5 / IN-07 A-bit priority rule now matches the CPU reference (all three GPU exactness fixtures pass).
- `python_native_contracts` / `test_make_distribution`: refuses the unpinned development executable (expected).

## Next step

**Parity replay and first playable**: add the HWR replay tool (the capture reader over the GPU write-back
renderer) and record S-01/S-02 against the donor frames, as 15.2 M4/M5 ask; then M6 (HWR frame dumps from the
sink and the first owner look at 1x). Then M5 (blending in the raster path: ABE is currently
drawn opaque, IN-500) and M6 (HWR frame dumps from the sink, first owner look at 1x). The front-end equivalent of the old `.inc` copy is not needed: the sink no longer uses donor memory for output. S-03..S-11 still need owner gameplay captures.

## 2026-10-21 replay checkpoint (slice219)

- Confirmed: IN-07 shader warm-up committed; slice217 full native CTest: 177 passed, 1 skipped (178 selected; Python run separately). Python: 627 tests, 44 skipped, one pre-existing distribution-pin error. Default donor headless: 42/42 = slice214, copied card unchanged.
- Confirmed: slice219 standalone CPU reference and composed libraries: 69/69 tests pass, including four new v2 fixtures. Real legacy, envelope-converted v2 and newly recorded donor v2 each replay all 1,101 frames to identical PPMs, with no rejected GIF, skipped texture or FIFO fault.
- Outputs are private/ and build/ **inside this worktree**, honoring the owner's no-writes-to-main instruction. Main assets and old slices remain read-only; no launcher mode/pin changes. Evidence and hashes: BUILD slice219 and the journal.
- Diagnostic only: after aligning capture N to donor N+1, 10/11 sampled menu frames have pct_bad <= 0.038%; donor frame900 still has 29.5487%, PSNR 8.7427 dB. These compare composed CPU output to donor VRAM output, so are not chapter14 qualification (IN-325). IN-315/IN-316 remain open; no field-offset rule was added.

## 2026-10-21 VRAM diagnostic checkpoint (local slice221)

- Confirmed:75/75 CPU-library CTests and2 CLI process tests pass. Python629 tests with44 skips and only the
  pre-existing unpublished-game-executable distribution-pin error.
- CLI defaults to `vram`; `--dump-source scanout` preserves the composed output. `frames.jsonl` records every
  capture-native ID and the selected raw registers, geometry, validity and line-doubling rule.
- Real capture: two raw replays produce1,096 identical PPMs; capture frames0..4 have no enabled circuit. All
  1,101 explicit scanout PPMs remain identical to slice219. No frame IDs or pixels were shifted by the tool.
- Aligned VRAM comparison:10/11 sampled donor frames100..1100 meet pct_bad<=0.5% and PSNR>=40dB; garage900
  remains29.5487%,8.7427dB. Report-only at this milestone, not full scene qualification.
- IN-315 historical offset is explained by wrong frame pairing for these samples (vertical-pairing.json),
  not by a missing VRAM field offset. IN-316 is now a confirmed remaining rendering/memory discrepancy.

## 2026-10-21 A+D XYZ kicks checkpoint (slice222, IN-326 closes IN-316)

- Confirmed: `m3_hw_gif_ad_xyz` (new) and 75 existing CPU/memory/front-end/pixel/display tests pass, 76/76, in a
  fresh standalone `build/slice222-cpuref`. PACKED descriptor 0xE now kicks XYZF2/XYZ2/XYZF3/XYZ3 in the
  canonical register layout (01.1, 02.1, 02.3); bits 72..127, bit 111 ADC included, are ignored for A+D.
- Confirmed cause of IN-316: the missing A+D kicks were the garage defect. Replaying the same donor v2 menu
  capture (`menu-v2.gscap`, SHA-7abb51cf...) through the fixed reference changes exactly frames 811..916 and
  nothing else: 990/1,096 VRAM images and 995/1,101 scanout PPMs are byte-identical to slice221.
- Confirmed: aligned donor sample comparison (capture N = donor N+1) now has 11/11 frames inside the chapter
  14.4 2D thresholds; the garage sample donor900/capture899 went from pct_bad 29.5487% / PSNR 8.7427 dB to
  0.2051% / 45.992 dB. Report only: scene qualification remains M8, and the donor 2D gate is gating from M8.
- Confirmed: the game executable is untouched (`gt4vulkan.exe` 97,291,264 bytes, SHA-3fa40ea1..., the slice217
  binary). The front end is linked only into the CPU reference, not into `gt4_hw_stream_sink`, so the default
  renderer path and the recorded 42/42 headless parity are unchanged by construction.
- Slice217 full native CTest after the change: 188 passed, Q-07 skipped, only the expected pre-existing
  `python_native_contracts` distribution-pin failure. Python: 629 tests, 44 skips, same single expected error.
- Evidence: `private/slice222-in326-evidence/` (vram, scanout, aligned-candidate, comparison JSON, logs).

## 2026-10-21 M4 write-back port checkpoint (slice223, IN-328/IN-329)

- Ported `task/027-m4-gpuref` (e31fcc4) onto this branch: `writeback/` (Vulkan-free drawer interface, `gt4_hwd_*` device table, batch translation, chunked execute with SLM read/write-back, Renderer), the uber vertex/fragment shaders and the backend routing. The dropped `.inc` edit has no equivalent need here: the sink owns presentation and never reads donor memory.
- Confirmed: the WIP did not compile; fixes were include/API drift, a mangled struct fragment at writeback.cpp:228 that closed the namespace twice, a `ClutUnit` constructor argument, a test predicate that compared `word >> 16` with 255 instead of the masked blue channel, and the loader's instance set (surface and swapchain entry points). New `load_headless_entry_points` serves windowless devices.
- Confirmed: 11 fixtures green — `hw_flat_provoking`, `hw_sprite_corners`, `hw_gpu_coverage_vectors` (chapter 03.3 vectors, sub-pixel sprite edges), `hw_gpu_equivalence_{flat,texture,region,bilinear,tfx_fog,depth_alpha,scissor}` (0 mismatching pixels over 1089-1681 compared pixels each) and the report-only `hw_gpu_gouraud_report`.
- Confirmed IN-328: the CPU reference truncated a constant fog of 90 to 89 (double barycentric weights that do not sum to 1), which was the 16-pixel TFX/fog difference. Constant attributes now interpolate exactly; new fixture `cpuref_constant_attributes` (528 covered pixels, exact (80,55,58)).
- Confirmed: full native CTest 200 pass + Q-07 skip + the known `python_native_contracts` distribution-pin failure (202 selected). Default renderer headless 4,200 frames: 42/42 dumps equal slice214, card copy unchanged. Python 629 tests, 44 skips, same single expected error.
- Confirmed: `GT4_RENDERER=hw` now runs the write-back renderer in process: 600 headless frames, 205,791,616 GIF bytes, 2,987 privileged writes, 0 faults, 33 s (about 55 ms per guest frame; one draw per primitive with a barrier at 1x). Nothing is dumped yet, so this is a stability result, not a picture.
- Evidence: `private/slice222-in326-evidence/hw-run/` and `.../default-parity/`. Fresh full build: `build/slice223-coregs2`. No pin, launcher mode, PR or merge.

## 2026-10-21 M5 raster path checkpoint (blend unit through the draw stage)

- Confirmed: the raster (fragment-shader) path now runs the whole pixel pipeline of chapter 08. The translation decodes the ALPHA register, PABE and COLCLAMP exactly as the CPU reference does and passes PRIM.ABE; `hw_process_pixel` was already the bit-exact core, so the only missing piece was the state.
- Confirmed: five new fixtures compare GPU against the CPU reference with zero mismatching pixels: `hw_gpu_raster_blend` (81 modes x COLCLAMP both ways, 13,122 pixels), `hw_gpu_raster_pabe` (both alpha MSB cases), `hw_gpu_raster_atest` (8 methods x 4 AFAIL, 4,896 pixels), `hw_gpu_raster_date` (DATM both values over the written result) and `hw_gpu_raster_frame` (FBA, FBMSK on CT24, DIMX dither on CT16).
- Confirmed: full native CTest 205 pass + Q-07 skip + the known distribution-pin failure (207 selected). Default renderer headless 4,200 frames: 42/42 dumps equal slice214, card copy unchanged.
- Confirmed: `GT4_RENDERER=hw` headless 600 frames with blending enabled: 0 faults, 37.0 s (about 62 ms per guest frame; blending costs about 7% over the previous 33.2 s). No frame dump yet.
- Open: DATE in CT24 writes nothing (read as High confidence in 08.7 and mirrored by both sides); the S-01/S-02 parity replay still needs the HWR replay tool.
