# GT4Recomp

**Gran Turismo 4 on PC. Preserving the original experience and paving the way for what comes next.**

GT4Recomp is a static recompilation project targeting **Gran Turismo 4 USA v2.00 for Windows x86-64**. Our goal is to bring the PlayStation 2 classic to PC with stable performance, configurable graphics, and a codebase structured for research, modding, and future expansion.

**The game already boots, navigates menus, and enters real racing gameplay.** The current base features Vulkan rendering, Windows audio, keyboard support, an XInput controller implementation, and persistent memory card storage. We are in an experimental development phase: the immediate challenge is turning this milestone into a reliable gameplay experience.

[Detailed Status](docs/STATUS.md) · [Hybrid Optimization and Port Plan](docs/HYBRID-PORT-PLAN.md) · [Build Guide](docs/BUILD.md)

**Development direction on this branch:** Windows first, stable 60 FPS at correct
game speed, no guest JIT, and improved GS graphics. Android on Galaxy S23 Ultra
and iOS on iPhone 15 Pro Max are planned targets. Neither mobile port nor stable
60 FPS is qualified today. The earlier native-scene renderer work is preserved
on `main` at slice 140; this branch explores the optimized hybrid route.

## Current Status

| Feature | Current State |
|---|---|
| **Boot and menus** | Real boot, navigable menus, and car/track selection |
| **Gameplay** | In-game racing on Nürburgring accessed and controls exercised; full-race stability still undergoing validation |
| **Vulkan** | Native Windows backend rendering genuine game output on the GPU |
| **16:9 Display** | Project default, using GT4's native option for camera and HUD; 4:3 also available |
| **Video Modes** | 480i, 480p, and 1080i manually tested during gameplay, all in 16:9 |
| **Audio** | 48 kHz stereo output on Windows; continuity improvements still required |
| **Controls** | Functional keyboard and implemented XInput with analog sticks and rumble; physical gamepad hardware untested |
| **Persistence** | Profile and settings saved by the game; persistent memory card with per-session backup and mutual exclusion lock |

**Critical work remains before a stable experience is reached:** low performance in 3D scenes, inconsistent frametimes, audio dropouts, and reports of keyboard double-triggering. Graphical compatibility, saves beyond tested scenarios, and extended stability remain under active qualification.

A keyboard correction now removes duplicate D-pad/analog signals from arrow
keys in the latest capture candidate. Automated controls pass; menu navigation
and digital steering still need owner confirmation. XInput analog sticks remain unchanged.

Manual testing has shown 480p to be the most stable mode so far. Interlaced modes function, but their visual and temporal fidelity are not yet fully qualified.

## The Experience We Want to Build

The primary objective is **playing GT4 stably and at correct game speed on PC**. Building on that foundation, we plan to develop:

- **FPS independent of game speed:** Rendering at 144 FPS or monitor refresh rate while preserving original physics, timers, and game pace.
- **Configurable internal resolution:** 1920×1080, 2560×1440, 3840×2160, and beyond, with progressive-scan output.
- **Ultrawide gameplay:** Adapted camera projection and repositioned HUD, maintaining proportions and legibility.
- **Extensible graphics:** A game-level rendering interface that can support separately scoped future upgrades.
- **Simple configuration:** A launcher to configure video and input settings before launching the game.
- **Readable and modifiable code:** Ongoing decompilation to facilitate research, bug fixes, and future mods.

These are **development goals**, not currently available features. The internal rendering scale remains 1x; resizing the window does not increase the internal resolution. Likewise, decoupled 144 FPS rendering independent of physics is not yet implemented.

## How It Works

The main game code is translated ahead-of-time (AOT) into C++ and compiled to run natively on **x86-64**. Host PC integration provides a native Windows window, **Vulkan**, audio, and input.

The current architecture is a **hybrid recompilation**: IOP and VU microprograms
remain interpreted, and PS2 hardware services are software models. VU0 macro
operations are translated with the EE. Vulkan reproduces the original GS behavior.
The current plan keeps that architecture while optimizing measured costs and
adding graphics controls. Selective offline VU compilation and PCSX2 GS reuse
are candidates, not implemented improvements. The scene-level renderer remains
a preserved later alternative. No guest JIT is required by this direction.

For implementation details, see [Architecture](docs/ARCHITECTURE.md).

## Next Steps

1. Establish an owner-tested Windows performance baseline and identify CPU/GPU work and waits.
2. Optimize demonstrated costs without guest JIT, preserving gameplay, audio and saves.
3. Qualify GS internal scaling, dithering and scan-mode presentation; reuse existing donor mechanisms before larger renderer changes where practical.
4. Build a portable ARM64 core and qualify an Android APK, then an iOS signed build.
5. Expand tested hardware and graphics presets, including the longer-term Windows 1080p/60 target.

Deferred rendering and a graphical remaster are outside the current roadmap.
The current focus is stable Windows gameplay and efficient hybrid rendering.
Internal upscaling is different from enlarging the final image. Dithering and
deinterlacing are separate features. Native ultrawide and 144 distinct frames
independent of physics remain later game-level work.

These are proposed phases, not completed features or published minimum requirements.
The exact entry-level Windows machine remains to be selected. The named phones
are future test targets, with OS/GPU capabilities and sustained performance still
to be qualified. Controls, audio and saves remain part of every acceptance gate;
the delegated performance investigation supplies evidence about costs.

We work in small verified steps. The [Hybrid Port Plan](docs/HYBRID-PORT-PLAN.md)
defines the active branch's phases, licensing boundaries and completion gates;
the [Agent Workflow](docs/AGENT-WORKFLOW.md) describes economical workers and
independent review. There is no announced completion timeline. PCSX2 source
reuse is permitted by the owner under compatible license obligations; no PCSX2
implementation has been imported or linked by this planning change.

