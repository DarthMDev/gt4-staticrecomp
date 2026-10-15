# GT4Recomp Tasks

Single ordered task list. Each task has its own branch, automated checks, an
in-game test by the owner and an explicit owner approval before its PR is merged
into `main` (workflow rules in [CLAUDE.md](CLAUDE.md)). Evidence for finished
tasks lives in `docs/journal/` and `docs/inputs/`; background studies are linked
from [STATUS](docs/STATUS.md).

States: ✅ Done · 🧪 Waiting for owner test · 🔄 In progress · ⏭️ Next · 📋 Planned · ⛔ Superseded

Task IDs are stable; the tables list tasks in execution order.

## Phases

| Phase | Platform | Goal |
|---|---|---|
| **Phase 1** (current) | Windows PC | Original game running natively: correct speed, smooth 60 FPS, no interpreter on the normal path, paraLLEl-GS renderer |
| **Phase 2** | Android | ARM64 build with the ARMSX2 mobile GS pipeline (Vulkan) |
| **Phase 3** | iOS | ARM64 build with the ARMSX2 Metal GS pipeline |

**Renderer decision (2026-10-15):** PC uses **paraLLEl-GS** (Vulkan compute,
LGPL-3.0+) as the renderer. The current donor compute GS renderer stays only as
an experimental fallback and comparison reference; it gets no new features.
Mobile uses the **ARMSX2 GS pipeline** (PCSX2 hardware renderer adapted for
Android Vulkan and iOS Metal, with GPU driver profiles and a tile-friendly pass
scheduler). See [GS renderer options](docs/GS-RENDERER-OPTIONS.md).

## Phase 1 — Windows PC

| Task | Goal | State |
|---|---|---|
| TASK‑001 | Rebuild the slice 149 baseline on this machine | ✅ Done |
| TASK‑002 | Owner-launched VU1 program recording mode | ✅ Done |
| TASK‑003 | Hardened VU1 AOT emitter with differential tests | ✅ Done |
| TASK‑004 | VU1 microcode compiled ahead of time in the game | ✅ Done |
| TASK‑005 | Asynchronous GPU submission + overlay (RTSS) fix | ✅ Done |
| TASK‑006 | Frame pacing on a single clock, rate-controlled audio | ✅ Done |
| TASK‑007 | VU1 per-program flag elision | ✅ Done |
| TASK‑008 | Full static VU1 entry coverage (all 2,048 entries) | 🔄 In progress |
| TASK‑030 | Repository cleanup: pre-donor material moved to a private backup | 🧪 Waiting for owner review |
| TASK‑009 | Quick fixes: title, fixed 16:9, fullscreen, interval counters, 480p everywhere | 🔄 480p rework |
| TASK‑018 | Faster builds (parallel jobs, compiler cache, lld-link) | ⏭️ Next |
| TASK‑010 | MTVU: VU1 on its own thread, on by default | 📋 Planned |
| TASK‑031 | paraLLEl-GS integration as the PC renderer | 📋 Planned |
| TASK‑032 | paraLLEl-GS options and GT4 validation: upscaling, deinterlacing, readbacks | 📋 Planned |
| TASK‑033 | Renderer extras on paraLLEl-GS: post-processing, dithering/texture options | 📋 Planned |
| TASK‑019 | Build tuning (fp-contract off, x86-64-v3 variant, LTO, PGO) | 📋 Planned |
| TASK‑013 | License-test crash | 📋 Planned |
| TASK‑014 | VU0 microcode compiled ahead of time | 📋 Planned |
| TASK‑015 | EE entry coverage from the decomp function list | 📋 Planned |
| TASK‑016 | LWR sign-extension fidelity fix | 📋 Planned |
| TASK‑017 | IOP per-module cost counters and review | 📋 Planned |
| TASK‑020 | In-game 16:9 option by default (guest hook) | 📋 Planned |
| TASK‑022 | Decomp names in generated EE code | 📋 Planned |
| TASK‑023 | Launcher (Rust + egui/GPUI) | 📋 Planned |
| TASK‑026 | Racing wheels with force feedback | 📋 Planned |
| TASK‑011 | Upscaling on the donor renderer | ⛔ Superseded by TASK‑032 |
| TASK‑012 | Night-race readback wait on the donor renderer | ⛔ Superseded by TASK‑032 |
| TASK‑029 | Donor renderer features | ⛔ Superseded by TASK‑033 |
| TASK‑021 | Per-interval counters | ⛔ Merged into TASK‑009 |

## Phase 2 — Android

