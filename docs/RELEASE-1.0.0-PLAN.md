# Release 1.0.0 plan (owner decisions 2026-10-15)

Goal: first stable GitHub Release: NSIS per-user installer with the prebuilt
game executable, the launcher 1.0.0, the **PCSX2 GS** renderer (separate process
`gt4-gs.exe`, downloaded during setup) with **CoreGS 1.1** built in as the
fallback, controller and keyboard input with a mapping menu, save/replay manager.
No racing wheels (TASK-018) and no IOP work (TASK-016 deferred). Unsigned installer.
Renderer decision: [decision 0070](decisions/0070-pcsx2-gs-separate-process.md).

## Renderer names

| Name | What | Launcher entry |
|---|---|---|
| **PCSX2 GS** | The PCSX2 hardware GS (GPL-3.0+) in a separate process, `gt4-gs.exe`, built from our fork `moesuito/gt4recomp-gs` (TASK-032..044). Based on PCSX2 stable v2.8.x; downloaded and pinned by the launcher | Default when present; one-click download during setup |
| **CoreGS 1.1** | Our current Vulkan compute GS renderer (donor lineage, MIT), built in. Offline default and the fallback when `gt4-gs.exe` is missing or fails | Built in, always available |
| **CoreGS 2.0** | Our own MIT hardware renderer, clean room from a behavioral spec of the PCSX2 hardware renderer (TASK-026/027) | Visible, greyed out ("coming soon") |

paraLLEl-GS is removed from the release (decision 0070); branch `task/012-parallel-gs` is archived.

## Workstreams (each its own branch and worktree, one writer each)

| Id | Workstream | Branch | Notes |
|---|---|---|---|
| W1 | MTVU merge (TASK-011) | task/011-mtvu -> main | Owner authorised PR and merge; default on |
| W2 | CoreGS 1.1: upscaler up to 3x, deinterlace (interlaced modes only), shader AA (FXAA-class), FSR 1.0 viewport scaling | task/028-donor-upscale | Paused by the owner (3x shows native-size aliasing on 3D models). Stays the built-in fallback. Deinterlace behaviour taken from our clean-room spec (docs/hw-renderer-spec chapter 13). FSR 1.0 is MIT (AMD) |
| W3 | Input: binding table, XInput auto-map with LT/RT as L2/R2 pressure, keyboard remap, controls config file read by the game | task/029-input-mapping (code merged into task/019-launcher) | Code done; ships with the launcher; owner test pending. docs/launcher/CONTROLS-STUDY.md |
| W4 | TASK-025 save manager: runtime atomic card flush and flush on exit; Rust save crate (card image read/write, folder model, .psu and card-image import, PCSX2 folder import) | task/025-save-manager (code merged into task/019-launcher) | Code done; ships with the launcher; owner test pending. Design in docs/launcher/LAUNCHER-PLAN.md section 13 |
| W5 | TASK-019 launcher 1.0.0 (Rust + Slint) and NSIS installer | task/019-launcher | Paused (Tauri 2 WIP). Integrates W3 and W4 interfaces; renderer dropdown per the table above |
| W6 | TASK-017 license-test crash | task/017-license-crash | Lead: GT4 JP/Asia use EE "chop" rounding to fix a B5 license crash. Waiting for the owner's FSV trace test |
| W7 | CoreGS 2.0 milestones (TASK-027) | task/027-hw-renderer | Paused; not part of 1.0.0 |
| W8 | PCSX2 GS renderer in a separate process (TASK-032..044): decision docs, fork and CI, proof of concept, MIT stream protocol, live MVP, readbacks, window, settings, textures, performance, JIT-free build, launcher download, packaging | docs/decision-0070-gs-process, then per-task branches (task/034-*, task/035-gs-stream, ...) and fork branches gt4gs/* | Owner go/no-go gate after TASK-034 before TASK-035/036 (GSDX-PLAN section F). TASK-042 (JIT-free) is required before 1.0.0 |

TASK-045 (high frame rate on the PCSX2 GS, redone from TASK-031) comes after 1.0.0.

## Order and gates

1. W1 merged first (MTVU on by default in the main build).
2. W3 and W4 code is merged into task/019-launcher; W5 resumes with the shell, setup flow and settings.
   W8 runs in parallel: TASK-032 and TASK-033, then TASK-034 and the owner gate.
3. Integration build `release-1.0.0`: PCSX2 GS (`gt4-gs.exe`, JIT-free, TASK-042) with CoreGS 1.1 fallback,
   MTVU, input, save fixes; CTest/Python green; headless identity at default settings; installer builds.
4. Gate, renderer download, verify and fallback: the launcher downloads `gt4-gs.exe` during setup
   (one click, on by default), verifies its SHA-256 and protocol version, and falls back to CoreGS 1.1
   when it is missing or fails. Tested on a clean install with the network off.
5. **Owner test** of the installer end to end (fresh install, ISO setup, play, saves, controls, renderer
   options, renderer fallback) -> owner approval -> tag v1.0.0 and GitHub Release (installer asset,
   `gt4-gs` release assets, notes, known issues, trademark notice).