The EE translator/library can now be rebuilt from verified inputs. Initial model
and camera mapping is documented, and an isolated experiment has recovered one
omitted callback pair with compilation checks. These foundations are still being
qualified. Separate baseline/recovered runtime candidates are prepared for owner
validation. Tested offline tooling now reads the volume catalogue and extracts
selected resources. The Ford Ka '01 is identified in the correct version's
database and mapped to its exact garage and racing model files. Their serialized
headers and fixed record boundaries are verified; geometry, texture decoding and
live integration remain pending.
See the [one-car research ledger](docs/CAR-ASSET-RESEARCH.md) and
[model asset probe](docs/MODEL-ASSET-PROBE.md).
The [fixed record inventory](docs/MODEL-RECORDS.md) supplies the next boundary
before interpreting shape packets and reconstructing renderable geometry.
The dedicated game-scene renderer has not started replacing live graphics.
The first bounded observation core now has native automated controls for
preserving original shape-call inputs across synthetic suspension and thread
changes. A separately verified full library inserts only the two planned probes.
An isolated observation executable now links those probes and the runtime hooks;
capture starts disabled. A separate manual candidate now supports F8 start/stop
and bounded private metadata export, with synthetic controls and save protection.
The first owner capture has a strong partial layout association with 18 of the
menu Ford Ka's 22 serialized shapes. Live geometry/ownership remain unqualified;
further scene-replacement research is deferred on this optimization branch.
See [Shape Observation](docs/SCENE-OBSERVATION.md).

## Testing and Development

The project currently requires a **development environment with prepared private assets**. This repository does not distribute game assets, ISOs, BIOS dumps, decompiled code, prebuilt binaries, or save files. A separate [EE producer](docs/EE-PRODUCER.md) now rebuilds the translator and game library from verified inputs; production still uses its qualified private library while candidate integration and gameplay checks remain pending. A clean end-to-end playable build remains future work.

Once the environment is prepared, `Jogar-GT4.cmd` launches the game, validates required files, and preserves the existing memory card. Requirements and instructions are detailed in the [Build Guide](docs/BUILD.md).

| Key | Action |
|---|---|
| Enter | Start / Pause |
| Z | Confirm / Accelerate |
| A | Brake |
| S | Triangle / Back |
| X | Circle |
| Arrow Keys | Navigate / Steer |
| F6 | Toggle 16:9 / 4:3 Window Framing |

F6 toggles the window framing. Match this setting under in-game **Options → Screen → Aspect Ratio** to correct the camera aspect ratio and HUD positioning. `Jogar-GT4.cmd --aspect 4:3` boots using 4:3 framing. Existing memory cards retain saved settings. 480p and 1080i modes must be verified during races; menus can use the standard display mode.

## Foundations, History, and Documentation

The active runtime foundation comes from the MIT-licensed [ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp) donor, adapted to native Windows and Vulkan. Our binary runs directly on Windows without a browser, WebAssembly, or embedded PCSX2. Credits, revisions, and licensing terms are documented in [THIRD_PARTY.md](native/THIRD_PARTY.md).

Development has progressed through boot and menus, Vulkan integration, audio and controls, genuine racing gameplay, and display modes. The original recompiler engine has been archived on the [`codex/archive-original-recomp-20261013`](https://github.com/moesuito/gt4-staticrecomp/tree/codex/archive-original-recomp-20261013) branch. The `main` branch is dedicated exclusively to the current native base.

- [STATUS](docs/STATUS.md): Confirmed milestones and pending tasks.
- [HYBRID-PORT-PLAN](docs/HYBRID-PORT-PLAN.md): Active Windows-first no-JIT optimization, GS graphics and future Android/iOS phases.
- [NATIVE-PORT-PLAN](docs/NATIVE-PORT-PLAN.md): Preserved game-scene renderer and eventual native migration alternative.
- [AGENT-WORKFLOW](docs/AGENT-WORKFLOW.md): Model routing, task contracts, review and future unattended queue policy.
- [EE-PRODUCER](docs/EE-PRODUCER.md): Verified CORE/ISO preparation, fresh EE generation and separate library build.
- [RENDER-CONTRACTS](docs/RENDER-CONTRACTS.md): Initial model/camera mapping, state effects and open coverage gates.
- [EE-CALLBACK-COVERAGE](docs/EE-CALLBACK-COVERAGE.md): Bounded offline recovery experiment and remaining integration gates.
- [EE-CANDIDATES](docs/EE-CANDIDATES.md): Separate runtime builds and owner-controlled qualification.
- [CAR-ASSET-RESEARCH](docs/CAR-ASSET-RESEARCH.md): Bounded volume-index evidence and the one-car extraction contract.
- [NENKAI-REFERENCES](docs/NENKAI-REFERENCES.md): Pinned local reference environment, format/source map and catalogue research limits.
- [ROADMAP](docs/ROADMAP.md): Earlier timing analysis and selective migration proposal, retained as technical context.
- [ARCHITECTURE](docs/ARCHITECTURE.md): Architecture of the current base.
- [BUILD](docs/BUILD.md): Build environment setup and verification procedures.
- [LEGACY](docs/LEGACY.md): Preservation and retrieval of archived work.

Active source code resides in `native/`, with tooling in `scripts/`, automated tests in `tests/`, and technical records in `docs/`. The active development fork is [`moesuito/gt4-staticrecomp`](https://github.com/moesuito/gt4-staticrecomp).