| Task | Goal | State |
|---|---|---|
| TASK‑024 | Mobile GS: ARMSX2 GS pipeline (Vulkan) behind our GS front end | 📋 Planned |
| TASK‑027 | Android app: ARM64 build, host layers, touch controls, Bluetooth controllers | 📋 Planned |

## Phase 3 — iOS

| Task | Goal | State |
|---|---|---|
| TASK‑025 | ARMSX2 Metal GS backend | 📋 Planned |
| TASK‑028 | iOS app: ARM64 build, signing, touch and controller input | 📋 Planned |

## Done

### TASK-001 — Rebuild the slice 149 baseline on this machine
Rebuilt the measured diagnostic candidate on the Ryzen 7 5700X / RX 9060 XT
machine; fixed a fixture that assumed every GPU limits workgroup counts below
`UINT32_MAX`. Commit `8b65693`.

### TASK-002 — VU1 program recording mode
`--mode vu1-record` captured 86 VU1 programs in two owner sessions. Commit `8702d90`.

### TASK-003 — Hardened VU1 AOT emitter
`tools/vu_recomp`: exact inputs, fresh outputs, identity digests, budget/resume
contract, differential tests. Commit `96c71f4`.

### TASK-004 — VU1 AOT in the game (slice 151)
Race frame 33.3 ms → 22.0 ms; 140,034 in-game verifications, 0 mismatches.
Commit `2d21abb`.

### TASK-005 — Asynchronous GPU submission (slice 152)
GS wait 9.5 ms → 0.8 ms; race at 100% speed; RTSS overlay visible on AMD.
Commit `56819b0`.

### TASK-006 — Frame pacing (slice 153)
Flat 16.7 ms frametime (99.6% of intervals in 16-17 ms). Commit `fe51478`.

### TASK-007 — VU1 flag elision (slice 154)
Showroom 44.8 → 59.1 FPS; 5-AI race at 60.0. Commit `26e211d`, merged in PR #1.

## Phase 1 details

### TASK-008 — Full static VU1 entry coverage
Branch `feat/vu1-entry-coverage`. Slice 155 (426 targeted entries) owner test:
night race 0 interpreted calls, rally still 354,837. Owner chose all 2,048
entries of the decrypted image (about 40 MB larger executable is fine); flag
elision applies per program automatically. Acceptance: interpreted calls 0 in
night race, rally and showroom.

### TASK-030 — Repository cleanup (pre-donor material)
Branch `task/030-legacy-cleanup`. Full history and all branches mirrored to the
private repository `moesuito/gt4-staticrecomp-legacy`. Owner choice: clean files
and branches only, no public history rewrite. `docs/LEGACY.md` removed and
archive references trimmed; historical reports, journals, decisions and pins
kept as evidence. After approval and merge: delete the legacy and merged
branches on GitHub (all are in the backup).

### TASK-009 — Quick fixes
Branch `task/009-quick-fixes`, slice 156 `--mode quickfix`. Owner accepted:
title "GT4Recomp", fixed 16:9 (F6/4:3 removed), Alt+Enter/F11 borderless
fullscreen, interval and session counters. Rework:
- 480p everywhere: the `SetGsCrt` rewrite alone does not work, because the game
  programs SMODE2/DISPLAY from its internal mode (default row id 1 of table
  `D_00618648`) and resets the option to `normal` at boot. New approach: patch
  the default row in guest RAM at load (id 1 → interlace 0 / mode 0x50), with
  original-byte verification, so boot, menus and races run progressive.
- Fix the `late_frames` definition (it counts almost every paced frame).

### TASK-018 — Faster builds
Parallel jobs inside one build (split large generated units), compiler cache,
lld-link. Measure before/after; speeds up every later task.

### TASK-010 — MTVU
Run VU1 programs on a worker thread (donor `start_vu_thread` exists), on by
default with an env switch to disable; exact sync points wherever the EE observes
VU1 results. Acceptance: identical headless frames, verify 0 mismatches, owner
showroom/night/race frametime equal or better.

### TASK-031 — paraLLEl-GS integration
Import paraLLEl-GS (github.com/Arntzen-Software/parallel-gs, LGPL-3.0+, with
Granite MIT) pinned by commit; build it in our CMake; bridge its Vulkan device to
ours (Granite device shim or port); adapter from our GS front end (`write`,
`write_packed`, `write_image`, `read_image`, privileged registers, signals,
vsync) to its interface (`gif_transfer`, `write_register`, `map_vram_*`,
`read_transfer_fifo`, `vsync` → scanout). Renderer selected by env (later the
launcher); the donor renderer stays as experimental fallback. Acceptance: GT4
boots, menus and races render correctly; image comparison against the donor
renderer at 1x; owner check of race, night, rally, showroom, menus.

