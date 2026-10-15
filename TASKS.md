# GT4Recomp Tasks

Single ordered task list. Each task has its own branch, automated checks, an
in-game test by the owner and an explicit owner approval before its PR is merged
into `main` (workflow rules in [CLAUDE.md](CLAUDE.md)). Evidence for finished
tasks lives in `docs/journal/` and `docs/inputs/`; background studies are linked
from [STATUS](docs/STATUS.md).

States: ✅ Done · 🔄 In progress · ⏭️ Next · 📋 Planned · 🔭 Long term

Task IDs are stable; the overview lists tasks in execution order.

## Overview

| Task | Goal | State |
|---|---|---|
| TASK-001 | Rebuild the slice 149 baseline on this machine | ✅ Done |
| TASK-002 | Owner-launched VU1 program recording mode | ✅ Done |
| TASK-003 | Hardened VU1 AOT emitter with differential tests | ✅ Done |
| TASK-004 | VU1 microcode compiled ahead of time in the game | ✅ Done |
| TASK-005 | Asynchronous GPU submission + overlay (RTSS) fix | ✅ Done |
| TASK-006 | Frame pacing on a single clock, rate-controlled audio | ✅ Done |
| TASK-007 | VU1 per-program flag elision | ✅ Done |
| TASK-008 | Full static VU1 entry coverage | 🔄 In progress |
| TASK-009 | Quick fixes: window title, fixed 16:9, fullscreen toggle, per-interval counters, 480p boot and 16:9 option (if the study shows it is small) | ⏭️ Next |
| TASK-018 | Faster builds (parallel jobs, compiler cache, lld-link) | 📋 Planned |
| TASK-010 | MTVU: VU1 on its own thread, on by default | 📋 Planned |
| TASK-019 | EE/runtime build tuning (fp-contract off, x86-64-v3 variant, LTO, PGO) | 📋 Planned |
| TASK-011 | Upscaling 1x-6x on the Vulkan (GS) renderer | 📋 Planned |
| TASK-012 | Night races: remove the GS readback wait | 📋 Planned |
| TASK-013 | License-test crash | 📋 Planned |
| TASK-014 | VU0 microcode compiled ahead of time | 📋 Planned |
| TASK-015 | EE entry coverage from the decomp function list | 📋 Planned |
| TASK-016 | LWR sign-extension fidelity fix | 📋 Planned |
| TASK-017 | IOP per-module cost counters and review | 📋 Planned |
| TASK-020 | In-game 16:9 option by default (guest hook); 480p via guest hook if the TASK-009 switch fails | 📋 Planned |
| TASK-021 | Merged into TASK-009 | — |
| TASK-022 | Decomp names in generated EE code | 📋 Planned |
| TASK-023 | Launcher (Rust + egui/GPUI): video, controls, game image | 📋 Planned |
| TASK-024 | Vulkan (Custom) renderer P0: GT4 GS feature census and replay harness | 📋 Planned |
| TASK-025 | Vulkan (Custom) renderer P1-P3: PCSX2 HW renderer import, adapter, options | 📋 Planned |
| TASK-026 | Racing wheels (G27/G29 and others) with force feedback | 📋 Planned |
| TASK-027 | Android build | 📋 Planned |
| TASK-028 | iOS build (Metal) | 📋 Planned |
| TASK-029 | Phase 2: decompilation bridge and first native functions | 🔭 Long term |

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

## In progress

### TASK-008 — Full static VU1 entry coverage
Branch: `feat/vu1-entry-coverage`.
- Decrypt the VU1 image from the pinned ELF; compile every plausible entry
  (all 2,046 addresses if size allows, otherwise program starts, branch/jump
  targets, block starts and the recorded entries).
- Acceptance: headless frames identical to TASK-007; verify mode 0 mismatches;
  owner night race, rally and showroom show `interpreted_calls` 0 in F8.

## Planned

### TASK-009 — Quick fixes
- Window title becomes just "GT4Recomp".
- Fixed 16:9: remove F6 and the 4:3 framing option (launcher `--aspect`).
- Fullscreen toggle: Alt+Enter (Windows convention) and F11, borderless
  fullscreen on the current monitor.
- Per-interval perf counters (`late_frames`, `audio_underruns`; was TASK-021).
- 480p by default (experimental switch, default on only after the owner check):
  the game applies the saved option at race start through `SetGsCrt`
  (480p = interlace 0 / mode 0x50, 480i = interlace 1 / mode 0x51; menus use
  mode 0x2 and are untouched). The host rewrites 0x51 → 0x50 in the `SetGsCrt`
  syscall (donor `kernel.cpp:474` / `hw.cpp:165`) when the switch is on. Risk:
  the game still prepares 480i field buffers, so the owner must confirm the race
  image; otherwise this moves back to TASK-020 with a guest-side hook.
  Study: `RaceDisplay` state at object +0x16B0, table `D_00618648`
  (`MOption::applyRaceDisplayMode`).
