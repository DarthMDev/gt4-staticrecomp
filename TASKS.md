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
| **Phase 1** (current) | Windows PC | Original game running natively: correct speed, smooth 60 FPS, no interpreter on the normal path, paraLLEl-GS renderer |
| **Phase 2** | Android | ARM64 build with the ARMSX2 mobile GS pipeline (Vulkan) |
| **Phase 3** | iOS | ARM64 build with the ARMSX2 Metal GS pipeline |

**Renderer decision (2026-10-15, [decision 0069](docs/decisions/0069-own-hardware-renderer-clean-room.md)):**
PC offers **paraLLEl-GS** (Vulkan compute, LGPL-3.0+) and, planned, **our own MIT
hardware GS renderer** written by clean room from a specification of the PCSX2
hardware renderer's behavior (TASK-026 specification, TASK-027 implementation:
internal resolution 1x-6x, texture packs). The release ships the prebuilt game
executable. The donor compute GS renderer stays only as an experimental fallback
and comparison reference; it gets no new features.
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
| TASK‑008 | Full static VU1 entry coverage (all 2,048 entries) | ✅ Done |
| TASK‑009 | Quick fixes: title, fixed 16:9, fullscreen, interval counters, races in 480p | ✅ Done |
| TASK‑010 | Build pipeline: faster builds and build tuning (parallel, cache, lld-link, fp-contract off, x86-64-v3 variant, LTO, PGO) | ✅ Done (tuning measurements pending) |
| TASK‑011 | MTVU: VU1 on its own thread, on by default | 📋 Planned |
| TASK‑012 | paraLLEl-GS as the PC renderer: integration, upscaling, deinterlacing, readbacks, extras, GT4 validation | 🔄 In progress |
| TASK‑013 | VU0 microcode compiled ahead of time | ✅ Done |
| TASK‑014 | EE entry coverage from the decomp function list | 🔄 Stage A done |
| TASK‑015 | LWR sign-extension fidelity check | ✅ Done (already correct; no change) |
| TASK‑016 | IOP per-module cost counters and review | ⏸️ Deferred (not needed for 1.0.0) |
| TASK‑017 | License-test crash | 📋 Planned |
| TASK‑018 | Racing wheels with force feedback | 📋 Planned |
| TASK‑019 | Launcher (Rust + Slint) and NSIS per-user installer | 🔄 Design and plan done (branch `task/019-launcher`) |
| TASK‑025 | Save manager: memory card slots, import/export (launcher depends on it) | 📋 Planned |
| TASK‑026 | Own hardware renderer, part 1: clean-room specification of the PCSX2 hardware renderer (study) | 🧪 Spec complete (chapters 00-15), ready for owner review |
| TASK‑027 | Own hardware renderer, part 2: MIT implementation from the specification (3x-6x, texture packs) | 📋 Planned |
| 🚀 | **First stable release on GitHub** (after TASK‑019) | 📋 Planned |
| TASK‑020 | Decomp names in generated EE code (waits for more decompilation progress) | 📋 Planned |

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

## Phase 1 details

### TASK-011 — MTVU
Run VU1 programs on a worker thread (donor `start_vu_thread` exists), on by
default with an env switch to disable; exact sync points wherever the EE observes
VU1 results. Acceptance: identical headless frames, verify 0 mismatches, owner
showroom/night/race frametime equal or better.
State: plan in docs/MTVU-PLAN.md; step 0 (measurement build slice169, `--mode mtvu-measure`) done on
branch `task/011-mtvu`; owner verdict go (section 10). Step 1 (SPSC ring and ordered journal, CTest
`native_mtvu_ring`, `native_mtvu_journal`) committed. Step 2 (runtime capsule slice170, `--mode mtvu`,
`GT4_MTVU`) implemented; step 3 (per-thread F8, slice171, `[mtvu]` section) done; step 4 (slice172: PATH2/PATH3 SIGNAL/FINISH/LABEL applied on the EE at acceptance, the night-race "other" EE wait; reasons split in F8) done, headless 42/42 with MTVU on and off, verify 0 mismatches (MTVU-PLAN section 12). Step 6 (`--cpu-cores N` weak-CPU simulation on the launcher) implemented; owner A/B pending (MTVU-PLAN section 12.6, BUILD Slice172).

### TASK-012 — paraLLEl-GS as the PC renderer
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

### TASK-027 — Own hardware renderer: implementation
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

A decompilation-based source port (native renderer, physics decoupled from frame
rate, ultrawide, remaster) is out of scope for this repository and would be a
separate project, possibly a fork.
