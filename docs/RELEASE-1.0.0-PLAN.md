# Release 1.0.0 plan (owner decisions 2026-10-15)

Goal: first stable GitHub Release: NSIS per-user installer with the prebuilt
game executable, the launcher 1.0.0, renderer **CoreGS 1.1** preselected,
controller and keyboard input with a mapping menu, save/replay manager. No
racing wheels (TASK-018) and no IOP work (TASK-016 deferred). Unsigned installer.

## Renderer names

| Name | What | Launcher entry |
|---|---|---|
| **CoreGS 1.x** | Our current Vulkan compute GS renderer (donor lineage, MIT). Major updates bump the version: 1.0 today, **1.1** for this release | Default, preselected |
| paraLLEl-GS | Integrated LGPL renderer (TASK-012), ships as a DLL | Option |
| **CoreGS 2.0** | Our own MIT hardware renderer, clean room from a behavioral spec of the PCSX2 hardware renderer (TASK-026/027) | Visible, greyed out ("coming soon") |

## Workstreams (each its own branch and worktree, one writer each)

| Id | Workstream | Branch | Notes |
|---|---|---|---|
| W1 | MTVU merge (TASK-011) | task/011-mtvu -> main | Owner authorised PR and merge; default on |
| W2 | CoreGS 1.1: upscaler up to 3x, deinterlace (interlaced modes only), shader AA (FXAA-class), FSR 1.0 viewport scaling | task/028-coregs-1.1 | Deinterlace behaviour taken from our clean-room spec (docs/hw-renderer-spec chapter 13 and native/hw_gs/display reference), which describes the PCSX2/paraLLEl-GS behaviour; no GPL code is copied. FSR 1.0 is MIT (AMD). AA written from the published algorithm or a permissive implementation |
| W3 | Input: binding table, XInput auto-map with LT/RT as L2/R2 pressure, keyboard remap, controls config file read by the game | task/029-input-mapping | docs/launcher/CONTROLS-STUDY.md |
| W4 | TASK-025 save manager: runtime atomic card flush and flush on exit; Rust save crate (card image read/write, folder model, .psu and card-image import, PCSX2 folder import) | task/025-save-manager | Design in docs/launcher/LAUNCHER-PLAN.md section 13 |
| W5 | TASK-019 launcher 1.0.0 (Rust + Slint) and NSIS installer | task/019-launcher | Design approved; integrates W3 controls page and W4 saves/replays pages; renderer dropdown per the table above |
| W6 | TASK-017 license-test crash | task/017-license-crash | Lead: GT4 JP/Asia use EE "chop" rounding to fix a B5 license crash |
| W7 | CoreGS 2.0 milestones (TASK-027) | task/027-hw-renderer | Continues in parallel; not part of 1.0.0 |

## Order and gates

1. W1 merged first (MTVU on by default in the main build).
2. W2, W3, W4 in parallel; W5 starts with shell, setup flow and settings, then
   integrates W3/W4 interfaces.
3. Integration build `release-1.0.0`: CoreGS 1.1 + MTVU + input + save fixes;
   CTest/Python green; headless identity at default settings; installer builds.
4. **Owner test** of the installer end to end (fresh install, ISO setup, play,
   saves, controls, renderer options) -> owner approval -> tag v1.0.0 and
   GitHub Release (installer asset, notes, known issues, trademark notice).