Owner check: title, fullscreen in menus/race, 16:9 image, F8 counters.

### TASK-010 — MTVU
Run VU1 programs on a worker thread (donor `start_vu_thread` exists), on by
default with an env switch to disable; exact sync points wherever the EE observes
VU1 results. Acceptance: identical headless frames, verify 0 mismatches, owner
showroom/night/race frametime equal or better.

### TASK-011 — Upscaling on the Vulkan (GS) renderer
Internal scale selected by environment variable until the launcher exists, from
1x up to 6x (the donor supports 1-3x; 4-6x needs checking shader assumptions and
memory: GS memory buffers grow with scale², 36x at 6x). Use the donor resolve
pass before CPU readbacks. Acceptance: 1x identical to TASK-010; owner visual
check and frame rate at 2x/3x/4x/6x.

### TASK-012 — Night race GS readback wait
Count GS→host readbacks per frame by scene (lens flare suspected), then copy only
requested regions and batch consecutive reads without draws between them; keep
results exact. Acceptance: owner night race GS wait near zero.

### TASK-013 — License-test crash
Env-gated logging at `RaceSolitaire::loadDemoInputs` (0x3EAD18),
`AutomobileControlRecord::Manager::load` (0x346BB8) and the PS2ZIP loader for
`replay/license/` files, plus a CDVD read trace; one owner reproduction; fix the
cause. Acceptance: owner completes the first license tests.

### TASK-014 — VU0 microcode AOT
All 14 `VCALLMS` entries are known statically (docs/VU0-MICROCODE-MAP.md).
Extend `tools/vu_recomp` to unit 0. Acceptance: VU0 interpreted calls 0.

### TASK-015 — EE entry coverage
Add translator entries for decomp functions without one, starting with the 15
referenced ones (docs/DECOMP-INTEGRATION.md). Acceptance: new library pins,
headless frames identical, owner smoke over several modes.

### TASK-016 — LWR fidelity
Lone LWR with offset != 0 must sign-extend to 64 bits; fix via overlay and scan
generated units for lone LWR uses.

### TASK-017 — IOP per-module counters
Per-module instruction counters and per-library HLE call counts in F8; decide
whether the sound modules need native HLE (docs/IOP-MAP.md).

### TASK-018 — Faster builds (runs right after TASK-009)
Parallel jobs inside one build, a compiler cache, lld-link. Measure before/after;
speeds up every later task.

### TASK-019 — Build tuning (runs after TASK-010)
`-ffp-contract=off` control, x86-64-v3 runtime variant chosen by the launcher,
dispatch divide removal, ThinLTO, PGO; one variable per candidate.

### TASK-020 — In-game 16:9 option by default
The wide flag is set through `MOption get_wide_mode` (0x180108) →
`func_00437110` (flag at object +0x14); forcing it needs a guest-side hook after
the save loads (MOption instance address still to confirm). Also the fallback
for 480p if the TASK-009 `SetGsCrt` switch is not acceptable.

### TASK-021 — Merged into TASK-009

### TASK-022 — Decomp names in generated code
Label comments in generated units with identical object code.

### TASK-023 — Launcher
Rust + egui or GPUI: game image, renderer (Vulkan GS / Vulkan Custom), internal
scale, video mode, controls; CPU feature detection for build variants.

### TASK-024 / TASK-025 — Vulkan (Custom) renderer
PCSX2-based hardware renderer specialized for GT4: census and replay harness
first, then import, adapter, 1x pixel diff against Vulkan (GS), then upscaling,
dithering control, deinterlacing, mipmapping and texture packs
(docs/HW-RENDERER-PLAN.md).

### TASK-026 — Racing wheels
High-level emulation at the PDIUSB boundary with SDL3 wheels and force feedback
(docs/MENUS-AND-WHEEL.md).

### TASK-027 / TASK-028 — Android and iOS
ARM64 builds, host layers, on-screen controls with Bluetooth controller
switching; iOS via MoltenVK first, Metal later (ARMSX2 as case study).

### TASK-029 — Phase 2
Decompilation bridge (`GPtr`, ABI thunks, A/B comparison), then native renderer,
physics decoupled from frame rate, ultrawide.

## Ideas (not scheduled)

LAN-over-internet and an online lobby launcher (docs/ONLINE-NOTES.md), in-game
settings menus, Spec II as a second target or data source (docs/SPEC2-STUDY.md),
DLSS/FSR 2+ after the native renderer.
