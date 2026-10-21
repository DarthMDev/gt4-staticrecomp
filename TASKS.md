# GT4Recomp Tasks

Single ordered task list. Each task has its own branch, automated checks, an
in-game test by the owner and an explicit owner approval before its PR is merged
into `main` (workflow rules in [AGENTS.md](AGENTS.md)). Evidence for finished
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
| TASK‑014 | EE entry coverage from the decomp function list | ✅ Done (2026-10-21) · 1,105 new entries (+3,728 dispatch ids), slice208 `pcsx2-coverage`; headless 42/42 identical; owner play-through approved |
| TASK‑015 | LWR sign-extension fidelity check | ✅ Done (already correct; no change) |
| TASK‑016 | IOP per-module cost counters and review | ⏸️ Deferred (not needed for 1.0.0) |
| TASK‑017 | License-test crash | ✅ Done (2026-10-16) · license demo guard on both demo loaders (slice206, `--mode pcsx2`): licenses playable, demos that decrypt wrong are skipped as in PCSX2 default; root cause moved to TASK‑063 |
| TASK‑018 | Racing wheels with force feedback | 📋 Planned |
| TASK‑019 | Launcher 1.0.0 (Rust core + Tauri 2 HTML frontend) and NSIS per-user installer | ✅ Launcher merged into main (2026-10-21): setup with disc copy, gt4-gs download, settings, saves, controls (SDL3), tray, updates; owner tested end to end. Still for 1.0.0: NSIS installer build and the v1.0.0 release |
| TASK‑020 | Decomp names in generated EE code (waits for more decompilation progress) | 📋 Planned |
| TASK‑025 | Save manager: memory card slots, import/export | ✅ Code done, merged into `task/019-launcher`; ships with the launcher (owner test pending) |
| TASK‑026 | CoreGS 2.0, part 1: clean-room specification of the PCSX2 hardware renderer | ✅ Done (docs/hw-renderer-spec, PR #17) |
| TASK‑027 | CoreGS 2.0, part 2: MIT implementation from the specification (3x-6x, texture packs) | 🔄 Resumed (owner, 2026-10-21) on `task/027-coregs2` (main 1.0.0 + the 027 work) · external agent GPT 6.1 Sol, clean room · brief `AGENT-BRIEF-COREGS2.md` |
| TASK‑028 | CoreGS 1.1: upscaling, interlaced-only deinterlace, FXAA-class AA, FSR 1.0 | ⏸️ Paused by the owner: 3x shows native-size aliasing on 3D models; stays the built-in fallback renderer (`task/028-donor-upscale`, latest slice186) |
| TASK‑029 | Controller and keyboard mapping (binding table, XInput LT/RT as L2/R2 pressure, controls file for the launcher) | ✅ Done (2026-10-21) · merged with the launcher; owner tested remapping in game |
| TASK‑030 | Night-race readback wait (CoreGS 1.1) | ⏸️ Paused: owner test 59.07 FPS, wait unchanged at 4.3 ms because the page is drawn the same frame (`task/030-night-readback`) |
| TASK‑031 | High frame rate (120-144 FPS) with game physics untouched | ❌ Dropped in this form; redone as TASK-045 on the PCSX2 GS (study docs on `task/031-high-fps`) |
| TASK‑032 | Decision 0070 docs: PCSX2 GS in a separate process, clean-room wording, renderer docs | ✅ Done (PR #24) |
| TASK‑033 | PCSX2 GS fork (`moesuito/gt4recomp-gs`): reproducible build and CI releases | ✅ Done · standalone repo, CI green, prerelease `gt4gs-v0.1.0-poc` (docs/GS-FORK.md) |
| TASK‑034 | Proof of concept: replay our `.gscap` capture through the PCSX2 GS and compare images | ✅ Done · owner approved the images on 2026-10-16; gate passed (docs/GS-PCSX2-POC.md) |
| TASK‑035 | MIT GS stream protocol, shared-memory IPC and renderer seam on main | ✅ Done · slice191: CTest 29/29, Python 321 OK, donor 42/42, stream converts to the PoC dump byte for byte (docs/GS-STREAM-PROTOCOL.md) |
| TASK‑036 | Live `gt4-gs.exe` MVP: the game drives the PCSX2 GS over IPC | ✅ Done · owner test 2026-10-16 at 3x (sharp, fullscreen OK): Nordschleife 59.98 FPS, night race with 5 AI 59.89 FPS, readback wait 0.93 ms; `--mode pcsx2` holds 480p by default |
| TASK‑037 | PCSX2 GS readbacks and the night race | ✅ Done · owner test 2026-10-16: night race 59.97 FPS, 69 late frames (was 213); accurate readbacks are the default, unsynchronized optional (drops headlight flares) |
| TASK‑038 | GS window, fullscreen, focus, input and crash handling | ✅ Done · owner test 2026-10-16: fullscreen, Alt+Tab and the click-in-picture focus fix (gt4-gs 0.3.1); crash grace frames and CoreGS 1.1 restart offer |
| TASK‑039 | GS settings table and `gt4-gs.ini` | ✅ Done (main-repo part): 51-row table drives `gt4-gs.ini`; allow-list enforced by gt4-gs 1.0.0 (slice201: ignored with a warning) |
| TASK‑040 | Texture dumping and replacement packs | ➡️ Folded into TASK-050 (mod manager, release 1.1.0) |
| TASK‑041 | GS process performance: F8 `[gs-ipc]`, tuning, owner A/B | ✅ Done · owner test 2026-10-16: night race 59.97 FPS, 53 late frames (was 69), EE critical path 11.8 of 16.7 ms (headroom 4.9 ms); GS wake batching and ordered privileged writes |
| TASK‑042 | JIT-free `gt4-gs.exe` (no recompilers, no RWX, no SW JIT); required before 1.0.0 | ✅ Done · owner test 2026-10-16 (menus, night race): gt4-gs 0.4.0 JIT-free, enforced by Arbitrary Code Guard; images identical to 0.3.2 |
| TASK‑043 | Launcher: download, pin and update the GS renderer | ✅ Done (2026-10-21) · the launcher installs gt4-gs 1.0.1 from the GitHub release at setup; owner tested |
| TASK‑075 | Input layer on SDL3 (game side in slice210: SDL3 3.4.18 gamepads mapped to the DS2 layout, controls.toml applied, GT4_INPUT=sdl default / xinput for one release) | ✅ Done (2026-10-21) · game (slice210) and launcher read pads through SDL3 3.4.18; owner tested and approved |
| TASK‑076 | Modern fixed keyboard layout (several keys per DS2 button, table in docs/CONTROLS.md `keyboard-default`) | ✅ Done (2026-10-21) · fixed keyboard layout in game and launcher, P = Start (owner choice); owner tested |
| TASK‑044 | Packaging, release notes and the release 1.0.0 plan update | ✅ Done (2026-10-17) · release 1.0.0 ships gt4-gs 1.0.1; fork release notes, README, THIRD_PARTY and RELEASE-1.0.0-PLAN updated (docs only) |
| TASK‑045 | High frame rate on the PCSX2 GS (TASK-031 redone) | 📋 Planned (after 1.0.0) |
| TASK‑046 | Launcher "Prepare shaders": compile GT4's PCSX2 GS pipeline list headless (gt4-gs --warm-selectors) and keep the cache warm | 📋 Planned (study done, 2026-10-16) |
| TASK‑047 | PCSX2 GS deinterlacing in the 480i menus (field and interlace state in the stream) | ✅ Done · owner test 2026-10-16: 480i menus clean on Automatic; root cause missing SYNCV and a one-field-early VSYNC field (protocol 1.1, gt4-gs 0.3.2) |
| TASK‑048 | Mod manager in the launcher (umbrella): texture packs first, Content Manager style | 📋 Planned for 1.1.0 (umbrella; owner decisions 2026-10-16) |
| TASK‑049 | GS settings curation for the launcher: Graphics and Advanced pages, reset to defaults, 12x, widescreen finding | ✅ Done · decisions 2026-10-16; default gt4-gs.ini unchanged; 58-row table ([settings](docs/GS-SETTINGS.md)) |
| TASK‑050 | Texture replacement contract and headless dump/replace round trip (absorbs TASK-040) | 📋 Planned for 1.1.0 |
| TASK‑051 | `gt4mods` core: manifest, pack detection, validation, import, store | 📋 Planned for 1.1.0 |
| TASK‑052 | Mods active view (hardlinks), priority, conflicts, uninstall, `gt4mods` CLI | 📋 Planned for 1.1.0 |
| TASK‑053 | GS settings and dev launcher wiring for mods (texture rows off Graphics, `[Folders] Textures`) | 📋 Planned for 1.1.0 |
| TASK‑054 | Launcher Mods page (mockup, then Tauri) | 📋 Planned for 1.1.0 |
| TASK‑055 | Mod creator tools: dump workspace, package as mod | 📋 Planned for 1.1.0 |
| TASK‑056 | Live texture reload in gt4-gs | 📋 Planned for 1.1.0 |
| TASK‑057 | Study: other mod kinds (GS presets, VOL overlay, data patches) | 📋 Planned for 1.1.0 |
| TASK‑058 | Crash-safe memory card in the real build: atomic writer, dirty retry, exit flush (GS-loss path too) | ✅ Done · owner test 2026-10-16; kill test 25/25; card flush on exit and after a GS loss (slice200; in slice202) |
| TASK‑059 | Distribution executable: installed-folder shaders and GS package, no repo paths, `--portable-check`, `scripts/make_distribution.py` | ✅ Done · owner test 2026-10-16; distribution folder smoke 42/42 and 41/41 (slice200; slice202: also from `private/slice202-dist`) |
| TASK‑060 | Mouse cursor auto-hide over the picture (fullscreen and windowed, game and gt4-gs agree) | ✅ Done · owner test 2026-10-16 (fullscreen and windowed); slice201, in slice202 |
| TASK‑061 | "Compiling shaders…" notice in gt4-gs (real compiles only) | ✅ Done · owner test 2026-10-16; gt4-gs 1.0.0 (cold cache 238 compiles shown, warm cache none) |
| TASK‑062 | Performance overlay (FPS, frame time, CPU/GPU of the game, resolutions, frame cost and headroom) | ✅ Done · owner test 2026-10-16; overlay off by default, +0.35 us/frame EE, +0.02 ms/frame GS thread |
| TASK‑063 | Bit-exact R5900 FPU `div.s`/`mul.s` (license demo phase, real-PS2 card replays and ghosts) | 📋 Planned · oracle ready (`scripts/license_demo_oracle.py`); needs hardware vectors |
| TASK‑064 | Third-party overlays (MSI Afterburner / RivaTuner) with the PCSX2 GS: Arbitrary Code Guard off by default | ✅ Done (2026-10-17) · owner test: the RivaTuner/Afterburner overlay shows in game with default settings |
| TASK‑077 | Context-aware input mapping (race vs menus, e.g. Esc = Start in a race, Triangle in menus) | 📋 Open, study done (docs/CONTEXT-INPUT-STUDY.md); not in 1.0.0 |
| TASK‑078 | Graphics settings inside GT4's own Options menu (output resolution 480p to 8K replacing the 480i/480p/1080i row, fullscreen, anti-aliasing); game fixed at 480p | 📋 Planned (after 1.0.0), study done (docs/INGAME-SETTINGS-STUDY.md) |
| TASK‑079 | Force 16:9 at boot: `GT4_FORCE_WIDESCREEN` holds the game's own Wide flag at 16:9 (slice211); launcher setting "Widescreen 16:9", default on | ✅ Done (2026-10-21) · slice211, GT4_FORCE_WIDESCREEN default on, launcher "Widescreen 16:9"; owner tested (boots 16:9 and 480p; the 100% save loads). The "Load failed!" seen on the way was the launcher card bug, fixed in TASK-019 |
| TASK‑080 | Native ultrawide and other aspect ratios (16:10, phones, 21:9, 32:9): screen-ratio detection, automatic Width/Aspect for the 3D, HUD/UI fix, renderer stretch | 📋 Planned (after 1.0.0) · study done (docs/ULTRAWIDE-NATIVE-STUDY.md), phases P0-P4 |
| TASK‑081 | Release 1.0.0 hardening and installer: release game build without build paths (slice213), launcher without user paths, CPU (SSE4.1) and Vulkan checks before setup and Play, game Vulkan baseline path for older drivers (slice214), WebView2 bootstrapper in the installer, installer asset name `GT4Recomp-Setup-X.Y.Z.exe`, release notes and draft release | ✅ Done (2026-10-21) · owner tested and approved the installer; released as v1.0.0 |
| TASK‑082 | Native variable frame rate: fixed 60 Hz simulation, render at any rate (render skip below 60, extra render-only passes above 60 with the game's own render time t) | 📋 Planned (after 1.0.0) · study done (docs/NATIVE-VARIABLE-FPS-STUDY.md) |
| TASK‑083 | Faster loading and saves: instant disc reads (deeper than PCSX2 Fast CDVD) and loading/save speed-ups in our runtime and game code; gameplay untouched | 📋 Future study (owner, 2026-10-21) |
| TASK‑084 | Frame cost optimisation: cut the emulation-side EE overhead and the night-race readback (target about 6 ms per race frame headless) | 📋 Planned (after 1.0.0) · study done (docs/FRAME-COST-STUDY.md) |
| 🧩 | **gt4-gs 1.0.0, the first stable renderer release** (with TASK‑060/061/062; settings allow-list enforced) | ✅ Published 2026-10-16 (`gt4gs-v1.0.0`, zip sha256 `2e878019...`) |
| 🚀 | **First stable release (1.0.0) on GitHub** (after TASK‑044, TASK‑043 and the owner's end-to-end test) | 📋 Planned |

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

State (2026-10-20, branch task/014-ee-coverage-b, Phase 1 done, not merged): static entry ledger
(scripts/ee_entry_ledger.py, docs/EE-ENTRY-LEDGER.md). Translated entries (case labels): 135,806. New
candidates: 1,122 (high 1,076, medium 39, low 6, rejected 1), 246 unit windows. Decomp names untranslated: 83
of 7,740. The 16 stage A seeds are found by the strict rules (14) and a 40-word window (2). The name table at
decomp e0b70c4d6 equals the committed one. Open: the generated folder has 135,806 labels but its log says
135,093; resolve before Phase 2 (extra-entries library A/B, then producer regeneration after owner approval).
State (2026-10-20, branch task/014-ee-coverage-b, phase 2 candidate built, not merged): 1,105 accepted seeds
(scripts/ee_entries.py). Library profile coverage: 139,534 dispatch ids (+3,728), 325 of 326 units change. Built with
the portable clang-cl 23.1.2; the 8-unit object check is identical to production. CTest 33/33, Python 583 OK,
headless 42/42 identical to slice206. New mode pcsx2-coverage (docs/inputs/slice208-pcsx2.json). Owner play-through
pending; the 135,806 vs 135,093 gap is the codegen boundary entries (EE-ENTRY-LEDGER).
State (2026-10-21): ✅ Done. The owner approved the slice208 candidate after the play-through and asked to close the task (PR into main). `--mode pcsx2` stays slice206 until the default is switched to slice208 in a follow-up (new pin labelled pcsx2, distribution and launcher test folder rebuilt).
Follow-up (2026-10-21, slice209): `--mode pcsx2` runs the slice209 pin (the slice208 coverage library plus the GT4_FULLSCREEN start, for the TASK-019 launcher test). `pcsx2-coverage` and `pcsx2-slice206` stay as history modes.

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
State (2026-10-15, branch task/017-license-crash): FPU rounding lead checked. Donor mul/div/sqrt equal an exact
chop reference; add/sub truncate the smaller operand (one-ulp, magnitude-larger). Not the crash cause by itself
(Hypothesis). The failing record-size words (0xFF80FF80 / 0x80FF80FF at offset 0x14F8, every file) look like a
decode or layout error in the triage parser, not FP results (Hypothesis, to verify with the owner trace).

State (2026-10-16, branch task/017-license-crash, slice 204): halt forensic dump built, not yet run by the owner. `GT4_HALT_DUMP=1` adds `[halt-dump]` lines at the halt (every thread's registers; for a thread whose saved pc is in the record copy loop of `Manager::load` (0x346BB8..0x346EB0), its record base and count, and whether the record window overlaps any thread stack, with the stack of thread 17 at 0x008722C0 as the suspect; the halted thread's stack top). Overlay `scripts/halt_dump_overlay.inc`; capsule `private/slice204-halt-runtime`; build `private/slice204-halt-build`; pins `docs/inputs/slice204-halt-runtime.json` and `slice204-halt-trace.json`. Owner command: `play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode license-halt`. The FSV read trace is on in the same run. Next: decide from the dump whether the copy overruns (Hypothesis) or the record count is wrong (Unknown).

State (2026-10-16, FPU oracle phase 1): cause Confirmed by the owner test (B1, B2, B5 crash; B3, B4 load, as predicted): the demos are decrypted by GT4's FPU-keyed cipher (func_00579E00), which needs bit-exact R5900 mul.s/div.s. `scripts/license_demo_oracle.py` checks the game CRC on all 592 demos; `tests/python/test_license_demo_oracle.py`. No model passes yet (chop/chop 0/592; every model tried 0/592). A CRC-confirmed, model-free beam search recovered exact chains for 528/592 files (52,469 unique div.s steps): the hardware quotient is always chop or chop + 1 ulp; per-step agreement chop 60.9%, nearest 82.8%, radix-2 carry-save SRT candidate 93.1% (best 94.1%). mul.s is a truncated Booth array (hardware vector 1.0 x 0x3FFFFFFF = 0x3FFFFFFE) that equals chop on this data. Next: identify the divider's digit-selection details (Hypothesis: uncorrected radix-2 SRT), then phase 2 (fpu.hpp overlay, ps2game.lib rebuild, new pins).

State (2026-10-17, done): owner approved guard v1 (slice205: B1 no longer crashes, its background movie fades and the demo does not run, the license is playable, as PCSX2 default; B3's demo plays). B5 then crashed through the second loader 0x00110230 (vtbl[57], target 0x003D5F00; session-20261016-235625-870886), which v1 did not see. Guard v2 (capsule `private/slice206-license-guard-runtime`, build slice206) keys on both loaders' call sites; `--mode pcsx2` = slice206 (`docs/inputs/slice206-pcsx2.json`), `pcsx2-slice203` and `license-guard` (slice205) kept. Consumer map: [LICENSE-DEMO-CIPHER](docs/LICENSE-DEMO-CIPHER.md) (memory-card loads check the CRC themselves). PCSX2 with EE clamping Full plays B1's demo but crashes in B3 (owner): another phase coin flip. Follow-up: TASK-063.

State (2026-10-16, round 3, slice205): overflow clamping models (from the public PCSX2 "Full clamping" workaround prose) refuted: every 0x7F7FFFFF profile gives 0 sane layouts and no model passes a CRC; CRC-confirmed chains use exponent-255 states as large normals, as the donor does. Mitigation per owner decision: license demo guard in the runtime's dispatch.cpp (capsule `private/slice205-license-guard-runtime`, ps2game.lib unchanged): when a decrypted demo fails the game CRC and is not the single-Recorder layout, its record count is set to 0xFFFFFFFF so Manager::load copies nothing; one `[license-guard]` line per demo load. Build slice205 on the slice203 recipe (gt4-gs 1.0.1), mode `license-guard`, pins `docs/inputs/slice205-license-guard*.json`. Known issue: those demos and license backgrounds do not play (as in PCSX2). Waiting for the owner license test (B1-B5).

State (2026-10-16, round 2): oracle reports layout sanity per model. The record payload is past the saturation point, so it is all right or all wrong (one key-stream phase); no FPU model gives a sane layout on all files (best 270/592, US best 50/85, every model near a coin flip), so there is no 1.0 candidate among FPU models. Divider fitting: radix-2 SRT carry-save variants plateau at 94.1% of 52,469 confirmed steps; radix-4 -2..2 ruled out; exact rule still Unknown.

### TASK-063 — Bit-exact R5900 FPU div.s/mul.s
Follow-up of TASK-017. Our `ps2/fpu.hpp` is not bit-exact with the R5900 for the cipher's mul.s/div.s chain: 0 of 592
license demos pass the game CRC, so demos in the wrong key-stream phase play no demo (guarded) and real-PS2 card
replays and ghosts fail to load (cleanly). The oracle (`scripts/license_demo_oracle.py`) and 52,469 CRC-confirmed div.s
steps are ready; the divider's digit selection is unknown (best model 94.1% per step). Needs targeted hardware div.s
vectors (or a published design description). Acceptance: 592/592 CRC passes, then an fpu.hpp overlay, a ps2game.lib
rebuild with new pins, and an F8 physics check.

### TASK-064 — Third-party overlays (MSI Afterburner / RivaTuner) with the PCSX2 GS
Goal: the RivaTuner Statistics Server on-screen display (MSI Afterburner) appears in game with the default settings.
Cause (suspected, not confirmed in game): Windows Arbitrary Code Guard (ACG), which gt4-gs enabled by default
(`[GT4GS] ProhibitDynamicCode`, missing key = true). RTSS injects a hook that needs executable memory, and ACG refuses it.
Change: ACG is OFF by default. Row `prohibit_dynamic_code` (gs-settings.toml) is on the Advanced page, default `false`,
written `always`, so gt4-gs.ini carries `ProhibitDynamicCode = false` under `[GT4GS]`; the Advanced page switch turns
ACG back on. The no-JIT guarantee does not depend on ACG: gt4-gs.exe is built JIT-free (GT4GS_ONLY) and `check-nojit.ps1`
verifies every release. Tests: `ProhibitDynamicCodeTests` and the default-ini assertions in `test_gs_settings.py` and
`test_gs_live.py`. README known issue left unchanged until the owner confirms the overlay.
Acceptance: owner sees the RTSS overlay in game with the default settings.
State: ✅ Done (2026-10-17). Owner test: the MSI Afterburner / RivaTuner overlay shows in game with the default settings, confirming Arbitrary Code Guard as the cause. Branch `task/064-overlay-compat` (single writer, from main da1eb95). Python suite
544 tests OK (38 skipped for missing private files). Merged with owner approval (PR #46).

### TASK-077 — Context-aware input mapping (idea)
Different mappings by game state (race vs menus), e.g. Esc = Start (pause) in a race and Triangle in menus. Needs a
study of how to read the game state reliably. After 1.0.0; the owner has more ideas.
State (2026-10-20): open, out of the 1.0.0 scope (owner). The read-only study is in docs/CONTEXT-INPUT-STUDY.md:
a host-side VBlank read of guest RAM (the race_display pattern) can tell race classes apart by vtable and see the pause
object; replay is still unknown. Proposed first step: a logging-only probe build (S1). Depends on TASK-029 reaching
main and on TASK-075.

### TASK-078 — Graphics settings in the game's own menu
Owner idea 2026-10-20, after 1.0.0. Remove GT4's video-output row (480i / 480p / 1080i, the "HiVision" widget on
Options → Screen), keep the game fixed at 480p, and show our settings in the native Options menu: output resolution
(480p to 8K, the renderer's internal resolution), fullscreen, anti-aliasing and maybe more. Read-only study in
docs/INGAME-SETTINGS-STUDY.md. Recommended path: host-side substitution of the option menu data when the game reads
the disc (patch recipes applied to the user's own disc, never shipped game files) plus a host bridge for the script
natives in `dispatch`; a host overlay menu is the fallback. Phases: P0 hold races at 480p for any saved value; P1 hide
the old row; P2 the "Output resolution" row and a settings file the launcher merges; later fullscreen and AA rows and
live apply in gt4-gs. Estimate: 4-5 slices for P0-P2, 6-8 plus a gt4-gs release in total. Owner decisions pending:
provenance of OpenAdhoc-derived script edits, progressive menus (ELF patch) or not.

### TASK-080 — Native ultrawide and other aspect ratios
Owner request 2026-10-21. The launcher reads the monitor resolution. 4:3 and 16:9 use the game's own modes
(TASK-079 forces 16:9). For other ratios the renderer stretches to the screen and the game's projection values
(hidden Detail Monitor Settings: Width, the display width in mm that acts as FOV, default 425; Aspect, default 0.97
for 16:9) are computed from the ratio (owner data points: 3088x1440 → Width 515, Aspect 1.20; 3440x1440 → Width 573,
Aspect 1.34). The HUD, UI and text are corrected too. The SilentWarrior112 PCSX2 patches (no licence) are a
reference only, mapped to decomp functions and redone in our own parameterised way. Progressive menus overlap
TASK-078. Study first, then a phased plan.
State (2026-10-21): study done, `docs/ULTRAWIDE-NATIVE-STUDY.md` (owner asked to keep it). Findings: the patch file
targets our ELF; the hidden-menu record (Width, Aspect...) lives in the saved option object, so the plan writes no
save data. 3D: a dispatch hook on the race camera call after `RaceBase::initMonitor` (return 0x388B38) widens the
projection by s = R / (16/9) (Hor+, Vert+ below 16:9; at 16:9 frames stay byte-identical). The owner's values
scale both Width and Aspect, which is about 23-37% too wide by the game's maths: first owner test. HUD: a hook on
`RaceDisplay::render` for the canvas (320 -/+ 240 R), plus an ELF patch table and re-translation for the fixed HUD
constants. Menus, FMV and photo stay pillarboxed 16:9. gt4-gs needs "Stretch" and a per-scene control message (a
new gt4-gs release). Plan: P0 a decision superseding the 0055 lines; P1 auto 3D aspect plus a launcher row; P1b
gt4-gs stretch; P2 HUD; P3 progressive menus (TASK-078); P4 optional true-FOV mode. After 1.0.0; seven owner
decisions listed in the study.

### TASK-081 — Release 1.0.0 hardening and installer
Release audit 2026-10-21. Branch `task/081-release-1-0-0` (from main 26f83b8). Not PCSX2-tainted.
- Game: CMake `GT4_RELEASE_DISTRIBUTION=ON` drops the build-time shader folder from the executable; shaders load only
  from `<exe folder>\shaders` (the build tree gets its own copy) and a missing folder stops with a clear message.
  slice213 = the slice211 recipe plus the switch; `--mode pcsx2` = slice213 (`docs/inputs/slice213-pcsx2.json`),
  slice211 kept as `pcsx2-slice211`. String scan: no absolute path or user name left.
- Launcher: `launcher/tools/build-release.ps1` builds with `--remap-path-prefix` (Cargo trim-paths is unstable) and
  fails on any user or build path left. The launcher already links the VC runtime statically (Tauri default); it
  imports only the Windows UCRT (`api-ms-win-crt-*`, part of Windows 10/11).
- Requirement checks before setup and Play (`gt4l_core::system_check`, pure and tested; `gt4l_platform::system`
  reads CPUID and probes `vulkan-1.dll`): SSE4.1 with SSE3/SSSE3 (owner decision 2026-10-21; the game executable is
  baseline x86-64 and the PCSX2 GS needs SSE4.1, so the game has no own check), and a GPU with Vulkan 1.2 plus
  `VK_KHR_surface` and `VK_KHR_win32_surface` (the PCSX2 GS renders with Vulkan too, so the check is always on).
- Game Vulkan baseline path (slice214, release blocker): `native/vulkan_extensions.cpp` (pure, CTest
  `native_vulkan_extensions`) enables swapchain maintenance 1 (KHR or EXT, with its instance prerequisites) only when
  the driver has it; otherwise slots wait on the GS timeline and each swapchain image has its own present semaphore.
  `GT4_VULKAN_FORCE_BASELINE=1` forces it (CTest `native_vulkan_pixels_baseline` presents to a hidden window on it).
  slice214 = slice213 plus this change; `--mode pcsx2` = slice214.
- Installer: detects the Evergreen WebView2 Runtime (HKLM WOW6432Node, HKLM, HKCU `pv`) and runs Microsoft's
  bootstrapper (embedded, Authenticode-checked) only when it is missing; default name `GT4Recomp-Setup-X.Y.Z.exe`;
  the uninstaller also removes `SDL3.dll`.
- Launcher controls: the built-in keyboard keys of the `gt4controls` crate now match the TASK-076 layout (they were
  the old Z/X table; a remap wrote them into `controls.toml`). `cargo test --workspace` green again (233 passed).
- Release: README for 1.0.0, `docs/release-notes/1.0.0.md`, draft GitHub release v1.0.0 with the installer and
  `SHA256SUMS`. The owner tests the installer end to end before the merge and the publish.
- Saves in Documents (owner request 2026-10-21): the save library and replays live in `Documents\Gran Turismo 4`
  (`Saves\Active`, `Saves\Save NN`, `Saves\.backup`, `Replays`), found with SHGetKnownFolderPath(FOLDERID_Documents)
  (OneDrive and redirection followed; data-folder fallback `<data>\Gran Turismo 4` with a logged warning;
  `GT4RECOMP_DOCUMENTS_DIR` is a dev and test override). The disc copy, prepared files, gs, config, controls, logs,
  sessions, `runtime\card.ps2` and `library\backups` stay in `%LOCALAPPDATA%\GT4Recomp`. One-time migration
  (`gt4saves::migration`): copy, size and sha256 check, rename the old `library\memcard` and `library\saves` into
  `library.migrated-<UTC stamp>`; both non-empty = no merge, conflict logged; failure = staging copy removed, old
  data untouched; marker `config\library-migration.json`. The guard refuses the player's folder for install,
  update and renderer writes. Saves, Replays and About show the folder with Open folder. The page items now carry
  `id` and `modified` (the real launcher sent neither, so activate and delete could not name an item). Uninstaller
  page: saves and replays in Documents are always kept; the AppData box stays unchecked. `cargo test --workspace`
  250 passed; nav-test 150/150, focus-check pass; scratch exe run migrated a copy of the owner's library.
- Settings feedback (owner, 2026-10-21, round 4): the game is fixed at 480p and 16:9 until the ultrawide update. The
  Input resolution, Widescreen 16:9 and GS Aspect ratio rows are removed; the launcher always sends
  `GT4_FORCE_480P=1` and `GT4_FORCE_WIDESCREEN=1` (no `GT4_RACE_DISPLAY`) and writes `AspectRatio = 16:9`. Old values
  are ignored by a load-time rule (`settings::RETIRED_SETTINGS`; settings stay schema 3, TASK-080 keeps schema 4).
  GS table column `forced` (Python and Rust writers): `aspect` 16:9 and PCSX2's `osd_fps`, `osd_gpu`,
  `osd_resolution` hidden and always written off; the other stock OSD rows hidden; allow-list unchanged. Advanced is a
  section at the bottom of Graphics (no Advanced tab; D-pad reaches it, B back, Y Reset). `cargo test --workspace`
  252 passed; Python 598 OK; nav-test 150/150; focus-check 93/93 pass. Installer rebuilt (sha256 8cf61ef4...), draft
  assets replaced.
State (2026-10-21): ✅ Done. The owner tested the installer end to end and approved it; merged into main and published as release v1.0.0.

### TASK-082 — Native variable frame rate
Owner request 2026-10-21: 30, 45, 120, 144 or unlocked FPS with game speed unchanged, real frames from the game rather
than image interpolation (TASK-045 stays the fallback and runs its race oracle in parallel). Study:
docs/NATIVE-VARIABLE-FPS-STUDY.md. GT4's main loop (`func_001016E8`) updates every object with dt 1/60, renders every
object, then flips; render already takes a time `t` (`render_world(buf, t)`), and the race camera lerps two poses with
it. Race time counts simulation steps, and replays and ghosts are frame-indexed inputs.
Owner decisions (2026-10-21, the study's recommendations accepted):
- N1: the simulation (physics, AI, timers, the 60 Hz guest VBlank) stays fixed at 60 Hz; only rendering changes rate.
  N2 (BLACK-style variable dt) is rejected for vehicle physics and allowed only for cosmetic effects.
- First P1, render skip below 60: game speed stays 100% down to 20 FPS with an identical simulation. Then extra
  render-only passes above 60 (P2 camera-only at 120 Hz in Time Trial; kill gate: a pass over 7.3 ms), P3 cars and
  scene, P4 present pacing (cap, VRR, unlocked), P5 a combined scheduler.
- Extra frames interpolate (+1 frame) by default; latency is cut elsewhere later. Extrapolation stays an option.
- Allowed: the main-loop ELF patch with re-translation (superseding decision 0055 for this feature), a GS stream
  PRESENT record and the gt4-gs change. Menus stay at 60 Hz.
Cost today: race critical path 10-19 ms of 16.7; one extra render pass 7-15 ms, so 120/144 needs cheaper passes.

### TASK-083 — Faster loading and saves (future study)
Owner request 2026-10-21. Loading and saves run at PS2 speed today: the CDVD HLE models every read as 0.4 ms plus
0.25 ms per 2 KiB sector of guest time (about 8 MB/s, donor `iop/cdvd.cpp` `read_cycles`), and the guest clock stays
paced at 60 VBlanks per second during loading screens and saves. The owner reports that PCSX2's Fast CDVD breaks
nothing in GT4 but does not gain much, and asks for a deeper, deliberate speed-up, game code changes included.
Study first: measure where boot, race-entry and save time goes (disc wait, decompression, fixed waits in game code,
card writes, VBlank pacing). Candidate levers: instant disc reads (and a faster byte copy than `write8` per byte);
an unthrottled guest clock while a loading or save screen is active (the TASK-077 study found the loading object),
with audio muted or kept at the right pace; hooks or ELF patches that shorten fixed waits and per-frame save
chunking. Never during races or replays; physics, lap times and replays untouched. After 1.0.0.

### TASK-084 — Frame cost optimisation
Owner request 2026-10-21. Study: docs/FRAME-COST-STUDY.md (slice211, headless, timed only with no builds running).
In the day race the EE thread takes 8.2 ms per frame, of which the translated game code is 2.9 ms; the rest is VIF1
job posting (about 5,200 MTVU jobs per frame, one doorbell), the journal GS front end, stream writes, IOP, run loop and
waits for the VU1 worker. The night-race lens-flare readback (2 KiB per frame) adds 3.3 ms of EE wait plus a 0.9-1.6 ms
GS round trip and serialises EE and VU1. Ranked plan: (1) defer the flare readback one frame, ordered through the
journal, after checking nothing in gameplay reads it (-4 to -5 ms night); (2) one MTVU job per VIF1 DMA chain and
wake-ups only when waiting (-1 to -2 ms); (3) no EE wait for the VU1 worker at every VBlank in pcsx2 mode (-1 to -3 ms);
(4) drop the second GIF walk and a copy (-0.4 to -0.8 ms); then compiler flags, run-loop fast paths, VU1 speed. Add the
missing F8 instrumentation first (pacing-sleep scope, fixed summary formula, per-readback records). Each step: its
own slice, headless frame identity, CTest and Python, owner F8 check. Feeds TASK-082 (a render pass must fit 7.3 ms for
120 Hz) and weaker PCs. After 1.0.0.

### TASK-018 — Racing wheels
High-level emulation at the PDIUSB boundary with SDL3 wheels and force feedback
(docs/MENUS-AND-WHEEL.md). Builds on the TASK-075 SDL3 input layer.

### TASK-075 — Input layer on SDL3
Owner decision 2026-10-21. Replace the game's XInput-only pad code (native/host_io.cpp) with one input layer on
SDL3 (zlib licence, fine in the MIT game exe; also the Linux and Android path, TASK-074 and TASK-022). SDL3 opens
only its gamepad/joystick subsystems; the game window stays ours. Every pad SDL3 knows (Xbox/XInput, DualShock 4,
DualSense, Switch Pro, generic) maps to the DS2 layout with analog L2/R2 pressure and stick dead zones kept. The game
reads the launcher's controls.toml (the branch controls code, GT4_CONTROLS_FILE), so remapping in the launcher
applies in game. The launcher's Controls page reads pads through the same SDL3 database (Rust sdl3 crate) instead of
the WebView Gamepad API, so the launcher shows exactly what the game sees. The input layer runs in the game process
(no launcher bridge: the launcher is hidden in the tray during play, and latency and force feedback need the game
side). Racing wheels (TASK-018) build on this layer. Acceptance: Xbox and DS4/DualSense pads work in game with no
third-party tools; launcher remaps apply in game; headless frame dumps unchanged; owner test.

State: ✅ Done (2026-10-21). Game side slice210 (SDL3 3.4.18 pinned in `docs/inputs/slice210-sdl3.json`, `native/pad_mapping.*`, `GT4_INPUT=sdl` default, `xinput` kept as a fallback), launcher side on the Controls page (SDL3 through the Rust `sdl3` crate). Owner tested pads and remapping, and approved.

### TASK-076 — Modern fixed keyboard layout
Owner design 2026-10-21, hard-coded (not remappable for now), several keys per DS2 button: D-pad left/right = A/D and
arrows; D-pad up/down = arrows; D-pad up (ghost) also E; Cross (accelerate, confirm) = W and Enter; Square (brake) = S;
Circle (handbrake) = Space; Triangle (back) = Backspace and Esc; nitro = Shift; gear up / gear down = their own keys
near WASD; look back = Q; camera = C or Tab. Map each action to the DS2 button GT4 really uses (check the decomp and
the manual: nitro, gears, rear view, camera). The launcher Controls page shows the layout read-only in Keyboard view.
State: ✅ Done (2026-10-21): the hard-coded table (`native/controls.cpp`, `docs/CONTROLS.md` keyboard-default, `docs/INPUT-SDL3.md`). Nitro = R1, gears = R2/L2, rear view = L1, camera = Select (ludo.guide); Start = P (owner). Owner tested.

### TASK-079 — Force 16:9 at boot
Owner request 2026-10-20. A fresh save starts the game in 4:3. When the player picks 16:9 (Wide) in GT4's own Options and
saves, it stays 16:9. Force 16:9 at boot for everyone, the same way GT4_FORCE_480P holds 480p (TASK-009, slice164).
Environment `GT4_FORCE_WIDESCREEN`: 1 forces 16:9, 0 leaves the game's own setting, unset means 1 (the launcher passes
its setting). Any other value refuses the start. The launcher setting "Widescreen 16:9" (Graphics page, default on) sends
it. Forced in guest RAM only; the owner's card file is never written by the host.

Guest chain (decompiled source at identical addresses, gt4-decomp e0b70c4d6; Confirmed unless marked):
- owner pointer `0x622F4C` (the game-data object); option object `owner + 0x38CB0` (MOption).
- wide flag `option + 0x14` = `owner + 0x38CC4`: 0 = 4:3, 1 = 16:9. Set by `func_00437110` (the script setter
  `MOption.wide_mode`, `MOption__get_wide_mode`), read by `func_001AB4C0` (race HUD rectangle, 0.75 scale of the
  4:3 squeeze) and by the photo mode code. Saved with the option object (High confidence: the same object holds the
  garage and calendar that the save path writes).
- aspect record `option + 0x24`, 11 words, written by `func_00439108` from one of four presets in guest data:
  `0x622E90` (4:3), `0x622EC0` (wide), `0x622EF0` (4:3 NTSC), `0x622F20` (wide NTSC); index = wide + 2 * (video == 1).
  `func_004390D0` says "wide" when width / height >= 1.5 (f32) at `record + 0xC` and `+ 0x10`.
- presets are filled at start by `func_00439B30` from `0x6A6188` (4:3, 275 x 205) and `0x6A61B8` (wide, 425 x 240);
  the NTSC presets hold the same two records (checked in the pinned ELF, Confirmed).
- video system `option + 0x1388` (1 = NTSC).
- published word `0x6184F0` (D_006184F0) = `func_004390D0(option + 0x24)`, written by `MOption.apply` (`func_004365B8`).
  It is the value the renderers read: RaceDisplay, RaceDisplayUtil::wideOffset, the 0.75 scale in `func_00393790`
  and `func_00102B30`, the minimap offset.

Native (`native/race_widescreen.*`, pure, fixture `native_race_widescreen`): at each guest VBlank, when the owner chain
is valid and the wide preset is wide, it sets the flag to 1, copies the wide preset into the record when the record is
not wide (what `func_004391C8` does), and publishes `0x6184F0` from the record. A preset that is not wide is refused
with no write (logged once).

Limits: the game's menu can show 4:3 for one frame before the next VBlank forces it back (same limit as 480p); a save
made with the force on stores 16:9. The presentation of the 4:3 and 16:9 layouts of the game is otherwise unchanged.

Checks (2026-10-21, slice211): see `docs/BUILD.md` slice 211 and `docs/journal/2026-10-21.md`.
State: ✅ Done (2026-10-21). slice211 (`docs/inputs/slice211-pcsx2.json`, `--mode pcsx2`) and the launcher setting
"Widescreen 16:9" (default on). Owner tested: a blank card boots in 16:9 and 480p, and the 100% save loads. A "Load
failed!" seen during the test was the launcher card bug (save folder "." entry), fixed in TASK-019; the force was
ruled out headless (slice210 and slice211 with the force off failed the same way on the bad card).

### TASK-019 — Launcher
State (2026-10-21, TASK-075 launcher side, branch task/019-launcher): the launcher reads pads through SDL3 3.4.18 (`gt4l-platform` `sdl_pads`, the same layer the game uses), in a Rust pad service that emits `pad-status`, `pad-live` (about 60 Hz) and `pad-nav`; the Controls page and nav.js use these events, the Web Gamepad API is only in the dev mock. The Keyboard view shows the fixed modern layout (TASK-076), read only. Checks: `cargo test --workspace` green, nav-test 149 of 149, focus-check 90 of 90 (headless Edge), real exe over WebView2 with 0 ACL denials. Not done: the game side (task/075-sdl3-input) and the owner's pad test; the launcher's controls file still uses XInput-named sources (the game reads them until its SDL layer lands).
State (2026-10-21, slice209): native game reads `GT4_FULLSCREEN` (strict 0/1; the launcher default is on). Game executable slice209 (build `private/slice209-build`, sha256 a75c58fa..., 97,105,408 bytes), mode `pcsx2` (docs/inputs/slice209-pcsx2.json): the slice208 coverage library and the fullscreen start only. CTest 33 of 33, Python 589 OK (23 skipped), headless 42 of 42 frame dumps identical to slice208. The owner launcher test folder was rebuilt (`private/owner-test-launcher/game`, slice209; the slice206 folder is `game-206-old`). Owner test of the fullscreen start pending; the branch is not in a PR.
State (2026-10-20): in progress on `task/019-launcher`. Main is merged (c4c7304) and the UI branch is merged (4292f7f).
The settings table is refreshed from main (TASK-049 page/curation, TASK-064 prohibit_dynamic_code always written), and the
Rust gt4-gs.ini writer equals scripts/gs_settings.py (golden fixtures regenerated by the Python writer). Checks: cargo
test --workspace 117 passed, 0 failed, 1 ignored; Python unittest 555 OK, 38 skipped. The logo has a white-text variant
(ui/assets/logo-gt4-white.png, launcher/tools/make_white_logo.py) on the setup, home and settings pages. The home page
has a background slideshow (12 s per image, 1.5 s crossfade, slow pan, reduced-motion fallback) with placeholder images
(ui/assets/backgrounds, see its README; the owner is making new images). The version is 1.0.0 everywhere it is declared
(Cargo workspace, tauri.conf.json, installer NSIS, home label, About). UI pass 3 (2026-10-20, owner requirements: touch, controller, Play): every control reachable by touch
(48 px targets, tap help tips, slider drag, swipe between Settings tabs, scrolling lists), every page
reachable by controller (X Save on Settings, Y secondary action, right-stick scroll, footer hints per
page), and the glass Play button (idle sheen, solid white when focused). Branch task/019-launcher, not yet
in a PR. Still open: the owner test of setup, saves, controls and the new touch and pad behaviour;
greying the presets above the device limit (max_upscale); the installer test and the live update test (TASK-043).
State (2026-10-20, owner requirements, one writer): SemVer streams (GT4Recomp `vX.Y.Z`, gt4-gs `gt4gs-vX.Y.Z`, minimum
gt4-gs 1.0.1, asset `gt4-gs-X.Y.Z-windows-x64.zip`), `version.json` (installer writes it, launcher records gt4-gs),
first gt4-gs install during setup and at launch, a launch-time update check (never blocks Play; popup with Update,
Not now, Skip this version; updates never automatic), data-folder guard, schema migrations with backups (settings,
controls, version.json), tray (hidden while the game runs, back on Home when it ends, close asks while it runs), and
the Graphics table of the UI mock. Docs: `docs/RELEASES.md`, `docs/launcher/MIGRATIONS.md`. Checks: `cargo test
--workspace` green. Not done here: a real installer build, a live GitHub check and the owner's end-to-end test.
State (2026-10-20, flow audit, `docs/launcher/FLOW-AUDIT.md`): the whole flow mapped and tested end to end with the
real discs in a sandbox (45 steps: 23 PASS, 17 FIXED, 0 FAIL, 5 NEEDS-OWNER). Disc rule: SCUS-97328 whose CORE.GT4
equals the pin is accepted (v2.00 must also match the full image pin); Spec II and other discs get "This disc is not
supported yet". Setup now copies the image to `%LOCALAPPDATA%\GT4Recomp\disc\SCUS-97328.iso` (free-space check,
verify then rename, schema 2 receipt), so the picked file is needed only once; a missing copy leads back to setup.
Setup draws copy, check, prepare and gt4-gs download progress; the real gt4-gs 1.0.1 zip (one wrapping folder) now
installs. The real GT4Recomp.exe opened on Setup, Home and "Game files missing" in the sandbox. Checks: cargo test
--workspace 186 passed, 3 ignored; nav-test 55/55. Owner: file picker, Play, tray, update popup, pad, touch.
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
**Resumed on 2026-10-21 (owner decision)** on branch `task/027-coregs2` (worktree
`C:\Antigravity\gt4-wt-027-coregs2`): `main` at release 1.0.0 (slice214) merged with
`task/027-hw-renderer` and the `task/027-cpuref` docs checkpoint. paraLLEl-GS code, pins and launcher modes
were left out (decision 0070); `GT4_RENDERER=hw` now runs through main's GS stream seam into the in-process
HWR backend (`GT4_HW_GS=ON` builds only; black output). `task/027-m4-gpuref` (e31fcc4, GPU write-back WIP)
is not merged. Implementation continues with the external agent **GPT 6.1 Sol** as the only writer of the
branch, clean room from `docs/hw-renderer-spec` (brief `AGENT-BRIEF-COREGS2.md`, progress in
`docs/hw-renderer-spec/PROGRESS.md` and ai-memory `decisions/coregs2-implementation`). No pins until a
candidate is offered to the owner. Warm-up complete (2026-10-21): shader TEXA/AEM follows 07.5 / IN-07; slice217 builds and all three GPU exactness fixtures pass. Native v2 replay is implemented (slice219: 69/69 CPU-library tests; 1,101/1,101 identical menu PPMs across legacy, converted v2 and fresh v2). VRAM diagnostic added (local221:75/75 tests, deterministic1,096-image replay; original scanout unchanged). Correct pairing resolves the IN-315 comparison offset; the separate A+D kick fix (IN-326, local222: 76/76 tests) closes IN-316: the garage defect was the dropped A+D kicks, exactly frames 811..916 change, and 11/11 aligned samples now sit inside the chapter 14.4 2D thresholds (garage 0.2051%, 45.992 dB) while 990/1,096 VRAM and 995/1,101 scanout images stay unchanged. The M4 GPU write-back path of `task/027-m4-gpuref` is then ported, built and wired into the backend (local223, IN-329): eleven fixtures pass (03.3 coverage vectors and seven GPU/CPU equivalence cases), the default renderer stays 42/42 vs slice214 and `GT4_RENDERER=hw` runs 600 headless frames with 0 faults, still without frame dumps. IN-328 fixed the CPU reference's constant-attribute truncation. M5 then wired the whole pixel pipeline into the same draw stage (blend unit over 81 modes, COLCLAMP, PABE, alpha test, DATE, FBA, FBMASK, dither; five new raster fixtures at zero mismatches); `GT4_RENDERER=hw` stays clean at about 62 ms per headless frame. M6 then started with the presentation interfaces (IN-331): a present scheduler decoupled from the VBlank (hold/re-present, newest-wins backlog, late-input hook), auxiliary frames that restore guest memory and refuse readbacks, and per-pass CPU timers; default 42/42 parity unchanged. HWR frame dumps then landed (IN-332): the 4,200-frame headless run writes the same 42-frame schedule with 0 faults and measures 42/42 frames outside the 2D gate, with two visible draw-loss defects (boot text line, garage list). Next: the HWR replay tool to diagnose the GPU draw path from a capture, then the windowed scheduler wiring, the first owner look and the S-01/S-02 parity replay. History of the pause follows.

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

**Level 1 delivered (2026-10-18, PR #41, not the whole task).** Read-only name table from gt4-decomp `0b54ed5c`:
`docs/data/ee-names.csv` (11,481 rows: address, name, kind, origin, quality), built by `scripts/ee_symbols.py`; see
[docs/EE-NAMES.md](docs/EE-NAMES.md). No generated EE file, pin or runtime code changed. Still open: labelling `unit_XXXX.cpp`
needs the new producer contract described under "Level 1" in [docs/DECOMP-INTEGRATION.md](docs/DECOMP-INTEGRATION.md).

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
State (2026-10-16, slice193): built; owner windowed test pending, no PR.
- Fork `gt4gs/live` (commits `0b51d4c3c`, `acf4f986e`; tag `gt4gs-v0.2.0-live`): `pcsx2/GSStreamReplayer`, frontend
  `pcsx2-gt4gs` (`gt4-gs.exe`: `--ipc/--parent/--config`, `--replay-gscap`, GSRunner dump mode), child window,
  vendored protocol files checked by SHA-256. Launcher mode is `--mode pcsx2` (not `pcsx2-gs`), with `--gs-scale N`.
- Main: GS process SHA-256 pin (`GT4_GS_PROCESS_SHA256`), `GT4_GS_CONFIG`, `WS_CLIPCHILDREN`, crash message box;
  minimal `gt4-gs.ini` from the launcher; slice193 pins; `tools/gs_live_check`.
- Headless frame dumps come from `gt4-gs.exe` itself (`[GT4GS] DumpDir`), so the game port is unchanged; CONTROL
  snapshot is implemented in `gt4-gs.exe` and used by the window check.
- Checks: CTest 29/29, Python 335 OK; donor 42/42; live 41/41 PNGs = offline replay, 300/700 = PoC; GS kill halts
  the game; hidden-window attach/resize/snapshot/shutdown. The PoC vertical-scale question is the aspect ratio
  (Auto 4:3 gives 480 rows). Not verified: windowed image, races, 480i, readbacks, GPU time.

### TASK-037 — Readbacks and night race
Repos: both, branches `task/037-038-gs-window` (main, shared with TASK-038), `gt4gs/window`. Opus, 2-3 sessions.
Goal: READ_FIFO round trip through `MTGS::InitAndReadFIFO`; `HWDownloadMode` and readback spin options
exposed; F8 counts and wait time; Unsynchronized mode served without draining the ring (if the owner allows).
Acceptance: CTest/fixture with a scripted stream (draw, TRXDIR=1, read) compared with the donor bytes;
headless frames unchanged; owner night race at 60 FPS with `gs_ipc_readback_wait` reported; lens flare and
night lighting correct versus CoreGS 1.1.
Risks: GPU sync cost at high upscale.
Parallel: with 038/039.
State (2026-10-16, slice194): built; owner night-race test pending, no PR.
- Cause of the TASK-036 night-race late frames (213/7,467): one readback per frame (1.07 ms each) removes the GS
  buffering, so GS-thread spikes (pipeline compiles at race start) land in the game frame; the shader cache was in
  `Documents\PCSX2`; the MTVU `gs_privileged` wait (3.4 ms/frame) leaves about 3.7 ms of headroom.
- `GT4_GS_READBACK=accurate|unsynchronized` (`--gs-readback`), F8 readback histograms, `gt4-gs.exe` readback and
  acquire timing, own cache/data folders, deferred swap chain acquire (A/B `GT4_GS_EAGER_ACQUIRE=1`).
- Checks: readback fixture exact in accurate mode (drawn sprite and uploaded bytes); headless frames unchanged;
  cold vs warm cache replay (max GS frame 84 ms -> 6 ms).
- Owner night race (slice194): accurate 59.97 FPS, 69 late frames (was 213), readback mean 0.78 ms; unsynchronized
  60.00 FPS, 1 late frame, but headlight flares missing. Owner chose accurate as the default; unsynchronized stays
  an option ("faster, may drop effects").

### TASK-038 — Window, fullscreen, focus, input, lifecycle
Repos: both, branches `task/037-038-gs-window` (main, shared with TASK-037), `gt4gs/window`. Opus, 3-4 sessions.
Goal: child window hardening (own pump thread, `HTTRANSPARENT`, `MA_NOACTIVATE`), Alt+Enter/F11 borderless,
resize, minimize, DPI, multi-monitor, focus loss; GS hotkeys forwarded (screenshot, OSD toggle); fallback
option 2 (GS-owned window, keyboard forwarding) behind a switch.
Acceptance: Python/CTest for the message contract; a scripted soak (resize and fullscreen toggles, 1,000
cycles) with no hang; a GS-thread stall test does not freeze our window beyond the stall; RTSS overlay
visible; owner checklist (fullscreen, alt-tab, minimize, controller and keyboard).
Risks: attached input queues; DWM latency in child windows.
Parallel: with 037/039.
State (2026-10-16, slice194): built; owner checklist pending, no PR.
- Input queues detached after SetParent; click on the picture brings the game forward; minimized (0x0) resizes
  ignored; WM_CLOSE in pcsx2 mode halts before the window is destroyed; GS death: 180 grace VSYNCs (card flush),
  restart offer with CoreGS 1.1 (launcher relaunches with `GT4_RENDERER=donor`); handshake timeout message.
- Checks: stall fixture (3 s GS window stall, parent answers within 17 ms), soak 3,000 resizes, aspect check (same
  rectangles as TASK-009), CTest handshake-timeout / game-killed / grace scenarios.
- Not done: own pump thread for the child (the main thread only pumps), GS hotkeys, fallback option 2, DPI and
  multi-monitor checks, RTSS.
- slice195 (2026-10-16): owner found that a click in the picture deactivated the game window (detached queues make
  the GS thread the foreground thread). gt4-gs 0.3.1 gives the foreground back after every click; the game keeps the
  keyboard focus on activation. Visible click fixture passes (0.3.0 fails). Owner click/focus check pending.

### TASK-039 — Settings table and `gt4-gs.ini`
Follow-up (2026-10-16, `fix/duplicate-gs-json-settings`): reject duplicate JSON setting ids consistently
with CLI assignments. Module validation: 58 tests pass; native/gameplay validation not performed.
Repos: main `task/039-gs-settings`, fork `gt4gs/config`. Haiku, 2-3 sessions.
Goal: `docs/launcher/gs-settings.toml` (GSDX-PLAN D.1), generator to `gt4-gs.ini` (dev script now, launcher
later), allow-list validation in `gt4-gs.exe`, folders under the user data directory, `CONTROL
reload_config`.
Acceptance: Python tests (table schema, generated INI golden file, refusal of unknown keys and out-of-range
values); `gt4-gs.exe` reports effective settings in READY; owner checks 1x/3x/6x, FXAA, CAS, dithering,
deinterlace in menus.
Parallel: with 037/038.
State: ✅ Done (main-repo part); fork allow-list enforcement pending (TASK-037/038 agent). All rows resolved. Branch `task/039-gs-settings` (2026-10-16). Details: [GS-SETTINGS](docs/GS-SETTINGS.md).

### TASK-040 — Texture dumping and replacement (folded into TASK-050, release 1.1.0)
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
State (2026-10-16, slice198, branch `task/041-gs-perf`; fork unchanged, `gt4gs-v0.3.2`): built and checked headless;
owner night-race A/B pending, no PR.
- Headless race schedule added (Arcade, El Capitan, 5 AI, 16,000 frames): readbacks with the night-race pattern.
- Night-race `gs_privileged` wait (3.48 ms/frame): 8 privileged writes per frame = 5 at the VBlank (PMODE, SMODE2,
  DISPFB2, DISPLAY2, BGCOLOR) + CSR and BUSDIR x2 per readback; the wait sits before the CSR write (the EE waits for
  the frame's VU1 work right before the flare readback). `GT4_MTVU_PRIV_ASYNC` (default 1) queues privileged writes
  in the MTVU journal: stream bytes identical (menus captures, race stream hash with 588 queued writes), frames
  identical; the gs_privileged wait becomes 0 but moves to the download sync of the readback (no net gain expected
  in the night race).
- Readback tail: long waits are pipeline compiles (cold cache: 12 of 294 at 8 ms or more, max 58 ms; warm: none, max
  2.7 ms); a GS-side spin does not help (fork unchanged); fix is TASK-046.
- Stream: 322 GS wake-ups per frame -> 11-14 with `GT4_GS_WAKE_BYTES` (default 256 KiB); `gs_ipc_write` about -30%
  per MB; one copy less per worker GIF record in the MTVU journal.
- F8 `[gs-ipc]`: privileged writes and MTVU waits by register, GS wake-ups, GS CPU thread per interval, one-line EE
  critical path summary against the 16.68 ms budget.
- Menus, same build: EE CPU donor 7.6 vs pcsx2 7.2 ms/frame. CTest 29/29, Python 416 OK, donor 42/42, pcsx2 41/41.
  Details: [GS-LIVE](docs/GS-LIVE.md) section 9.

### TASK-042 — JIT-free `gt4-gs.exe`
Repos: both, branches `gt4gs/nojit` (fork, from `gt4gs/main` `660017bb1`) and `task/042-gs-nojit` (main, from `a1a06a4`).
Opus, 1 session.
Goal: `GT4GS_ONLY` build without EE/IOP/VU recompilers, without RWX code mapping, with the C draw-scanline
path or CPU sprite/CLUT render refused.
Acceptance: binary scan finds no recompiler symbols; a VirtualAlloc/VirtualProtect audit shows no executable
private memory; PoC and live frames unchanged; GSRunner tests still pass.
Parallel: with 037-041 (fork-only).
State (2026-10-16, slice197): built and published (`gt4gs-v0.4.0`, zip sha256 `5c92ad00...`); owner windowed check
pending, no PR.
- Fork option `GT4GS_ONLY`: no EE/IOP/VU/VIF recompilers, no x86 emitter, no GS software JIT (C rasterizer for CPU
  sprite/CLUT render and the software renderer; no setting refused), no code area, executable protections refused;
  Windows Arbitrary Code Guard on at start (works with the AMD Vulkan driver); executable-memory audit in the log.
- Static check `tools/gs_nojit_check/check-nojit.ps1` (fork `gt4gs/check-nojit.ps1`, run by the release script): 0.4.0
  passes, 0.3.2 shows 22 markers; `tests/python/test_gs_nojit.py` repeats it on the pinned package.
- Offline menus replays (slice191 and slice196 streams, 1x and 3x): frames byte-identical to 0.3.2; also with CPU
  sprite and CLUT render on. Live headless pcsx2 1x 41/41 = slice196; donor 42/42. GS thread time unchanged within
  noise. `--mode pcsx2` = slice197 (same game runtime as slice196), `--mode pcsx2-slice196` kept.
  Details [GS-LIVE](docs/GS-LIVE.md) section 9.

### TASK-043 — Launcher integration
Repo: main, branch `task/019-launcher` (when the launcher resumes). Haiku, 2-3 sessions.
State (2026-10-20): in progress on `task/019-launcher`. Code: `launcher/crates/gt4l-core/src/{releases,update,version,versions,guard,migrate,gs_release}.rs`
and `launcher/crates/gt4l-tauri/src/{updates,tray}.rs`. Stable releases only (drafts and prereleases ignored); the
renderer minimum is gt4-gs 1.0.1; the install is verified against `SHA256SUMS` and staged as before. Checks: fixtures
for both repositories (no network), SemVer, update decision, version.json, migrations, data-path guard, tray state machine.
Still open: the owner's live test against GitHub, and a real NSIS build (the installer was edited, not built here).
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
Note (2026-10-16): `gt4gs-v1.0.0` is published (stable) and pinned in slice202. Known issue for the release notes: gt4-gs 1.0.0 writes under `Documents\PCSX2`: `VMManager::Internal::LoadStartupSettings()` runs before `[GT4GS] DataRoot` is applied, so it rewrites `Documents\PCSX2\logs\emulog.txt` every session (Confirmed: before/after snapshot of the folder) and creates the PCSX2 user folders there (bios, memcards, logs, ...) on a PC that has none (High confidence, `EmuFolders::EnsureFoldersExist`). The log line "MemoryCards Directory: ...Documents\PCSX2\memcards" is that early pass; the session itself uses DataRoot. Fix on fork branch `gt4gs/datadir` (commit `b30870fe8`, version 1.0.1, NOT released): the call moves after the stream folders; checked: 0 changes under `Documents\PCSX2`, emulog in DataRoot\logs.
Decided (2026-10-17): release 1.0.0 ships gt4-gs 1.0.1 (see Result below).
Note (2026-10-16, TASK-044): gt4-gs 1.0.1 is published (stable, tag `gt4gs-v1.0.1`, fork PR #7 merged) and pinned in
slice203 (`--mode pcsx2`). The `Documents\PCSX2` known issue is fixed in 1.0.1: no changes in the `Documents\PCSX2` snapshot for a
live run or an offline replay (slice203, TASK-044). The clean-profile check was not run (see BUILD.md slice 203).
Result (2026-10-17, docs only; no build, no game run): **decision: release 1.0.0 ships gt4-gs 1.0.1** (`gt4gs-v1.0.1`,
pinned in slice203 and slice206, decision 0070). The gt4-gs 1.0.0 release stays as history. The fork release notes of
`gt4gs-v1.0.1` were edited (upstream base PCSX2 v2.8.2, commit `fd9d310cc`; patch list; GPL-3.0 and source; SHA256SUMS);
assets unchanged. README renderer section, known issues and legal notes updated; `native/THIRD_PARTY.md` rewritten;
RELEASE-1.0.0-PLAN renderer table and W8 updated. Release order: this task, then the launcher and installer (TASK-019/043),
then the owner's end-to-end test, then the v1.0.0 tag and GitHub Release. Mods (TASK-048, 050-057) are release 1.1.0 and
appear only in this list.

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

### TASK-058 — Crash-safe memory card in the real build
Repo: worktree `C:\Antigravity\gt4-wt-058-harden`, branch `task/058-059-release-hardening` (from main `3b5334d`). Release 1.0.0 hardening, game side. One writer.
Goal: the runtime writes the card through the atomic writer of TASK-025 Part A (temporary file, flush, rename), keeps a failed write dirty and retries, and flushes a pending save on exit, including the pcsx2-mode GS-process loss path after its grace VSYNCs.
Result (slice200, docs/BUILD.md): TASK-025 Part A was on `task/025-save-manager`, not on main, and is imported; memcard capsule `private/slice200-memcard-runtime` (derived `iop/sio2.cpp`, pinned by `runtime.json`); CMake `GT4_MEMCARD_RUNTIME_SOURCE`; `flush_cards_now()` after `emulate()`. CTest 29/29 (`native_memcard_store`), Python kill test (25 kills of a copy: old or new image, never torn). The headless menus schedule never saves: the card of the donor and pcsx2 runs equals the seed, so the byte comparison of a saved card is not available headless; the in-game save check is the owner's test.

### TASK-059 — Distribution executable
Repo: same worktree and branch as TASK-058. Haiku, 1 session.
Goal: an install folder that runs with no repository path: `<exe folder>\shaders` when present (else the build-time folder), the gt4-gs package at `<exe folder>\gs\gt4-gs.exe` or `GT4_GS_PROCESS`, no build path embedded except the development fallback, a `--portable-check` self-test, and `scripts/make_distribution.py` for the folder (no gt4-gs package, no game data; GPL kept separate, decision 0070).
Result (slice200, docs/DISTRIBUTION.md): the shader change of `task/019-launcher` ported (its printf string repaired); `native/install_paths.*`; port `private/slice200-port` with `--portable-check`; `make_distribution.py` wrote `private/slice200-dist` (16 files, 93 MB); the exe holds one build path (the build-time shader fallback). Smoke from that folder, headless: donor 42/42 frames identical to slice157; pcsx2 41/41 identical to slice199 (gt4-gs 0.4.0 via `GT4_GS_PROCESS`, ACG on). Tests: `test_portable_install.py`, `test_make_distribution.py`.
Open: the release build should drop the build-time shader fallback; `--mode main` is not rebuilt (its build is not shared); the owner's disc is not copied into the worktree.

### TASK-047 — PCSX2 GS 480i deinterlace
Repos: both, branches `task/047-gs-deinterlace` (main, from `6806f78`) and `gt4gs/deinterlace` (fork). Opus, 1 session.
Goal: the 480i menus look like real PCSX2 with deinterlace Automatic (owner report 2026-10-16: soft and flickering
in gt4-gs; races at 480p fine).
Acceptance: root cause with evidence; headless before/after menu frames with a comb and flicker metric; progressive
output unchanged; CTest, Python, donor 42/42; published `gt4gs-v0.3.2` pinned; owner check of the menus.
State (2026-10-16, slice196): built; owner check pending, no PR.
- Cause (Confirmed): the stream never carried SYNCV (the BIOS writes it; the donor's SetGsCrt HLE does not), so
  PCSX2's GS treated the 480i output as not interlaced and showed each half-height field line-doubled, bouncing one
  line per frame. With SYNCV alone the fields were woven in the wrong order: gt4-gs used the previous VSYNC's field,
  but the donor flips FIELD before GT4's VBLANK_S handler (`0x004AB430`) reads it, one field earlier than hardware.
- Fix: SetGsCrt posts SYNCV for NTSC/PAL; protocol 1.1 defines VSYNC `field` as the display field (same values);
  gt4-gs 0.3.2 presents with the record's own field. `--mode pcsx2` = slice196, `--mode pcsx2-slice195` kept.
- Static Garage menu, frames 1000-1199: slice195 comb 0.50, flicker 3.23; SYNCV with the old field comb 1.00
  (combing); slice196 comb 0.57, flicker 0. Progressive control identical. Samples:
  `private/slice196-deinterlace-samples`; details [GS-LIVE](docs/GS-LIVE.md) section 8.

### TASK-048 — Mod manager in the launcher (umbrella)
Plan: [MOD-MANAGER-PLAN](docs/launcher/MOD-MANAGER-PLAN.md).
Goal: a Mods page where users install, enable, order and remove mods; texture packs for the PCSX2 GS first.
Texture replacement is not a graphics setting: the launcher writes the GS keys from the enabled mods.
Children: TASK-050..057. Legal: users bring their own packs; we never host or download packs.
State: plan written 2026-10-16; owner answered all ten questions the same day (see below); release 1.1.0.

### TASK-050 — Texture replacement contract and headless round trip
Repos: main, branch `task/050-texture-contract` (from `main`); fork `gt4gs/textures` only if a summary log line is
needed. Haiku, 1-2 sessions. PCSX2-tainted agent (never TASK-027).
Goal: confirm the PCSX2 v2.8.2 replacement contract on our stream and correct GSDX-PLAN D.3 (no zip packs;
duplicates resolved by enumeration order; PNG only 8-bit RGB/RGBA; `-mipN` files ignored; `[Folders] Textures`
relative to `DataRoot`). Measure map scan time, RAM (with and without precache) and VRAM on a synthetic
10,000-file pack.
Acceptance:
- Python test (skipped without the private capture): replay twice with dumping on, identical dump name sets;
  solid-magenta replacement of a texture used in frame K appears in frame K (`Async = false`) and within 2 vsyncs
  with `Async = true`; one-digit-changed name, `-mip1` name and empty view leave frames byte-identical to the
  baseline; duplicate-key rule recorded.
- Name-parser fixture built from real dump names (all parse; round trip name → key → name).
- Measurements recorded in GS-LIVE or a new `docs/GS-TEXTURES.md`; D.3 corrected; `gs-settings.toml` texture rows
  get their sources.
- Optional: name-set overlap ≥ 95% against a PCSX2 v2.8.2 dump of the same menu scene; owner tries a community
  pack if available.
Risks: hash-relevant settings (`HWMipmap`, `TriFilter`, `UserHacks_EstimateTextureRegion`) differ from a pack
author's; the capture may not contain the texture chosen (pick from the dump set itself).

### TASK-051 — `gt4mods` core: manifest, detection, validation, import, store
Repo: main, branch `task/051-mods-core` cut from `task/019-launcher` (the launcher workspace lives there); new
UI-free crate `launcher/crates/gt4mods`. Haiku, 3 sessions; Opus reviews the validation code (plan 5.2).
Goal: `gt4mod.toml` schema 1 and `files.lock.json`; import from zip or folder with or without a manifest;
detection of raw PCSX2 layouts (`SCUS-97328/replacements`, bare `replacements`, name-majority folders); texture
key parser (all five name forms); PNG/DDS validation; staging and atomic commit into `mods\store\<id>\`;
registry `mods.toml` written with temp + `ReplaceFileW`; operation journal and recovery at start.
Acceptance: Rust tests with synthetic zips: zip-slip (`..`, absolute, drive, ADS, device names, case
collisions), zip bomb (ratio, total, entries, lying headers), symlink and encrypted entries refused; PNG 16-bit,
paletted and interlaced detected; DDS block-size and truncated data detected; within-pack duplicate rule; kill
between steps leaves a recoverable state; update of an installed id keeps enabled state and order.
Risks: very large packs (streaming hash and extraction, progress and cancel); non-ASCII and long paths.

### TASK-052 — Active view, priority, conflicts, uninstall, CLI
Repo: main, branch `task/052-mods-view` (after TASK-051, same base). Haiku, 2 sessions.
Goal: build `mods\active\textures\SCUS-97328\replacements\` with one hardlink per winning texture key (top of
the list wins), copy fallback when hardlinks fail; view digest; atomic swap (`active.new` → `active`); conflict
report per mod; enable/disable/move without reinstall; uninstall to the Recycle Bin with the quota check; a
`gt4mods` CLI (`list`, `import`, `enable`, `disable`, `move`, `remove`, `build-view`, `report`) for tests and
the dev launcher.
Acceptance: Rust tests: each key exactly once in the view; priority changes switch the winner; identical-content
collisions not reported; view rebuilt only when the digest changes; refused while the game lock is held;
tampered view detected and rebuilt; copy fallback on a non-NTFS temp volume (or simulated failure); Recycle Bin
path covered by a fake platform layer. Manual: 10,000-file view build time recorded.
Risks: hardlink limits (1,023 links per file, irrelevant here); antivirus scanning many new links.

### TASK-053 — GS settings and dev launcher wiring for mods
Repo: main, branch `task/053-mods-gs-wiring` (from `main`). Haiku, 1 session.
Goal: in `docs/launcher/gs-settings.toml` move `texture_load`, `texture_load_async`, `texture_precache` off the
Graphics page (`page = "Mods"`, `visibility = "hidden"`, `written = "launcher"`), move the dump rows to a creator
group, add `textures_folder` (`[Folders] Textures`, `path`, launcher, absolute); `scripts/gs_settings.py`
accepts the new page; `external_runtime_probe.py` / `play_performance.py --textures-root <dir>` writes the
folder and `LoadTextureReplacements = true` when the folder has files; `result.json` records the folder digest.
Acceptance: Python tests: golden INI unchanged without the option; with the option, the three keys and the
folder appear; relative paths refused; UTF-8 path with non-ASCII characters round trips; allow-list regenerated;
pins regenerated where the probe changes. Owner: one in-game run with a synthetic or community pack.
Risks: gt4-gs pin and allow-list drift (TASK-037/038 enforcement).

### TASK-054 — Launcher Mods page
Repo: launcher branch after TASK-019 resumes (Tauri 2). Haiku, 2-3 sessions.
Goal: `mockup-mods.html` (states: empty, list, details, import progress, conflict, remove confirm, wrong
renderer) for owner approval, then the Tauri page and commands (`mods_list`, `mods_import`, `mods_set_enabled`,
`mods_move`, `mods_remove`, `mods_details`, progress and cancel events); drag and drop of zips; Inbox folder
import at start; controller navigation (D-pad focus, Cross toggle, Triangle details, L1/R1 priority, Square
remove, Start install help, Circle back); the view is rebuilt before Play and its digest written to `launch.json`.
Acceptance: owner approves the mockup; Rust command tests; UI test of keyboard and XInput focus order; owner
installs, reorders and removes a pack end to end.
Risks: WebView file drop and CSP; long imports must not block the UI thread.

### TASK-055 — Mod creator tools
Repos: launcher branch (UI and packaging), main docs. Haiku, 1-2 sessions.
Goal: collapsed "Creator" section: "Dump textures" for the next Play (folder `mods\creator\textures`, dump keys
written by the launcher, FMV off), open dumps and work folders, "Package as mod" (zip with `gt4mod.toml`,
validated by the TASK-051 importer), "Clear dumps" (Recycle Bin); `docs/MODDING.md` for pack authors (names,
formats, alpha scale, BC7 with mips advice, hash-relevant settings).
Acceptance: a dump session produces PNGs in the creator folder only; a packaged work folder re-imports cleanly;
docs reviewed by the owner.
Risks: dumping cost in races (documented banner).

### TASK-056 — Live texture reload in gt4-gs (after 1.0.0)
Repos: fork `gt4gs/texture-reload`, main. Haiku, 1-2 sessions.
Goal: a control message (`reload_textures`) that calls the replacement map reload on the GS thread, and a game
window key that sends it, so creators see edits without restarting.
Acceptance: headless test: replacement added during a replay appears after the message; no leak across reloads.
Risks: protocol version bump; reload stalls (sync worker thread wait).

### TASK-057 — Study: other mod kinds (after 1.0.0)
Repo: main docs only. Opus, 1 session.
Goal: decide on `gs-preset` mods (bundles of `gs-settings.toml` rows), a read-only `GT4.VOL` overlay for data mods
(cars, tracks, menus; Spec II idea), and RAM data patches; code patches stay out (AOT EE).
Acceptance: a decision record with risks and effort; no code.

Release: 1.1.0. Owner decisions (2026-10-16)

1. The mod manager is **not in release 1.0.0**; it is planned for **1.1.0**. It is tracked in TASKS.md only
   (not in the README task table). The launcher's Mods tab stays hidden in 1.0.0.
2. Priority: the mod higher in the list wins a texture conflict.
3. TASK-040 is folded into TASK-050.
4. The "precache all textures" toggle is hidden (off by default; RAM risk).
5. PAL-version packs are allowed with a warning.
6. PNGs in an unsupported format: the launcher shows a warning and asks whether to convert them to 8-bit.
7. Third-party pack catalogue: not now, but planned later (we never host packs ourselves).
8. Limits: 20 GiB per pack; zip and loose-folder import.
9. Creator texture dumps are taken with mods off, from a creator option in the Advanced settings.
10. A pack too big for the Recycle Bin: ask clearly, then delete permanently only after on-screen confirmation.

### TASK-060 — Cursor auto-hide
Repos: both, branches `task/060-062-osd` (main, from `3b5334d`) and `gt4gs/osd` (fork, from `0e40edc92`). Opus, 1 session
(with 061 and 062).
Goal: hide the mouse cursor after about 3 s without movement, in fullscreen and in windowed mode only over the picture;
show it on any movement; never touch it elsewhere; the game window and the gt4-gs child window (another process) agree.
`GT4_CURSOR_AUTOHIDE=0` switch (default on) and an Advanced-page row.
Acceptance: CTest of the rule; a visible tiny fixture with the real child (move, hold, assert visibility); owner check.
State: ✅ done, owner test 2026-10-16 (fullscreen and windowed); merged with main as slice202.
- `native/cursor_autohide.*` (pure rule, CTest `native_cursor_autohide`), used by `window.cpp`; protocol 1.2 CONTROL
  cursor carries the policy to gt4-gs 1.0.0, whose window thread applies the same rule and answers `WM_SETCURSOR`.
- `tools/gs_live_check --cursor-check` (visible, about 1.7 s) passes; details [GS-LIVE](docs/GS-LIVE.md) section 11.1.

### TASK-061 — "Compiling shaders…" notice
Repos: fork (`gt4gs/osd`) and main (settings row). Same session.
Goal: plain text "Compiling shaders…" bottom-left while the PCSX2 GS really compiles shaders or pipelines (not cache
loads), gone when compiling stops; reuse PCSX2's compile signal; on by default with an Advanced-page row.
State: ✅ done, owner test 2026-10-16 (gt4-gs 1.0.0). `GSShaderCompileIndicator` plus a real-compile timestamp;
pipelines from the driver cache are excluded with `VK_EXT_pipeline_creation_feedback`; shown until 0.5 s after the last
compile. Cold cache 238 compiles (notice drawn), warm cache 0 compiles and 138 cache hits (no notice). [GS-LIVE](docs/GS-LIVE.md) 11.2.

### TASK-062 — Performance overlay
Repos: both. Same session.
Goal: PCSX2-OSD-like overlay, top-right, each item switchable: FPS, frame time, CPU and GPU usage of the game only (game
process plus gt4-gs), output and internal resolution, per-part ms per frame and the headroom against 16.67 ms (the F8
summary, live, about twice per second); off by default; negligible overhead (measured).
State: ✅ done, owner test 2026-10-16. Game numbers in protocol 1.2 OSD_STATS records (sent only when
READY asks for them); CPU via GetProcessTimes of both processes, GPU via PDH "GPU Engine" counters of both pids; drawn
with the PCSX2 ImGui OSD. Overhead: 0.35 us/frame on the EE thread, 12-15 us/frame of ImGui text and +0.02 ms/frame of GS
thread time. Rows `perf_overlay` (master) and one per item. [GS-LIVE](docs/GS-LIVE.md) 11.3.

Settings and stable renderer (same branch): internal resolution presets 480p (1x), 720p (2x), 1080p (3x), 1440p (4x),
4K (6x), 8K (12x) only (default 1080p; launcher rules for the monitor height and the GPU limit documented in
[GS-SETTINGS](docs/GS-SETTINGS.md)); `--gs-resolution`; gt4-gs 1.0.0 ignores INI keys and values outside the allow-list
with a warning; **`gt4gs-v1.0.0` published as the first stable release** and pinned by slice201.

### TASK-049 - GS settings curation for the launcher
Done (2026-10-16). Owner decisions applied to `docs/launcher/gs-settings.toml` (58 rows, `page`, `order`, `control`,
`gt4-defaults` profile): Graphics page = renderer choice (PCSX2 GS or CoreGS 1.1), internal resolution slider 1 to 12,
aspect, FXAA, CAS mode and sharpness; Advanced page = 36 rows pre-configured to the GT4 ideal; texture rows on `mods`.
Findings (PCSX2 v2.8.2, [GS-SETTINGS](docs/GS-SETTINGS.md)): FSR 1.0 does not exist in v2.8.2 (CAS only);
12x needs no `ExtendedUpscalingMultipliers` (the device limit is `max texture size / 1280`); READY does not carry the
device limit yet; widescreen patches are pnach EE patches and are not exposed. `reset_to_defaults()` reproduces the
default gt4-gs.ini byte for byte. Default gt4-gs.ini unchanged. The `mipmap` key is now `hw_mipmap`.

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
timing, remaster) is out of scope for this repository and would be a
separate project, possibly a fork. Presenting more than 60 FPS with the game's
fixed 1/60 s physics untouched is in scope (TASK-031, owner 2026-10-15). Ultrawide and other aspect ratios
are in scope (TASK-080, owner 2026-10-21).

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
