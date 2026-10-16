# GT4Recomp Tasks

Single ordered task list. Each task has its own branch, automated checks, an
in-game test by the owner and an explicit owner approval before its PR is merged
into `main` (workflow rules in [CLAUDE.md](CLAUDE.md)). Evidence for finished
tasks lives in `docs/journal/` and `docs/inputs/`; background studies are linked
from [STATUS](docs/STATUS.md).

States: ✅ Done · 🧪 Waiting for owner test · 🔄 In progress · ⏭️ Next · 📋 Planned

Tasks are numbered in execution order (renumbered on 2026-10-15). Older
journals and commits use the previous numbers: 018→010, 010→011, 031→012,
014→013, 015→014, 016→015, 017→016, 020→017, 022→018, 023→019, 026→020,
013→021, 024→022, 027→023, 025→024, 028→025. Second renumbering (same day): 022→017, 021→018,
020→019, 019→020, phase 2/3 tasks 022-025→021-024; the in-game 16:9 task was
dropped (the owner's game already boots in 16:9). Earlier IDs 011, 012, 019, 029
were merged into the new 010/012; the pre-donor cleanup was done outside the
list (private repository `moesuito/gt4-staticrecomp-legacy`). Third renumbering
(2026-10-15, owner): 018→017 (license crash), 019→018 (wheels), 020→019
(launcher), 017→020 (decomp names, last in phase 1, after the first stable release).

## Phases

| Phase | Platform | Goal |
|---|---|---|
| **Phase 1** (current) | Windows PC | Original game running natively: correct speed, smooth 60 FPS, no interpreter on the normal path, PCSX2 GS renderer in a separate process (decision 0070), CoreGS 1.1 fallback |
| **Phase 2** | Android | ARM64 build with the ARMSX2 mobile GS pipeline (Vulkan) |
| **Phase 3** | iOS | ARM64 build with the ARMSX2 Metal GS pipeline |

**Renderer decision (2026-10-15, [decision 0070](docs/decisions/0070-pcsx2-gs-separate-process.md)):**
PC uses the **PCSX2 GS** in a separate GPL process, `gt4-gs.exe`, built from our fork
`moesuito/gt4recomp-gs` and downloaded by the launcher (TASK-032..044). **CoreGS 1.1** (our MIT
Vulkan compute renderer) is built in as the fallback and offline default. **CoreGS 2.0** (our MIT
hardware renderer, clean room from the TASK-026 specification; TASK-027) is planned and paused. paraLLEl-GS
is dropped from the release (0070 supersedes the renderer options of 0069). The game executable never
contains GPL code. Mobile uses the **ARMSX2 GS pipeline** (Vulkan on Android, Metal on iOS) without its JIT.
See [GS renderer options](docs/GS-RENDERER-OPTIONS.md) and [GSDX-PLAN](docs/GSDX-PLAN.md).

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
| TASK‑008 | Full static VU1 entry coverage (all 2,048 entries) | ✅ Done |
| TASK‑009 | Quick fixes: title, fixed 16:9, fullscreen, interval counters, races in 480p | ✅ Done |
| TASK‑010 | Build pipeline: faster builds and build tuning (parallel, cache, lld-link, fp-contract off, x86-64-v3 variant, LTO, PGO) | ✅ Done (tuning measurements pending) |
| TASK‑011 | MTVU: VU1 on its own thread, on by default | ✅ Done · in `--mode main` (slice178), `GT4_MTVU=0` A/B |
| TASK‑012 | paraLLEl-GS as an optional PC renderer | ❌ Dropped (decision 0070): LGPL code not shipped. Branch `task/012-parallel-gs` kept as history; MIT seam and capture code salvaged into TASK-035 |
| TASK‑013 | VU0 microcode compiled ahead of time | ✅ Done |
| TASK‑014 | EE entry coverage from the decomp function list | 🔄 Stage A done |
| TASK‑015 | LWR sign-extension fidelity check | ✅ Done (already correct; no change) |
| TASK‑016 | IOP per-module cost counters and review | ⏸️ Deferred (not needed for 1.0.0) |
| TASK‑017 | License-test crash | 🔄 In progress: waiting for the owner's FSV trace test |
| TASK‑018 | Racing wheels with force feedback | 📋 Planned |
| TASK‑019 | Launcher 1.0.0 (Rust core + Tauri 2 HTML frontend) and NSIS per-user installer | ⏸️ Paused (Tauri 2 WIP); resumes for TASK-043 (decision 0070) |
| TASK‑025 | Save manager: memory card slots, import/export | ✅ Code done, merged into `task/019-launcher`; ships with the launcher (owner test pending) |
| TASK‑026 | CoreGS 2.0, part 1: clean-room specification of the PCSX2 hardware renderer | ✅ Done (docs/hw-renderer-spec, PR #17) |
| TASK‑027 | CoreGS 2.0, part 2: MIT implementation from the specification (3x-6x, texture packs) | ⏸️ Paused (unchanged) · pure libraries done on `task/027-hw-renderer` |
| TASK‑028 | CoreGS 1.1: upscaling, interlaced-only deinterlace, FXAA-class AA, FSR 1.0 | ⏸️ Paused by the owner: 3x shows native-size aliasing on 3D models; stays the built-in fallback renderer (`task/028-donor-upscale`, latest slice186) |
| TASK‑029 | Controller and keyboard mapping (binding table, XInput LT/RT as L2/R2 pressure, controls file for the launcher) | ✅ Code done, merged into `task/019-launcher`; ships with the launcher (owner test pending) |
| TASK‑030 | Night-race readback wait (CoreGS 1.1) | ⏸️ Paused: owner test 59.07 FPS, wait unchanged at 4.3 ms because the page is drawn the same frame (`task/030-night-readback`) |
| 🚀 | **First stable release on GitHub** (after TASK‑019) | 📋 Planned |
| TASK‑020 | Decomp names in generated EE code (waits for more decompilation progress) | 📋 Planned |
| TASK‑031 | High frame rate (120-144 FPS) with game physics untouched | ❌ Dropped in this form; redone as TASK-045 on the PCSX2 GS (study docs on `task/031-high-fps`) |
| TASK‑032 | Decision 0070 docs: PCSX2 GS in a separate process, clean-room wording, renderer docs | ✅ Done (this PR) |
| TASK‑033 | PCSX2 GS fork (`moesuito/gt4recomp-gs`): reproducible build and CI releases | ✅ Done · standalone repo, CI green, prerelease `gt4gs-v0.1.0-poc` (docs/GS-FORK.md) |
| TASK‑034 | Proof of concept: replay our `.gscap` capture through the PCSX2 GS and compare images | ✅ Done · owner approved the images on 2026-10-16; gate passed (docs/GS-PCSX2-POC.md) |
| TASK‑035 | MIT GS stream protocol, shared-memory IPC and renderer seam on main | ✅ Done · slice191: CTest 29/29, Python 321 OK, donor 42/42, stream converts to the PoC dump byte for byte (docs/GS-STREAM-PROTOCOL.md) |
| TASK‑036 | Live `gt4-gs.exe` MVP: the game drives the PCSX2 GS over IPC | 📋 Planned (after the gate) |
| TASK‑037 | PCSX2 GS readbacks and the night race | 📋 Planned |
| TASK‑038 | GS window, fullscreen, focus, input and crash handling | 📋 Planned |
| TASK‑039 | GS settings table and `gt4-gs.ini` | 📋 Planned |
| TASK‑040 | Texture dumping and replacement packs | 📋 Planned |
| TASK‑041 | GS process performance: F8 `[gs-ipc]`, tuning, owner A/B | 📋 Planned |
| TASK‑042 | JIT-free `gt4-gs.exe` (no recompilers, no RWX, no SW JIT); required before 1.0.0 | 📋 Planned |
| TASK‑043 | Launcher: download, pin and update the GS renderer | 📋 Planned (waits for TASK-019 to resume) |
| TASK‑044 | Packaging, release notes and the release 1.0.0 plan update | 📋 Planned |
| TASK‑045 | High frame rate on the PCSX2 GS (TASK-031 redone) | 📋 Planned (after 1.0.0) |

## Phase 2 — Android

| Task | Goal | State |
|---|---|---|
| TASK‑021 | Mobile GS: ARMSX2 GS pipeline (Vulkan) behind our GS front end | 📋 Planned |
| TASK‑022 | Android app: ARM64 build, host layers, touch controls, Bluetooth controllers | 📋 Planned |

## Phase 3 — iOS

| Task | Goal | State |
|---|---|---|
| TASK‑023 | ARMSX2 Metal GS backend | 📋 Planned |
| TASK‑024 | iOS app: ARM64 build, signing, touch and controller input | 📋 Planned |

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

### TASK-008 — Full static VU1 entry coverage (slice 157)
All 2,048 entries of the decrypted VU1 image compiled. Owner test on an idle
CPU: night race 58.9 FPS (remaining cost is the 4.1 ms GS fence wait, for
TASK-012), rally 59.9, showroom 60.0; 0 interpreted VU1 calls in all three.

### TASK-009 — Quick fixes (slice 164)
Title "GT4Recomp", fixed 16:9, Alt+Enter/F11 borderless fullscreen, interval
counters, `late_frames` fixed. `--force-480p` holds the game's race display
option (index at `[0x622F4C]+0x38CB0+0x16B0`) at 480p: owner confirmed races
render 480p while menus stay 480i as in the original.

### TASK-010 — Build pipeline (slice 159)
Full build 3,942 → about 1,700 CPU-seconds: shared corpus library, split
generated tables, `-Wno-unused-label` on generated code. `GT4_FP_CONTRACT_OFF`
and `GT4_X86_64_V3` options exist (default off); their speed effect is Unknown
until an idle-CPU bench. ThinLTO/PGO not attempted.

### TASK-013 — VU0 microcode AOT (slice 160; main build slice 165)
All 14 VCALLMS entries compiled with exact MAC/status/clip (the EE reads them via
CFC2) and exact `run(0,pc)` hand-offs. 28,000-case differential vs the donor,
0 failures; headless frames identical. Owner test: 0 interpreted VU0 calls;
showroom 58.0, night 57.9, rally 60.0 VBlanks/s (performance neutral, VU0 cost
0.1-0.3 ms/frame). `--mode main` (slice 165) is the build of main with all of it.

### TASK-032 — Decision 0070 docs
Docs only, branch `docs/decision-0070-gs-process`: decision 0070 (PCSX2 GS in a separate process, owner answers recorded), `docs/GSDX-PLAN.md` with the go/no-go gate, TASK-032..045 in this file, CLAUDE.md clean-room wording, RELEASE-1.0.0-PLAN workstream W8, README renderer text, GS-RENDERER-OPTIONS note, journal 2026-10-15. No code, build or game launch.

## Phase 1 details

### TASK-011 — MTVU
Run VU1 programs on a worker thread (donor `start_vu_thread` exists), on by
default with an env switch to disable; exact sync points wherever the EE observes
VU1 results. Acceptance: identical headless frames, verify 0 mismatches, owner
showroom/night/race frametime equal or better.
State: ✅ Done (release 1.0.0 W1, branch `task/011-mtvu`). Plan in docs/MTVU-PLAN.md; step 0 (slice169 measurement), steps 1-4 (ring and journal, slice170 worker, slice171 per-thread F8, slice172 EE-side signals) and step 6 (`--cpu-cores N`) are in. Release W1 (slice178): MTVU is part of `--mode main`, on by default (`GT4_MTVU=0` is the A/B switch), and `--mode main` holds 480p by default. Checks: CTest 27/27, Python 281 OK (12 skipped), headless 4,200 frames: 42/42 identical to slice157 with MTVU on (three runs) and off (one run), and with the default environment. The PR into `main` is not opened yet (ready for review); the merge waits for owner approval. Owner A/B (MTVU-PLAN section 12.6) still pending. Details: BUILD.md "Slice178".

### TASK-012 — paraLLEl-GS as the PC renderer
Dropped (decision 0070): not shipped; LGPL code is not merged. Branch `task/012-parallel-gs` is kept as history; its MIT renderer seam and capture code are salvaged in TASK-035. The text below is the original plan.
One large task, delivered in verified steps on its own branch:
1. Import paraLLEl-GS (github.com/Arntzen-Software/parallel-gs, LGPL-3.0+, with
   Granite MIT) pinned by commit; build it in our CMake; bridge its Vulkan device
   to ours (Granite device shim or port).
2. Adapter from our GS front end (`write`, `write_packed`, `write_image`,
   `read_image`, privileged registers, signals, vsync) to its interface
   (`gif_transfer`, `write_register`, `map_vram_*`, `read_transfer_fifo`,
   `vsync` → scanout). Renderer selected by env (later the launcher); the donor
   renderer stays as experimental fallback.
3. GT4 validation: boot, menus, races, night, rally, showroom; image comparison
   against the donor renderer at 1x; channel-shuffle brightness effects, lens
   flares, half-pixel issues.
4. Options: super-sampling upscaling 2x-16x, deinterlacing/field options for the
   interlaced menus, page-tracked readbacks against the night-race wait.
5. Extras where paraLLEl-GS allows: post-processing (sharpening, FXAA, filters),
   dithering/texture options, texture dump/replacement feasibility.
Replaces the former donor-renderer upscaling, readback-wait and renderer-feature tasks.

### TASK-014 — EE entry coverage
Add translator entries for decomp functions without one, starting with the 15
referenced ones (docs/DECOMP-INTEGRATION.md).

### TASK-015 — LWR fidelity
State: analysis done, no runtime change (branch task/015-lwr-fidelity). The donor helper matches PCSX2:
offset 0 sign-extends, offsets 1-3 preserve the upper 32 bits. The brief's premise does not hold in code.
The lone-LWR scan found one site (0x00549178, `lwr r5, 0(r2)`). A change would need hardware evidence and
an overlay or ps2game.lib regeneration, so it is parked. Part B (silent audio test) is done.

### TASK-016 — IOP per-module counters
Per-module instruction counters and per-library HLE call counts in F8; decide
whether the sound modules need native HLE (docs/IOP-MAP.md).

### TASK-017 — License-test crash
Pre-existing; traced to `RaceSolitaire::loadDemoInputs` (0x3EAD18) →
`AutomobileControlRecord::Manager::load` (0x346BB8) reading a bad record count.
Env-gated logging there and in the PS2ZIP loader for `replay/license/` files,
plus a CDVD read trace; one owner reproduction; fix the cause.

### TASK-018 — Racing wheels
High-level emulation at the PDIUSB boundary with SDL3 wheels and force feedback
(docs/MENUS-AND-WHEEL.md).

### TASK-019 — Launcher
Paused (owner, 2026-10-15; Tauri 2 WIP). Resumes for TASK-043 (decision 0070).
Rust + egui or GPUI: game image, renderer (paraLLEl-GS / donor fallback),
upscaling, video mode, controls; CPU feature detection for build variants.

### TASK-025 — Save manager
One standard 8 MB PS2 card image per slot under the user data folder; the
launcher passes the active slot's card to the game. Import from single-save files
(.psu first; .max/.cbs/.xps later), from card images (.ps2/.mcd, listing their
saves) and from PCSX2 folder cards; import always creates a new slot and never
overwrites one. Export, delete to Recycle Bin, backups. Optional slot summary
(day, credits) from Nenkai's PDTools save format. Atomic card writes in the
runtime. Design: `docs/launcher/LAUNCHER-PLAN.md` on branch `task/019-launcher`.

### TASK-028 — CoreGS 1.1
Our current Vulkan compute GS renderer (CoreGS 1.x, donor lineage, MIT) gains:
internal resolution scale 1x-4x (`--mode upscale --scale N`, PS2_SCALE; 3x is
the release target), a deinterlace filter active only for interlaced output
(behaviour from our clean-room description in docs/hw-renderer-spec chapter 13,
no GPL/LGPL code copied), an FXAA-class AA pass and AMD FSR 1.0 (MIT) for
viewport scaling. Branch `task/028-donor-upscale`. Part of release 1.0.0
(docs/RELEASE-1.0.0-PLAN.md).

### TASK-029 — Controller and keyboard mapping
Binding table in the runtime (XInput auto-map, LT/RT as L2/R2 pressure,
keyboard remap, deadzones/curves) read from a controls file the launcher
writes; schema in docs/CONTROLS.md. Branch `task/029-input-mapping`.

### TASK-026 — Own hardware renderer: clean-room specification
Study only (decision 0069). The specification team reads the PCSX2 hardware GS
renderer (pinned source) and public GS documentation and writes a behavioral
specification in plain English under `docs/hw-renderer-spec/` (branch
`task/026-hw-renderer-spec`): architecture and data flow, GS state to GPU draw
translation, texture cache and render-target management, upscaling (internal
resolution multiplier and its fixes), CLUT, readbacks, blending and channel
shuffles, texture dumping/replacement and pack format, GT4-specific fixes,
post-processing, validation scenes and test cases, and an implementation plan
with milestones for TASK-027. No PCSX2 code or code-like translation in the
specification. Sizes and coupling overview: `docs/GSDX-STUDY.md` on branch
`task/012-parallel-gs`.

State (2026-10-15): specification complete in `docs/hw-renderer-spec/`
(README index, chapters 00-15; chapter 15 holds milestones M0-M19 for TASK-027,
~94 implementer-days, risks and 13 open owner questions). Ready for owner review;
TASK-027 starts after approval and the answers to Q-01 to Q-03.

### TASK-027 — Own hardware renderer (CoreGS 2.0): implementation
**Paused by the owner on 2026-10-15** to focus on release 1.0.0. State on the
integration branch `task/027-hw-renderer`: CPU-verified pure libraries for GS
memory/swizzle (M2), GIF front end and CLUT (M3), exact pixel arithmetic (M5),
display/deinterlace/LOD, texture-replacement keys and pack loading, surface
cache bookkeeping with an authority fuzz (M7 core), per-draw decisions, the
shader toolchain with GPU shaders bit-exact against the CPU reference (42.5 M
blend cases), a CPU reference renderer that replays real GT4 captures (menus
mostly match the donor; garage frame and a 2-line field offset open as
IN-315/IN-316), the M1 comparison tools, and the game hookup (GT4_RENDERER=hw,
black output). WIP branches: task/027-m4-gpuref (GPU write-back path) and
task/027-cpuref. Interpretations awaiting confirmation:
`docs/hw-renderer-spec/IMPLEMENTATION-NOTES.md` on the integration branch.
MIT implementation written only from the TASK-026 specification, behind our GS
front end next to paraLLEl-GS and the donor renderer. Implementers never open
PCSX2 GS sources. Milestones and acceptance come from the specification.

### First stable release
After TASK-019: a tagged GitHub Release of this repository with the NSIS
installer (prebuilt game executable and launcher; the user supplies their own
disc; official branding with a trademark notice; decision 0069), under the MIT license
([decision 0067](docs/decisions/0067-mit-license-and-release-model.md)). TASK-020
is deliberately after it, to give the gt4-decomp project more time to progress
before its names are imported.

### TASK-020 — Decomp names in generated code
Label comments in generated units with identical object code.

Common rules for TASK-032..045: agents that open PCSX2 sources or the fork are PCSX2-tainted and never
work on TASK-027. MIT files in this repository never contain PCSX2 code. The fork's own rules: one writer at
a time, commits as `moesuito`, every release built by CI from a tag.

### TASK-032 — Decision 0070 docs
Repo: main, branch `docs/decision-0070-gs-process`. Haiku, 1 session.
Goal: add `docs/decisions/0070-pcsx2-gs-separate-process.md`, `docs/GSDX-PLAN.md`, the CLAUDE.md
clean-room paragraph and renderer lines, GS-RENDERER-OPTIONS update, these TASKS rows, STATUS and journal.
Acceptance: owner answers to GSDX-PLAN section G recorded; links valid; Python suite unchanged.
Parallel: none needed; first.

### TASK-033 — Fork, reproducible build, CI releases
Repo: standalone public repository `moesuito/gt4recomp-gs` (not a GitHub fork: the account already has a fork in
the PCSX2 network, ARMSX2, and GitHub allows one per network). Full PCSX2 history at tag `v2.8.2`
(`fd9d310c`); branch `gt4gs/main` is the default. Haiku, Opus if the dependency build stalls; 2-3 sessions.

**Result (2026-10-15, 3 CI runs, 1 fix).** Details and run URLs in [docs/GS-FORK.md](docs/GS-FORK.md).
- Fork: `gt4gs/main` at `70a0aa66f`; `gt4gs/` docs, trimmed dependency script (Qt removed), packaging script,
  `gt4gs-windows.yml`; upstream workflows set to `workflow_dispatch` only (no file deleted).
- Release `gt4gs-v0.1.0-poc` (prerelease, tag on `3fd7faba8`): zip 18,202,463 bytes, source archive 24,954,987
  bytes, `SHA256SUMS`. Checksums verified after download.
- Deviation: MSVC (VS 18, MSVC 14.51, CMake 4.4.4, Ninja 1.13.2) instead of clang-cl; upstream's CI matrix
  also builds CMake with MSVC.
- Not done: GSRunner smoke with `-renderer null` (no dump in the repo, and game data must not be committed);
  bit-identical local rebuild; build provenance attestation. The first run failed on the VS locator and is kept
  in the run history.
Goal: CMake + Ninja + clang-cl build of the unmodified `pcsx2-gsrunner` (`-DENABLE_QT_UI=OFF
-DENABLE_GSRUNNER=ON`, Vulkan/D3D11/D3D12), dependency script trimmed of Qt and cached, `gt4gs/BUILD.md`,
NOTICE, third-party license list, a release workflow that publishes `gt4-gs-<ver>-win64.zip`, its SHA-256,
a source archive and build provenance.
Acceptance: CI green from a clean runner; release zip contains COPYING.GPLv3, NOTICE, licenses; GSRunner
smoke on CI with `-renderer null` on a tiny dump; local rebuild from the tag on the owner's machine produces
the same file list (bit-identical is a goal, recorded either way).
Risks: dependency build time and breakage; runner disk limits.
Parallel: with TASK-035.

### TASK-034 — Proof of concept: our capture through the PCSX2 GS
Repos: fork branch `gt4gs/gscap2gs` (GPL converter) and main branch `task/034-gs-replay-compare` (MIT
compare script). Haiku, Opus for the GS freeze state; 2-3 sessions.
Goal: `gscap2gs` converts `private/slice175-capture/menu-0-1100.gscap` to a `.gs` dump: header with serial
`SCUS-97328`, freeze state of a freshly reset GS, an 8 KiB privileged block maintained from 'P' records and
emitted as Registers before each VSync, 'G' path 1/2/3 to Path1New/Path2/Path3, 'R' to ReadFIFO2, 'V' to
VSync with alternating field. Stock GSRunner replays it (`-dumpdir`, `-renderer vulkan`, `-upscale 1` and
`4`, `-perf`).
Acceptance: 1,101 frames replayed, no errors; frame count and VSync count match the capture; a MIT script
compares frames 100..1100 with the donor frames in `private/slice175-capture/run-menu/frames` (pct_bad > 8,
PSNR, as in `scripts/compare_frame_dumps.py`) and writes a JSON report and contact sheet; the 4x replay
shows upscaled output; GS-thread ms/frame from `-perf` recorded; GameDB fixes for SCUS-97328 reported as
active in the log. Owner review of side-by-side images.
Risks: freeze-state format (only in the fork); field parity; the donor is not bit-identical to PCSX2, so
the gate is "recognisably the same frame", not equality.
Parallel: with TASK-035 (different repos).

### TASK-035 — MIT GS stream protocol, IPC and renderer seam
Repo: main, branch `task/035-gs-stream`. Opus, 4-6 sessions.
Goal: `native/gs_stream/gs_stream_protocol.h` (plain C, versioned records GIF, PRIV, VSYNC, READ_FIFO,
RESET, CONTROL, reserved STATE and AUX_FRAME; back-channel READY, STATS, ERROR, SNAPSHOT_DONE, EXITING;
control block; written from GS hardware semantics and our `.gscap`, not from PCSX2 code); cross-process SPSC
ring with named events and peer-handle waits; `GT4_RENDERER` dispatch on main with values `donor` (default)
and `pcsx2`, salvaging only the MIT seam from task/012; `GT4_GS_CAPTURE` (generalized `GT4_PGS_CAPTURE`,
capture v2 header); a MIT consumer `gs_stream_sink` (records to `.gscap`, or discards) used as the test peer;
the MTVU journal drain writes to the ring; signals applied on the EE for all paths.
Acceptance: CTest ring fixtures (wrap, split, full ring, peer death, spurious wake, 10^6 random records
byte-identical); protocol round trip; headless 4,200-frame run with `GT4_RENDERER=pcsx2` against the sink:
the sink's capture is byte-identical to the in-process `GT4_GS_CAPTURE` of a `donor` run with the same
schedule; donor frames still 42/42 identical to slice157 with the default renderer; Python suite green;
launcher pins updated.
Risks: ordering between MTVU worker output and EE work; readback blocking with the worker.
Parallel: with TASK-033/034.
State (2026-10-15, slice191, branch `task/035-gs-stream`): built; owner review pending, no PR.
- Protocol `native/gs_stream/gs_stream_protocol.h` (C++20 header, fixed-width structs) and the normative
  [GS-STREAM-PROTOCOL](docs/GS-STREAM-PROTOCOL.md); transport `gs_stream_ipc` (mapping, rings, events with
  waiter flags, peer-handle waits, Job Object spawn); `.gscap` v2 files; game client `gs_stream_client` spliced by
  `prepare_external_vulkan.py --gs-stream-hooks` and `prepare_perf_runtime.py --gs-stream`; consumer
  `tools/gs_stream_record` (recorder or null consumer; readbacks answered with zeros); F8 `[gs-ipc]`.
- CTest 29/29 (`native_gs_stream_ipc`, `native_gs_stream_client` new); Python 321 OK, 12 skipped.
- Headless 4,200 frames: donor 42/42 identical (default and with `GT4_GS_CAPTURE`); `GT4_RENDERER=pcsx2` completes;
  donor capture == pcsx2 in-process capture == recorder file (plus CONTROL shutdown); 619 KB and 390 records per
  frame, 0 readbacks. Converted frames 0-1100 equal the TASK-034 PoC dump byte for byte; GSRunner PNGs 1,100/1,100
  identical to the PoC (300 and 700 included). EE cost in menus: `gs_ipc_write` about 0.08 ms/frame, VSYNC post
  0.004 ms/frame, no back-pressure.
- Not verified: readbacks over IPC in the game (none in the schedule; covered by fixtures only), race scenes,
  window mode (`--mode pcsx2-stream` shows a black window by design), weak CPUs.

### TASK-036 — Live MVP
Repos: fork branch `gt4gs/live`, main branch `task/036-gs-live`. Opus, 4-6 sessions.
Goal: fork adds `pcsx2-gt4gs/` frontend (from GSRunner) producing `gt4-gs.exe`, `GSStreamReplayer` (an R5900
CPU provider reading the ring or a `.gscap` with `--replay-gscap`), handshake, child window in the game
window, VMManager hooks; the protocol header vendored with its hash checked. Main: spawn with Job Object,
handshake, pin check of `gt4-gs.exe`, `play_performance.py --mode pcsx2-gs` with a new pinned manifest,
headless frame dumps via `CONTROL snapshot`.
Acceptance: headless run: live frames equal the offline `--replay-gscap` frames of the same session capture;
boot to menus, Time Trial, race at 1x and 3x; GS crash (killed) ends the game cleanly with a message and a
flushed card; protocol-version mismatch refused; owner plays the reference scene.
Risks: VMManager boot without BIOS in stream mode; first-run shader compile stutter.
Parallel: 037/038/039 start once the MVP boots.

### TASK-037 — Readbacks and night race
Repos: both, branches `task/037-gs-readback`, `gt4gs/readback`. Opus, 2-3 sessions.
Goal: READ_FIFO round trip through `MTGS::InitAndReadFIFO`; `HWDownloadMode` and readback spin options
exposed; F8 counts and wait time; Unsynchronized mode served without draining the ring (if the owner allows).
Acceptance: CTest/fixture with a scripted stream (draw, TRXDIR=1, read) compared with the donor bytes;
headless frames unchanged; owner night race at 60 FPS with `gs_ipc_readback_wait` reported; lens flare and
night lighting correct versus CoreGS 1.1.
Risks: GPU sync cost at high upscale.
Parallel: with 038/039.

### TASK-038 — Window, fullscreen, focus, input, lifecycle
Repos: both, branches `task/038-gs-window`, `gt4gs/window`. Opus, 3-4 sessions.
Goal: child window hardening (own pump thread, `HTTRANSPARENT`, `MA_NOACTIVATE`), Alt+Enter/F11 borderless,
resize, minimize, DPI, multi-monitor, focus loss; GS hotkeys forwarded (screenshot, OSD toggle); fallback
option 2 (GS-owned window, keyboard forwarding) behind a switch.
Acceptance: Python/CTest for the message contract; a scripted soak (resize and fullscreen toggles, 1,000
cycles) with no hang; a GS-thread stall test does not freeze our window beyond the stall; RTSS overlay
visible; owner checklist (fullscreen, alt-tab, minimize, controller and keyboard).
Risks: attached input queues; DWM latency in child windows.
Parallel: with 037/039.

### TASK-039 — Settings table and `gt4-gs.ini`
Repos: main `task/039-gs-settings`, fork `gt4gs/config`. Haiku, 2-3 sessions.
Goal: `docs/launcher/gs-settings.toml` (GSDX-PLAN D.1), generator to `gt4-gs.ini` (dev script now, launcher
later), allow-list validation in `gt4-gs.exe`, folders under the user data directory, `CONTROL
reload_config`.
Acceptance: Python tests (table schema, generated INI golden file, refusal of unknown keys and out-of-range
values); `gt4-gs.exe` reports effective settings in READY; owner checks 1x/3x/6x, FXAA, CAS, dithering,
deinterlace in menus.
Parallel: with 037/038.

### TASK-040 — Texture dumping and replacement
Repos: fork `gt4gs/textures`, main docs. Haiku, 1-2 sessions.
Goal: folder layout `...\gs\textures\SCUS-97328\{replacements,dumps}` and `.zip` packs; dump, load, async,
precache options; user documentation.
Acceptance: scripted test: dump from a replayed capture, replace one texture with a solid colour, replay,
detect it in the frame; owner tries a community GT4 pack if available.
Parallel: after 039.

### TASK-041 — Performance
Repos: both, branches `task/041-gs-perf`, `gt4gs/perf`. Haiku for counters, Opus for tuning; 3-4 sessions.
Goal: F8 `[gs-ipc]` section (game: write, back-pressure, vsync post, readback wait, bytes, records; GS:
thread busy, GPU ms, draws, uploads, readbacks, present wait, ring occupancy); tuning: frames in flight,
ring size, direct GS-thread feed; `--cpu-cores 2` check.
Acceptance: `perf_probe_fixture` covers the new categories; owner A/B in showroom, night race, rally,
reference scene: CoreGS 1.1 vs PCSX2 GS at 1x and 3x, 60 FPS, flat frametime, no measurements while builds run.
Parallel: after 037.

### TASK-042 — JIT-free `gt4-gs.exe`
Repo: fork, branch `gt4gs/no-jit`. Opus, 3-5 sessions.
Goal: `GT4GS_ONLY` build without EE/IOP/VU recompilers, without RWX code mapping, with the C draw-scanline
path or CPU sprite/CLUT render refused.
Acceptance: binary scan finds no recompiler symbols; a VirtualAlloc/VirtualProtect audit shows no executable
private memory; PoC and live frames unchanged; GSRunner tests still pass.
Parallel: with 037-041 (fork-only).

### TASK-043 — Launcher integration
Repo: main, branch `task/019-launcher` (when the launcher resumes). Haiku, 2-3 sessions.
Goal: renderer manifest (version, URL, size, SHA-256, protocol version, upstream commit) in the launcher;
download with progress and resume, verify, unpack to the user data folder, update to newer pinned versions,
offline fallback to CoreGS 1.1; settings pages generated from the table; license/source links shown.
Acceptance: Rust tests (hash mismatch refused, partial download resumed, wrong protocol refused, fallback);
owner end-to-end install.

### TASK-044 — Packaging and release notes
Repos: both. Haiku, 1-2 sessions.
Goal: fork release `gt4-gs v1.0.0` (notes: upstream commit, our patch list, license, source); main: README
renderer section, `native/THIRD_PARTY.md`, RELEASE-1.0.0-PLAN renderer table and W8, known issues.
Acceptance: release assets complete; links valid; owner approval.

### TASK-045 — High frame rate on the PCSX2 GS
Repos: both. Opus, 10-14 sessions, after release 1.0.0.
Plan: (1) D1: VRR and present-mode tests, no interpolation (1 session). (2) Oracle on the PCSX2 GS: the MIT
`tools/gs_interp` builds interpolated and extrapolated frames as streams; replay them with `gt4-gs.exe
--replay-gscap` and compare with real frames (race capture needed; kill if matched primitives < 95% or
race PSNR < 32 dB) (2-3 sessions). (3) In engine: the game holds frame N+1, generates the in-between stream
(MIT, game side) and sends it as `AUX_FRAME` before N+1; the GS renders and presents it without guest-visible
side effects (readbacks and signals stripped; targets overwritten by N+1); present scheduler for 120/144 Hz;
+1 frame latency measured (6-10 sessions).
Risks: texture-cache pollution by auxiliary frames, page alternation (HIGH-FPS 9.3), HUD.

## Phase 2 — Android details

### TASK-021 — Mobile GS (ARMSX2 pipeline, Vulkan)
Bring the ARMSX2 GS pipeline (PCSX2 hardware renderer with ARMSX2's Android
changes: GPU driver profiles, tile-friendly pass scheduler, framebuffer-fetch
policy) behind our GS front end. ARMSX2's ARM64 JIT is not used (no JIT rule).
Owner's ARMSX2 checkout: `C:/Antigravity/armsx2` (upstream `ARMSX2/ARMSX2`).

### TASK-022 — Android app
ARM64 build of the AOT EE/VU code and runtime (fp-contract off), Android host
layers (window, audio, input, storage, lifecycle), on-screen controls that hide
when a Bluetooth controller connects; target Galaxy S23 Ultra.

## Phase 3 — iOS details

### TASK-023 — Metal GS (ARMSX2)
ARMSX2's Metal backend for the same GS pipeline.

### TASK-024 — iOS app
ARM64 build, iOS host layers, signing/installation on iPhone 15 Pro Max and
17 Pro Max (built on a Mac with Xcode).

## Ideas (not scheduled)

LAN-over-internet and an online lobby launcher (docs/ONLINE-NOTES.md), in-game
settings menus, Spec II as a second target or data source (docs/SPEC2-STUDY.md).

A decompilation-based source port (native renderer, changing the game's physics
timing, ultrawide, remaster) is out of scope for this repository and would be a
separate project, possibly a fork. Presenting more than 60 FPS with the game's
fixed 1/60 s physics untouched is in scope (TASK-031, owner 2026-10-15).

### TASK-031 — High frame rate (physics untouched)
Dropped in this form (decision 0070): redone as TASK-045 on the PCSX2 GS after 1.0.0. Study docs stay on branch `task/031-high-fps`.
Goal: present 120-144 FPS while the game's physics keeps its fixed 1/60 s step
(`RaceBase__updateRace` uses a constant dt; read-only study, 2026-10-15).
Approaches: (A) GS primitive interpolation between two guest frames, with real
rasterised in-between frames and no optical flow or frame generation. It costs
about half a guest frame of latency, and an extrapolation variant is measured
alongside. (B/C) A native render rate: re-run the game's render path (EE scene
submission and VU1) with interpolated car and camera transforms, via decomp
hooks. Step 1: an offline oracle that builds frame N from captured frames N-1
and N+1 (and an extrapolated frame from N-2 and N-1) and compares it with the
real frame N; kill if fewer than 95% of primitives match or race PSNR is below
32 dB. In parallel, a read-only study of the native render-rate path: main
loop, update/render split, transforms and render cost. Input-latency trims
(submit ring depth, present mode) are measured with the prototype.

### TASK-034 — PCSX2 GS proof of concept (offline replay)
Branch `task/034-gs-replay-compare`. Goal: prove that the PCSX2 GS (decision 0070, separate GPL process) renders
GT4 correctly from our GS stream, offline. Done: `tools/gscap_to_gsdump/` converts the menu capture to a `.gs`
dump (23 unit tests); stock `pcsx2-gsrunner` built from PCSX2 `685637d` replays it at 1x and 3x without errors
(1,100 frames each); the GameDB fixes for SCUS-97328 apply from the serial. Frames 300 and 700 match the reference
exactly; frames 900 and 1100 do not, and the PCSX2 output is 480 rows against the references' 448.
Recommendation: GO for TASK-033 and TASK-035; conditional GO for TASK-036 (open: the vertical scale, the 900/1100
differences, a freeze state). Details and numbers: [docs/GS-PCSX2-POC.md](docs/GS-PCSX2-POC.md). Clean room:
PCSX2-tainted work, not touching CoreGS 2.0. Owner review: the side-by-side images and frames 900 and 1100.