### TASK-032 — paraLLEl-GS options and GT4 validation
Super-sampling upscaling (2x-16x) by env; deinterlacing and field options for
the interlaced menus; page-tracked readbacks against the night-race wait; GT4
checks (channel-shuffle brightness effects, lens flares, half-pixel issues);
performance per scene. Supersedes TASK-011 and TASK-012.

### TASK-033 — Renderer extras on paraLLEl-GS
Post-processing (sharpening, FXAA, filters) and dithering/texture options where
paraLLEl-GS allows them; texture dump/replacement feasibility. Supersedes
TASK-029.

### TASK-019 — Build tuning
`-ffp-contract=off` control, x86-64-v3 runtime variant chosen by the launcher,
dispatch divide removal, ThinLTO, PGO; one variable per candidate.

### TASK-013 — License-test crash
Pre-existing; traced to `RaceSolitaire::loadDemoInputs` (0x3EAD18) →
`AutomobileControlRecord::Manager::load` (0x346BB8) reading a bad record count.
Env-gated logging there and in the PS2ZIP loader for `replay/license/` files,
plus a CDVD read trace; one owner reproduction; fix the cause.

### TASK-014 — VU0 microcode AOT
All 14 `VCALLMS` entries are known statically (docs/VU0-MICROCODE-MAP.md).
Extend `tools/vu_recomp` to unit 0. Acceptance: VU0 interpreted calls 0.

### TASK-015 — EE entry coverage
Add translator entries for decomp functions without one, starting with the 15
referenced ones (docs/DECOMP-INTEGRATION.md).

### TASK-016 — LWR fidelity
Lone LWR with offset != 0 must sign-extend to 64 bits; fix via overlay and scan
generated units for lone LWR uses.

### TASK-017 — IOP per-module counters
Per-module instruction counters and per-library HLE call counts in F8; decide
whether the sound modules need native HLE (docs/IOP-MAP.md).

### TASK-020 — In-game 16:9 option by default
The wide flag is set through `MOption get_wide_mode` (0x180108) →
`func_00437110` (flag at object +0x14); forcing it needs a guest-side write after
the save loads.

### TASK-022 — Decomp names in generated code
Label comments in generated units with identical object code.

### TASK-023 — Launcher
Rust + egui or GPUI: game image, renderer (paraLLEl-GS / donor fallback),
upscaling, video mode, controls; CPU feature detection for build variants.

### TASK-026 — Racing wheels
High-level emulation at the PDIUSB boundary with SDL3 wheels and force feedback
(docs/MENUS-AND-WHEEL.md).

### Superseded
TASK-011 (donor upscaling) and TASK-012 (donor readback wait) → TASK-032;
TASK-029 (donor renderer features) → TASK-033; TASK-021 → TASK-009.

## Phase 2 — Android details

### TASK-024 — Mobile GS (ARMSX2 pipeline, Vulkan)
Bring the ARMSX2 GS pipeline (PCSX2 hardware renderer with ARMSX2's Android
changes: GPU driver profiles, tile-friendly pass scheduler, framebuffer-fetch
policy) behind our GS front end. ARMSX2's ARM64 JIT is not used (no JIT rule).
Owner's ARMSX2 checkout: `C:/Antigravity/armsx2` (upstream `ARMSX2/ARMSX2`).

### TASK-027 — Android app
ARM64 build of the AOT EE/VU code and runtime (fp-contract off), Android host
layers (window, audio, input, storage, lifecycle), on-screen controls that hide
when a Bluetooth controller connects; target Galaxy S23 Ultra.

## Phase 3 — iOS details

### TASK-025 — Metal GS (ARMSX2)
ARMSX2's Metal backend for the same GS pipeline.

### TASK-028 — iOS app
ARM64 build, iOS host layers, signing/installation on iPhone 15 Pro Max and
17 Pro Max (built on a Mac with Xcode).

## Ideas (not scheduled)

LAN-over-internet and an online lobby launcher (docs/ONLINE-NOTES.md), in-game
settings menus, Spec II as a second target or data source (docs/SPEC2-STUDY.md).

A decompilation-based source port (native renderer, physics decoupled from frame
rate, ultrawide, remaster) is out of scope for this repository and would be a
separate project, possibly a fork.
