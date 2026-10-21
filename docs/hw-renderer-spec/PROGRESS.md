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
| M3 front end, CLUT | Done | CTest `m3_*` |
| M4 reference I | CPU reference done; GPU write-back WIP on `task/027-m4-gpuref` (e31fcc4, not built) | CTest `cpuref_*`; menu capture replay |
| M5 reference II | Arithmetic done on CPU and GPU (bit-exact); not wired | CTest `pixel_*`, `hw_gpu_*` |
| M6 output | Display library done; CPU VRAM diagnostic added; IN-315 comparison resolved, IN-316 open; game dumps still pending | slice221:75/75 CPU-library tests and calibrated VRAM replay |
| M7 cache | Bookkeeping core done | cache CTest |
| M8, M12, M14, M15, M17, M19 | Not started | |
| M9-M11, M13, M16, M18 | Pure references only (decide, lod, replace) | decide, lod, `hw_replace_*` CTest |

## Log

- 2026-10-21: branch `task/027-coregs2` from `main` (db99f9e) merging `task/027-hw-renderer` (80c87a2) and the
  cpuref docs checkpoint (03bdd0d). paraLLEl-GS and its pins dropped (decision 0070). `GT4_RENDERER=hw` now rides
  `main`'s GS stream seam through `native/gs_stream/gs_stream_sink.hpp`. Check build slice216 (see
  `docs/BUILD.md`). Handed to the external implementation agent.

## Open questions

- IN-316 (frame900 texture), IN-322 (local-to-local DIR mirroring), IN-326 (A+D XYZ kicks). IN-315 was a frame-pairing error for the VRAM comparison; actual scanout phase still needs M6 qualification. See
  `IMPLEMENTATION-NOTES.md`.

## Known failures at handover (slice216)

- `hw_gpu_pixel_directed`: resolved on 2026-10-21 in slice217; the 07.5 / IN-07 A-bit priority rule now matches the CPU reference (all three GPU exactness fixtures pass).
- `python_native_contracts` / `test_make_distribution`: refuses the unpinned development executable (expected).

## Next step

Fix IN-326 (A+D XYZ kicks) in a separate candidate with equivalent-path fixtures, then replay the same
menu capture to measure its effect on IN-316. The VRAM diagnostic and frame alignment are now validated.
Investigate the remaining garage texture in coherent SLM before M4 GPU write-back. No arbitrary field
shift is justified; composed/deinterlaced scanout phase remains an M6/M15 check.

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
