<div align="center">

# GT4Recomp

**Gran Turismo 4, recompiled to run natively on PC — no JIT, no emulator window, full speed.**

![EE](https://img.shields.io/badge/EE%20(main%20CPU)-100%25%20AOT-2ea44f)
![VU0 macro](https://img.shields.io/badge/VU0%20macro-100%25%20AOT-2ea44f)
![VU1 micro](https://img.shields.io/badge/VU1%20micro-AOT%20%C2%B7%20all%202%2C048%20entries-2ea44f)
![VU0 micro](https://img.shields.io/badge/VU0%20micro-AOT%20%C2%B7%2014%2F14%20entries-2ea44f)
![IOP](https://img.shields.io/badge/IOP-HLE%20%2B%20interpreter-orange)
![GS](https://img.shields.io/badge/GS-PCSX2%20GS%20in%20a%20separate%20process-blue)

![Race](https://img.shields.io/badge/race-60%20FPS%20%C2%B7%20100%25%20speed-2ea44f)
![Showroom](https://img.shields.io/badge/showroom-60%20FPS-2ea44f)
![Night race](https://img.shields.io/badge/night%20race-60%20FPS-2ea44f)
![Scenes](https://img.shields.io/badge/scenes%20tested-6-informational)
![Frame pacing](https://img.shields.io/badge/frame%20pacing-16.7%20ms%20flat-2ea44f)
![JIT](https://img.shields.io/badge/guest%20JIT-none-informational)
![Platform](https://img.shields.io/badge/platform-Windows%20x64%20%C2%B7%20Vulkan-informational)
![License](https://img.shields.io/badge/license-MIT-blue)
![Tasks](https://img.shields.io/badge/tasks%20done-34%20of%2063-2ea44f)

</div>

![Toyota Supra on the Nordschleife at 60 FPS](docs/images/race-nordschleife-60fps.webp)

GT4Recomp is a static recompilation of **Gran Turismo 4 (NTSC-U, SCUS-97328)**
for **Windows x86-64**. The game's own code is translated ahead of time into C++
and compiled to native machine code; the PlayStation 2 hardware it talks to is
reproduced in software, and graphics run on **Vulkan**. Nothing generates code
while the game runs, which is also what makes future **Android and iOS** builds
possible.

**Where we are (October 2026):** the game boots, plays and races natively at
**60 FPS and full game speed** with a **flat 16.7 ms frametime**: Time Trial and
AI races, rally and the car showroom all hold 60 FPS, and night races run at
about 59 (Ryzen 7 5700X + RX 9060 XT). Every PS2 vector-unit program the game
uses (all VU1 and VU0 microcode) now runs as precompiled native code, with no
interpreter on the normal path. Races render in **480p progressive**, the window
is **16:9** with borderless fullscreen. Next: the **PCSX2 GS** renderer in a separate process (`gt4-gs.exe`, decision 0070)
(upscaling, sharper image), VU1 on its own CPU core for weaker CPUs and phones,
and a one-click **launcher and installer** for the first stable release.
License tests are playable (TASK-017); some license demos do not play, as in PCSX2 with default settings.
See [Scene results](#scene-results) and [Known issues](#known-issues).

[Status](docs/STATUS.md) · [Optimization plan](docs/OPTIMIZATION-PLAN.md) · [Build guide](docs/BUILD.md) · [Credits](#credits)

## Recompilation progress

How much of each PS2 processor runs as precompiled native code instead of being
interpreted at run time:

| Unit | What it does | Status | Progress |
|---|---|---|---|
| **EE** (main CPU) | Game logic, physics, menus, draw lists | Translated ahead of time to C++; no EE interpreter exists | **100%** of executed code · 5,676 unreached decomp functions to add for coverage |
| **VU0 macro** | Vector math inside EE code | Translated together with the EE | **100%** |
| **VU1 microcode** | Vertex transform, lighting, clipping (the "vertex shader") | Whole microcode image recovered from the executable; all 2,048 entry points compiled ahead of time | **100%** · 0 interpreted calls in day/night races, rally, showroom |
| **VU0 microcode** | Small helper programs | All 14 entry points compiled ahead of time with exact flags | **100%** · 0 interpreted calls |
| **IOP** (I/O CPU) | Sound, disc, memory card, USB | System libraries replaced by native code; 23 game modules interpreted | **System libraries native** · game modules later |
| **GS** (graphics chip) | Rasterization | Faithful GS emulation in Vulkan compute shaders, asynchronous submission | PCSX2 GS in a separate process (TASK-032..044); CoreGS 1.1 built in as fallback |

## Performance journey

Same machine, same scene (Time Trial, Nordschleife, Supra), measured with the
built-in F7/F8 profiler:

| Step | Frame time (median) | FPS | Game speed |
|---|---|---|---|
| Starting point (VU1 interpreted, synchronous GPU) | 33.3 ms | ~30 | 51% |
| VU1 compiled ahead of time | 22.0 ms | ~45 | 75% |
| + asynchronous GPU submission | 60 VBlanks/s | 60 | 100% |
| + single-clock frame pacing | **16.67 ms** (99.6% of frames within 16–17 ms) | **60** | **100%** |

Harder scenes, same machine:

| Scene | Before (VU1 partly interpreted) | Now (all VU1/VU0 compiled, flag elision) |
|---|---|---|
| Car showroom | 44.8 FPS | **60 FPS** |
| Rally, Grand Canyon | 53.9 FPS | **59.9 FPS** |
| Night race with AI cars | 46.2 FPS | **58.9 FPS** |

<img src="docs/images/frametime-flat.webp" alt="Flat frametime graph at 60 FPS" width="460">

## Scene results

Owner sessions on the current `main` build (`--mode main`: single-clock pacing,
all VU1 and VU0 microcode compiled, async GPU), one F7/F8 interval per scene:

| Scene | FPS (VBlanks/s) | Game speed | Notes |
|---|---|---|---|
| Time Trial, Nordschleife, Supra (480p) | **60.0** | **100%** | 99.6% of frames within 16–17 ms |
| Arcade race, Nordschleife, 5 AI cars | **60.0** | **100%** | After VU1 flag elision (was 58.0) |
| Replay of that race | 59.1 | 98.5% | |
| Night race with AI cars | 58.9 | 98% | Remaining cost is a ~4 ms GPU readback wait, targeted by the PCSX2 GS (TASK-037) |
| Rally, Grand Canyon, 1 AI car | 59.9 | 99.8% | All VU1 entries compiled (was 53.9) |
| Car showroom (rotating car) | **60.0** | **100%** | Steady on the main build (was 44.8); short dips to 58 with some cars |

## Known issues

- **Some license demos and backgrounds do not play; the licenses are playable.**
  This is the same as PCSX2 with default settings. GT4 decrypts the license
  demos with a cipher built on floating-point results, and our FPU is not
  bit-exact with the PS2, so about half the demos decrypt wrong. The game's own
  check fails, and since TASK-017 the build skips such a demo instead of
  crashing ([details](docs/LICENSE-DEMO-CIPHER.md)). Replays and ghosts saved on
  a real PS2 card do not load either (a clean load error). The exact FPU fix is
  TASK-063.
- **Night races** run at about 59 FPS: the remaining cost is a GPU readback wait
  that the PCSX2 GS (TASK-037) is expected to remove. With the PCSX2 GS (accurate
  readbacks) the night race holds 59.97 FPS; its remaining late frames come from the
  flare readback waiting for the VU1 worker and, at race start, from pipeline compiles
  (TASK-041 analysis; TASK-046 "prepare shaders" targets the compiles).
- **Weaker CPUs:** VU1 work now runs on its own worker thread (MTVU, TASK-011),
  on by default in `--mode main`; `GT4_MTVU=0` restores the single-thread path for
  A/B. The weak-CPU effect is not measured yet (`--cpu-cores N` is the simulation
  switch); it matters most for older PCs and phones.
- Menus are interlaced like the original game (480p applies in races). With the
  PCSX2 GS (`--mode pcsx2`, gt4-gs 0.3.2 and later) they are deinterlaced (TASK-047; the
  stream lacked SYNCV and presented the wrong field). CoreGS 1.1 shows them as before.
- With `--mode pcsx2` (slice203, gt4-gs 1.0.1) the mouse cursor hides over the picture
  after 3 s without movement (`GT4_CURSOR_AUTOHIDE=0` keeps it), a small "Compiling
  shaders…" text shows while shaders compile, and `--gs-setting perf_overlay=true` shows
  a performance overlay (owner test 2026-10-16).
- Fixed in gt4-gs 1.0.1 (slice203, TASK-044): gt4-gs no longer writes its log to `Documents\PCSX2\logs\emulog.txt` or
  creates the PCSX2 user folders there. A live run and an offline replay left the `Documents\PCSX2` snapshot unchanged.
- gt4-gs 0.4.0 (`--mode pcsx2`, TASK-042) turns on Windows Arbitrary Code Guard (no
  dynamic code in the GS process). It is checked with the owner's AMD driver only; a
  driver that needs dynamic code there would fail to start the GS (`--mode
  pcsx2-slice196` keeps 0.3.2).
- Physical gamepads, racing wheels and long sessions are not fully qualified yet.

## Roadmap

Work is tracked as numbered tasks in [TASKS.md](TASKS.md); each task is tested
in game before it is merged.

| Phase | Platform | Renderer |
|---|---|---|
| **Phase 1** (current) | Windows PC | PCSX2 GS in a separate GPL process (`gt4-gs.exe`, decision 0070), downloaded by the launcher; CoreGS 1.1 (MIT) built in as fallback; CoreGS 2.0 planned |
| **Phase 2** | Android | ARMSX2 GS pipeline (Vulkan) |
| **Phase 3** | iOS | ARMSX2 GS pipeline (Metal) |

### Phase 1 — Windows PC

| Task | Goal | Result / State |
|---|---|---|
| TASK‑001 | Rebuild the measured baseline on the new test PC | ✅ Done |
| TASK‑002 | Record the VU1 programs the game uses while playing | ✅ Done · 86 programs captured |
| TASK‑003 | Hardened VU1 compiler with differential tests | ✅ Done · 100/100 programs compiled |
| TASK‑004 | VU1 microcode compiled ahead of time in the game | ✅ Done · 30 → 45 FPS, 0 mismatches in 140k checks |
| TASK‑005 | Asynchronous GPU submission + overlay fix | ✅ Done · 45 → 60 FPS, 100% game speed |
| TASK‑006 | Frame pacing on a single clock, rate-controlled audio | ✅ Done · flat 16.7 ms frametime |
| TASK‑007 | VU1 per-program flag elision | ✅ Done · showroom 45 → 59 FPS, 5-AI race 60 FPS |
| TASK‑008 | Full static VU1 entry coverage (all 2,048 entries) | ✅ Done · 0 interpreted VU1 calls; rally 60, showroom 60, night 59 FPS |
| TASK‑009 | Title, fixed 16:9, fullscreen, races in 480p | ✅ Done · races forced to 480p, menus 480i as original |
| TASK‑010 | Build pipeline: faster builds and tuning (CPU variants, LTO, PGO) | ✅ Done · full build 3,942 → ~1,700 CPU-s |
| TASK‑011 | MTVU: VU1 on its own thread | ✅ Done · in `--mode main` (on by default, `GT4_MTVU=0` A/B) · 5–6 ms/frame off the main core, headless 42/42 on and off |
| TASK‑012 | paraLLEl-GS as an optional renderer | ❌ Dropped (decision 0070); branch kept as history |
| TASK‑013 | VU0 microcode compiled ahead of time | ✅ Done · 14/14 entries, 0 interpreted calls, 28k differential checks |
| TASK‑014 | Full EE entry coverage from the decompilation | 🔄 Stage A done |
| TASK‑015 | LWR instruction fidelity check | ✅ Done · already matches PCSX2; build audio test now silent |
| TASK‑016 | IOP per-module cost review | ⏸️ Deferred |
| TASK‑017 | License-test crash | ✅ Done (2026-10-16) · licenses playable; demos that decrypt wrong are skipped (as PCSX2 default) |
| TASK‑018 | Racing wheels with force feedback | 📋 Planned |
| TASK‑019 | Launcher (Rust) + per-user installer: pick your ISO once, then Play | ⏸️ Paused (Tauri 2 WIP); resumes for TASK-043 |
| TASK‑020 | Decompilation names in the generated code | 📋 Planned |
| TASK‑025 | Save manager: memory card slots, import saves from PCSX2 cards and save files | ✅ Code done in `task/019-launcher`; ships with the launcher (owner test pending) |
| TASK‑026 | CoreGS 2.0 (our own MIT hardware renderer, clean room): specification | ✅ Done |
| TASK‑027 | CoreGS 2.0: implementation, 3x-6x resolution and texture packs | ⏸️ Paused · core libraries done |
| TASK‑028 | CoreGS 1.1: up to 3x internal resolution, deinterlace, FXAA, FSR 1.0 | ⏸️ Paused by the owner: 3x shows native-size aliasing on 3D models; stays the built-in fallback |
| TASK‑029 | Controller and keyboard mapping with a launcher menu | ✅ Code done in `task/019-launcher`; ships with the launcher (owner test pending) |
| TASK‑030 | Night-race readback wait (CoreGS 1.1) | ⏸️ Paused · owner test 59.07 FPS; the 4.3 ms wait is unchanged |
| TASK‑031 | 120-144 FPS with the game's physics untouched | ❌ Dropped in this form; redone as TASK-045 on the PCSX2 GS |
| TASK‑032 | Decision 0070: PCSX2 GS in a separate process (docs) | ✅ Done · decision 0070 (PR #24) |
| TASK‑033 | PCSX2 GS fork (`moesuito/gt4recomp-gs`): reproducible build and CI releases | ✅ Done · release `gt4gs-v0.1.0-poc` ([fork](docs/GS-FORK.md)) |
| TASK‑034 | Proof of concept: our capture through the PCSX2 GS, image comparison | ✅ Done · owner approved the images (2026-10-16) ([report](docs/GS-PCSX2-POC.md)) |
| TASK‑035 | MIT GS stream protocol, shared-memory IPC, renderer seam | ✅ Done · slice191, stream identical in both renderer modes ([spec](docs/GS-STREAM-PROTOCOL.md)) |
| TASK‑036 | Live `gt4-gs.exe` MVP | ✅ Done · owner test 2026-10-16 at 3x: Nordschleife 59.98 FPS, night race 59.89 FPS ([live](docs/GS-LIVE.md)) |
| TASK‑037 | PCSX2 GS readbacks and the night race | ✅ Done · night race 59.97 FPS, 69 late frames (was 213); accurate readbacks by default ([live](docs/GS-LIVE.md)) |
| TASK‑038 | GS window, fullscreen, focus, input, crash handling | ✅ Done · fullscreen, focus and click fix (gt4-gs 0.3.1), crash handling with CoreGS 1.1 fallback |
| TASK‑039 | GS settings table and `gt4-gs.ini` generator | ✅ Done · 51-row settings table generates `gt4-gs.ini`; allow-list enforced by gt4-gs 1.0.0 (ignored with a warning) ([settings](docs/GS-SETTINGS.md)) |
| TASK‑041 | GS process performance (`[gs-ipc]`), tuning | ✅ Done · owner test 2026-10-16: night race 59.97 FPS, 53 late frames (was 69), EE critical path 11.8 of 16.7 ms (headroom 4.9 ms); GS wake batching and ordered privileged writes ([live](docs/GS-LIVE.md#9-performance-of-the-pcsx2-gs-path-task-041-slice198)) |
| TASK‑042 | JIT-free `gt4-gs.exe` (required before 1.0.0) | ✅ Done · owner test 2026-10-16 (menus, night race): gt4-gs 0.4.0 JIT-free, enforced by Arbitrary Code Guard; images identical to 0.3.2 |
| TASK‑043 | Launcher: download, pin and update the GS renderer | 📋 Planned (waits for TASK-019) |
| TASK‑044 | Packaging, release notes, release 1.0.0 plan | 📋 Planned |
| TASK‑045 | High frame rate on the PCSX2 GS (TASK-031 redone) | 📋 Planned (after 1.0.0) |
| TASK‑046 | "Prepare shaders": warm the PCSX2 GS pipeline cache before the first race | 📋 Planned (study done) |
| TASK‑047 | PCSX2 GS deinterlacing in the 480i menus | ✅ Done · owner test 2026-10-16: sharp, steady 480i menus (SYNCV + display field, gt4-gs 0.3.2) ([live](docs/GS-LIVE.md#8-480i-deinterlacing-task-047-slice196-gt4-gs-032)) |
| TASK‑049 | GS settings curation for the launcher (Graphics and Advanced pages, reset to defaults) | ✅ Done · default gt4-gs.ini unchanged; 58-row table ([settings](docs/GS-SETTINGS.md)) |
| TASK‑058 | Crash-safe memory card in the real build (atomic write, retry, exit flush) | ✅ Done · owner test 2026-10-16; kill test 25/25, card flushed on exit and after a GS loss (slice200, in slice202) |
| TASK‑059 | Installed game folder: shaders and GS package next to the exe, `--portable-check`, `make_distribution.py` | ✅ Done · owner test 2026-10-16; headless smoke from the folder 42/42 and 41/41 ([distribution](docs/DISTRIBUTION.md)) |
| TASK‑060 | Mouse cursor hides over the picture after 3 s (fullscreen and windowed) | ✅ Done · owner test 2026-10-16: hides in fullscreen and over the picture only in a window (`GT4_CURSOR_AUTOHIDE=0` keeps it) ([live](docs/GS-LIVE.md)) |
| TASK‑061 | "Compiling shaders…" notice while the PCSX2 GS compiles | ✅ Done · owner test 2026-10-16; only real compiles (cache hits excluded), gt4-gs 1.0.0 |
| TASK‑062 | Performance overlay: FPS, frame time, CPU/GPU of the game, resolutions, frame cost and headroom | ✅ Done · owner test 2026-10-16; off by default (`--gs-setting perf_overlay=true`), +0.02 ms/frame GS thread |
| TASK‑063 | Bit-exact PS2 FPU division and multiply (all license demos, real-PS2 card replays) | 📋 Planned · needs hardware test vectors |
| 🧩 | **gt4-gs 1.0.0: first stable renderer release** | ✅ Published 2026-10-16, pinned in slice202 (`--mode pcsx2`) ([fork](docs/GS-FORK.md)) |
| 🧩 | **gt4-gs 1.0.1: stable, no writes under `Documents\PCSX2`** | ✅ Published 2026-10-16 (`gt4gs-v1.0.1`), pinned in slice203 (`--mode pcsx2`) ([fork](docs/GS-FORK.md)) |
| 🚀 | **First stable release (1.0.0) on GitHub** (after TASK‑044) | 📋 Planned |

### Phase 2 — Android · Phase 3 — iOS

| Task | Goal | State |
|---|---|---|
| TASK‑021 | ARMSX2 GS pipeline (Vulkan) for Android | 📋 Planned |
| TASK‑022 | Android app: touch controls, Bluetooth controllers | 📋 Planned |
| TASK‑023 | ARMSX2 Metal GS for iOS | 📋 Planned |
| TASK‑024 | iOS app | 📋 Planned |

Details: [TASKS.md](TASKS.md) · [GS renderer options](docs/GS-RENDERER-OPTIONS.md) · [Optimization plan](docs/OPTIMIZATION-PLAN.md).

## The Experience We Want to Build

- **Correct speed, smooth 60 FPS** on ordinary PCs, then on phones.
- **Configurable internal resolution** (1080p, 1440p, 4K) with real internal upscaling.
- **Clean image options:** PCSX2 GS upscaling and texture packs, deinterlaced menus, post-processing.
- **Racing wheels** with force feedback.
- **Faithful first:** the original game, as accurate as possible, just faster and sharper.

A decompilation-based source port (native renderer, high refresh rates independent of
physics, ultrawide, remaster features) is a possible separate project later; it is not
part of this repository's roadmap.

## How It Works

The EE game code is translated ahead of time (AOT) into C++ and compiled for
x86-64. The VU1 microcode, stored encrypted inside the executable, is recovered,
compiled offline (all 2,048 entry points) and verified against a reference
interpreter on real game state (hundreds of thousands of checks, zero
mismatches); the 14 VU0 microprograms are compiled the same way with exact flags. IOP system services are
native; the GS is reproduced with Vulkan compute shaders. Frames are paced by a
single high-resolution clock with rate-controlled audio. Interpreters remain only
as counted fallbacks. This is a **hybrid recompilation**: PS2 hardware is still
modeled in software, so we do not claim zero emulation or full PS2 fidelity.

For implementation details, see [Architecture](docs/ARCHITECTURE.md).

## Testing and Development

GS settings validation can be checked without game assets or a native build (Python 3.11+, with
`scripts/requirements.txt` installed):
`python -m unittest discover -s tests/python -p test_gs_settings.py`.
This includes rejecting duplicate setting ids in both CLI assignments and JSON input.

The project currently requires a **development environment with prepared private assets**. This repository does not distribute game assets, ISOs, BIOS dumps, decompiled code, prebuilt binaries, or save files. A separate [EE producer](docs/EE-PRODUCER.md) now rebuilds the translator and game library from verified inputs; production still uses its qualified private library while candidate integration and gameplay checks remain pending. A clean end-to-end playable build remains future work.

Once the environment is prepared, the current build starts with `scripts/play_performance.py --mode main` (races held at 480p, VU1 on a worker thread; F7 starts a measured interval, F8 reports it); it validates every input against its pin and preserves the existing memory card. The planned launcher (TASK-019) replaces this with an installer and a Play button. Requirements and instructions are detailed in the [Build Guide](docs/BUILD.md).

| Key | Action |
|---|---|
| Enter | Start / Pause |
| Z | Confirm / Accelerate |
| A | Brake |
| S | Triangle / Back |
| X | Circle |
| Arrow Keys | Navigate / Steer |
| Alt+Enter, F11 | Toggle borderless fullscreen on the current monitor |

The presentation is fixed at 16:9; there is no 4:3 framing option. Set **Options → Screen → Aspect Ratio** in the game to 16:9. Fullscreen restores the previous windowed size and position when toggled off. Existing memory cards retain saved settings.

`--mode main` holds the game's race display option at 480p by default (owner decision for release 1.0.0): races render progressive while menus stay 480i as in the original. `--force-480p` (or `GT4_FORCE_480P=1`) does the same in the other modes. Each run records the setting.

## Credits

GT4Recomp stands on the work of others. Thank you:

| Project | Author | What it gives this project |
|---|---|---|
| [ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp) (MIT) | snuri00 | The runtime foundation: the EE-to-C++ translator, PS2 hardware/service models, the GS compute renderer and the original VU1 program compiler our `tools/vu_recomp` derives from. GT4 was its first title. |
| [paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs) (LGPL-3.0+) | Hans-Kristian Arntzen (Arntzen Software) | Dropped from the release (decision 0070). Its MIT renderer seam and capture code are reused. |
| [Granite](https://github.com/Themaister/Granite) (MIT) | Hans-Kristian Arntzen | The Vulkan engine of paraLLEl-GS (dropped, decision 0070). |
| [gt4-decomp](https://github.com/gustavocanepa/gt4-decomp) (MIT) | gustavocanepa | A matching decompilation of GT4's game code; its CORE.GT4 is byte-identical to ours ([evidence](docs/DECOMP-IDENTITY.md)). Function names, class layouts and engine notes. |
| [Nenkai's Gran Turismo research](https://github.com/Nenkai) ([gt-modding-hub](https://github.com/Nenkai/gt-modding-hub), [PDTools](https://github.com/Nenkai/PDTools), [GT-File-Specifications-Documentation](https://github.com/Nenkai/GT-File-Specifications-Documentation) and more) | Nenkai | File formats, volume and model structures, and the VU1 microcode encryption scheme that let us find GT4's whole VU1 program image ([details](docs/VU1-MICROCODE-SOURCE.md)). |
| [GT4FS](https://github.com/Razer2015/GT4FS) | Razer2015 | GT4 volume file-system research. |
| [PCSX2](https://github.com/PCSX2/pcsx2) (GPL-3.0) | PCSX2 team | Decades of PS2 knowledge. Its GS renderer runs only in the separate `gt4-gs.exe` (decision 0070) ([lessons](docs/PCSX2-LESSONS.md)). Its MTVU design informs our VU1 threading plan. |
| [ARMSX2](https://github.com/ARMSX2/ARMSX2) (GPL-3.0) | ARMSX2 team | The mobile GS pipeline planned for the Android and iOS phases (without its JIT). |
| [naga](https://github.com/gfx-rs/wgpu/tree/trunk/naga) (MIT OR Apache-2.0) | gfx-rs / wgpu contributors | Compiles the donor's WGSL shaders to SPIR-V at build time (`tools/wgsl_spirv`). |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) (Apache-2.0 OR MIT) | Khronos Group | Vulkan API headers. |

Exact revisions, licenses and imported files are listed in [THIRD_PARTY.md](native/THIRD_PARTY.md).

## License

The project's own code is [MIT](LICENSE). Third-party components keep their own
licenses ([THIRD_PARTY.md](native/THIRD_PARTY.md)); PCSX2 GS (GPL-3.0+) runs only in the separate `gt4-gs.exe` (decision 0070).
Gran Turismo, Gran Turismo 4 and their logos and artwork are trademarks or property
of Sony Interactive Entertainment and Polyphony Digital. GT4Recomp is an unofficial
fan project, not affiliated with or endorsed by them; it does not include the game,
and you need your own copy.

## Documentation

Current work:

- [TASKS](TASKS.md): the ordered task list, with states and results.
- [STATUS](docs/STATUS.md): current state, owner measurements and history.
- [OPTIMIZATION-PLAN](docs/OPTIMIZATION-PLAN.md): technical background of the performance work.
- [BUILD](docs/BUILD.md): build environment, candidate recipes and verification.
- [ARCHITECTURE](docs/ARCHITECTURE.md): how the native runtime is put together.
- [GS-RENDERER-OPTIONS](docs/GS-RENDERER-OPTIONS.md): paraLLEl-GS (dropped), PCSX2 hardware renderer and ARMSX2 compared; decision 0070 is the current choice.
- [VU1-MICROCODE-SOURCE](docs/VU1-MICROCODE-SOURCE.md) · [VU0-MICROCODE-MAP](docs/VU0-MICROCODE-MAP.md) · [IOP-MAP](docs/IOP-MAP.md): where the PS2 co-processor code comes from and how it runs.
- [DECOMP-IDENTITY](docs/DECOMP-IDENTITY.md) · [DECOMP-INTEGRATION](docs/DECOMP-INTEGRATION.md): how the gt4-decomp project relates to this build.
- [PCSX2-LESSONS](docs/PCSX2-LESSONS.md) · [GS-ASYNC-DESIGN](docs/GS-ASYNC-DESIGN.md) · [MENUS-AND-WHEEL](docs/MENUS-AND-WHEEL.md): design notes.
- [Decisions](docs/decisions/) and the daily [journal](docs/journal/).

Earlier plans kept as context: [HYBRID-PORT-PLAN](docs/HYBRID-PORT-PLAN.md), [NATIVE-PORT-PLAN](docs/NATIVE-PORT-PLAN.md), [ROADMAP](docs/ROADMAP.md), [EE-PRODUCER](docs/EE-PRODUCER.md), [EE-CANDIDATES](docs/EE-CANDIDATES.md), [EE-CALLBACK-COVERAGE](docs/EE-CALLBACK-COVERAGE.md), [RENDER-CONTRACTS](docs/RENDER-CONTRACTS.md), [CAR-ASSET-RESEARCH](docs/CAR-ASSET-RESEARCH.md), [NENKAI-REFERENCES](docs/NENKAI-REFERENCES.md), [AGENT-WORKFLOW](docs/AGENT-WORKFLOW.md).

Source code lives in `native/` (runtime adapter), `tools/` (translators and the VU recompiler), `scripts/` (preparation and launchers) and `tests/`. The binary runs directly on Windows: no browser, no WebAssembly and no embedded PCSX2. The active development fork is [`moesuito/gt4-staticrecomp`](https://github.com/moesuito/gt4-staticrecomp).
