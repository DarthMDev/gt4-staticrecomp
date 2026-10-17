# Native renderer: plan (branch `native-renderer/main`)

Status: **planned, not started** (owner, 2026-10-17). Work starts after release 1.0.0.
Background study: [STUDY.md](STUDY.md) (feasibility, render architecture, difficulty map). Decision:
[0071](../decisions/0071-native-renderer-branch.md).

## Goal

GT4Recomp keeps the GS path as the reference. On top of it, an optional **native renderer** (our own MIT code)
draws GT4's 3D world itself. It takes the game's draw calls (`pgluCallShape`, the PGL library) and turns them into
modern GPU draws instead of emulating the GS. It is designed from the first line for:

- **High frame rates** above 60 FPS while the game's fixed 1/60 s physics stays untouched (per-draw
  interpolation between simulation ticks, one tick of latency).
- **True widescreen and ultrawide** (21:9, 32:9). The field of view gets wider rather than stretched,
  culling is widened to match, and HUD elements are anchored with the game's `wideOffset` flags.
- **Any internal resolution.**
- **Android and iOS** later: a portable renderer core with Vulkan, Metal and GLES backends.

## Hybrid model

- Menus, 2D, HUD (`render_panel`), `render_other` and movies stay on the GS.
- The 3D world pass (`render_world`: sky, course, wet reflection, cars, mirror, flares, sparks, smoke) moves to the
  native renderer piece by piece.
- Every piece runs in **shadow mode** first: drawn but not shown, and compared against the GS image. It goes on
  screen only after its image-diff gate and the owner's in-game approval.
- The game stays fully playable on the GS path at every step. The native path is a switch, off by default until
  Phase 4.

## Branch workflow

- `native-renderer/main` is the long-lived integration branch for this work. It is created from `main` and
  merges `main` in regularly to stay current.
- Each task gets its own branch from `native-renderer/main`: `native-renderer/NNN-short-name`. The usual
  lifecycle applies: single writer → CTest/Python/headless checks → owner test → explicit owner approval → PR
  into `native-renderer/main`.
- Generic, renderer-neutral pieces (hook registry, scene capture, GS layer markers) may be proposed to `main`
  separately when they are useful there. The renderer itself stays on this branch until the owner decides
  otherwise.

## Rules

- MIT only, written by us. No GPL code. Do not read celosia's source code (GPL-3.0); use only the prose summary
  in the study.
- Agents on these tasks must be **untainted**: they never open PCSX2 sources, the `gt4recomp-gs` fork,
  `private/tooling/pcsx2-*` or `private/slice189-pcsx2-ref`. They may use the GS path's final frame images as a
  black-box reference.
- No JIT. GPU driver shader compilation is allowed.
- Captures (`.gt4scene`), decoded assets and game data stay under `private/` and are never committed.

## Tasks

The numbers are reserved for this branch. Effort estimates come from the study and are hypotheses.

| Task | Phase | Goal | Effort | Acceptance (summary) |
|---|---|---|---|---|
| TASK-065 | P0 | Hook layer (replace / observe-after-return by guest address, missing-entry audit), scene capture with frame markers, `.gt4scene` format, headless replay, renderer core with Vulkan backend, side-by-side viewer | 10-14 weeks | Observe-only hooks change nothing (42/42 headless frame dumps identical); capture overhead < 1 ms/frame; CTest fixtures for hooks, capture parser, PGL state decoder |
| TASK-066 | P1 | Asset decode (VIF shapes, GS textures and CLUTs, materials) with caches; offline 1x redraw of captured frames | 8-12 weeks (overlaps P0) | Captured showroom and race frames: PSNR ≥ 30 dB, pct_bad ≤ 8% on the geometry mask; every draw decoded or counted as unsupported |
| TASK-067 | P2 | Cars live in shadow mode at any internal resolution, including the per-frame environment map | 8-12 weeks | Car-mask PSNR ≥ 32 dB at 1x on the reference scene; no missing parts; 100% game speed |
| TASK-068 | P3 | Track, sky, fog and billboards in shadow mode | 6-10 weeks | World geometry PSNR ≥ 30 dB at 1x; ≥ 99% of H1 draw calls covered in the reference race |
| TASK-069 | P4 | World-pass effects (wet reflection, mirror, shadows, particles, smoke, flares with readback emulation, post effects), then native ownership of `render_world` on screen with the hybrid compositor | 16-26 weeks | Per-effect diffs within thresholds (night, wet, rally); owner approval; 60 FPS at 100% speed at 4x; flare readback bytes match the GS path or a documented tolerance |
| TASK-070 | P5 | High frame rate: per-draw interpolation between ticks, present scheduler for 120/144 Hz | 4-8 weeks | 120/144 Hz at 100% speed; matched-draw rate ≥ 95% in races; p99 frame time under the present interval; headless guest dumps unchanged (no physics change) |
| TASK-071 | P6 | Ultrawide and any resolution: game projection fed the wide aspect, widened culling, HUD anchoring | 3-6 weeks | 21:9 and 32:9 without edge pop-in on the reference lap (owner); HUD anchored; 4:3/16:9 unchanged |
| TASK-072 | P7 | Other modes: replays (replay camera `t`), photo mode, showroom, split screen, menu 3D cars | 8-16 weeks | Each mode through shadow mode, then owner approval |
| TASK-073 | P8 | Mobile backends (Metal, Vulkan-mobile, GLES) and memory limits | 10-20 weeks | Same scenes at 60 FPS on the target devices (defined in Phase 2/3 of the project) |

**Total:** about 75-130 weeks on PC plus 10-20 for mobile. The first owner-visible result (TASK-067, cars side by
side with the GS) comes about 4-6 months in.

## Open questions to settle early (read-only, before TASK-065)

The study (section 9) leaves these open:
- the live race camera's aspect source;
- the internals of `render_cars`;
- the readback function behind the night-race flare;
- tire marks;
- the exact role of each VU1 program.

Several gt4-decomp names were found to be wrong (for example `RaceEntryCar__update` at 0x4A5348 is really
`pglMatrixMode`); report them upstream.
