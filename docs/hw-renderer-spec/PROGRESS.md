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
| M6 output | Display library done; IN-315, IN-316 open | CTest `display_*` |
| M7 cache | Bookkeeping core done | cache CTest |
| M8, M12, M14, M15, M17, M19 | Not started | |
| M9-M11, M13, M16, M18 | Pure references only (decide, lod, replace) | decide, lod, `hw_replace_*` CTest |

## Log

- 2026-10-21: branch `task/027-coregs2` from `main` (db99f9e) merging `task/027-hw-renderer` (80c87a2) and the
  cpuref docs checkpoint (03bdd0d). paraLLEl-GS and its pins dropped (decision 0070). `GT4_RENDERER=hw` now rides
  `main`'s GS stream seam through `native/gs_stream/gs_stream_sink.hpp`. Check build slice216 (see
  `docs/BUILD.md`). Handed to the external implementation agent.

## Open questions

- IN-315 (field phase, two-row offset), IN-316 (frame 900 texture), IN-322 (local-to-local DIR mirroring): see
  `IMPLEMENTATION-NOTES.md`.

## Known failures at handover (slice216)

- `hw_gpu_pixel_directed`: resolved on 2026-10-21 in slice217; the 07.5 / IN-07 A-bit priority rule now matches the CPU reference (all three GPU exactness fixtures pass).
- `python_native_contracts` / `test_make_distribution`: refuses the unpinned development executable (expected).

## Next step

Implement the chapter 12.9 VRAM diagnostic output for the CPU replay before qualifying IN-315/IN-316. Use capture VSYNC N for donor dump N+1 (IN-325); retain composed output separately. Fix the separate existing A+D XYZ kick gap with its own fixture (IN-326). Then investigate the garage texture in the coherent SLM and return to brief step 3 (M4 GPU write-back); do not merge that WIP before these checks. Captures outside the headless S-01/S-02 schedule still require owner gameplay. No windowed game run was made.

## 2026-10-21 replay checkpoint (slice219)

- Confirmed: IN-07 shader warm-up committed; slice217 full native CTest: 177 passed, 1 skipped (178 selected; Python run separately). Python: 627 tests, 44 skipped, one pre-existing distribution-pin error. Default donor headless: 42/42 = slice214, copied card unchanged.
- Confirmed: slice219 standalone CPU reference and composed libraries: 69/69 tests pass, including four new v2 fixtures. Real legacy, envelope-converted v2 and newly recorded donor v2 each replay all 1,101 frames to identical PPMs, with no rejected GIF, skipped texture or FIFO fault.
- Outputs are private/ and build/ **inside this worktree**, honoring the owner's no-writes-to-main instruction. Main assets and old slices remain read-only; no launcher mode/pin changes. Evidence and hashes: BUILD slice219 and the journal.
- Diagnostic only: after aligning capture N to donor N+1, 10/11 sampled menu frames have pct_bad <= 0.038%; donor frame900 still has 29.5487%, PSNR 8.7427 dB. These compare composed CPU output to donor VRAM output, so are not chapter14 qualification (IN-325). IN-315/IN-316 remain open; no field-offset rule was added.
