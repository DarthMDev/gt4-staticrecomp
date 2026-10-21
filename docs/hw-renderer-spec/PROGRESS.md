# CoreGS 2.0 (HWR, TASK-027) progress log

Running log of the TASK-027 implementation on `task/027-coregs2`. The implementer updates it with every pushed
step; the same state goes to the ai-memory page `decisions/coregs2-implementation`. Clean room: written from
`docs/hw-renderer-spec` only. Brief: [AGENT-BRIEF-COREGS2.md](../../AGENT-BRIEF-COREGS2.md).

## Milestones

| M | State | Evidence |
|---|---|---|
| M0 scaffolding | Done; game hookup moved to the GS stream seam on 2026-10-21 | CTest `hw_options`, `hw_present_rect`, `hw_smoke_*`, `hw_stream_sink` |
| M1 tools | Python done; native replay reads the old capture format only | `test_hw_capture`, `test_hw_replay_compare`, `hw_cpuref_replay` |
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

- `hw_gpu_pixel_directed`: 8 TEXA cases; GPU shader lacks the IN-07 AEM rule the CPU reference took in 2434e1d.
- `python_native_contracts` / `test_make_distribution`: refuses the unpinned development executable (expected).

## Next step

Bring IN-07 into `hw_pixel_core.glsl` (warm-up), then native `.gscap` v2 replay for the CPU reference (brief section 5, step 1).
