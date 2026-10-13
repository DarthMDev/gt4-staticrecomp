# GT4Recomp

**Gran Turismo 4 on PC. Preserving the original experience and paving the way for what comes next.**

GT4Recomp is a static recompilation project targeting **Gran Turismo 4 USA v2.00 for Windows x86-64**. Our goal is to bring the PlayStation 2 classic to PC with stable performance, configurable graphics, and a codebase structured for research, modding, and future expansion.

**The game already boots, navigates menus, and enters real racing gameplay.** The current base features Vulkan rendering, Windows audio, keyboard support, an XInput controller implementation, and persistent memory card storage. We are in an experimental development phase: the immediate challenge is turning this milestone into a reliable gameplay experience.

[Detailed Status](docs/STATUS.md) · [Development Roadmap](docs/ROADMAP.md) · [Build Guide](docs/BUILD.md)

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

Manual testing has shown 480p to be the most stable mode so far. Interlaced modes function, but their visual and temporal fidelity are not yet fully qualified.

## The Experience We Want to Build

The primary objective is **playing GT4 stably and at correct game speed on PC**. Building on that foundation, we plan to develop:

- **FPS independent of game speed:** Rendering at 144 FPS or monitor refresh rate while preserving original physics, timers, and game pace.
- **Configurable internal resolution:** 1920×1080, 2560×1440, 3840×2160, and beyond, with progressive-scan output.
- **Ultrawide gameplay:** Adapted camera projection and repositioned HUD, maintaining proportions and legibility.
- **Graphical enhancements:** Room to upgrade visual effects, textures, and Vulkan renderer features.
- **Simple configuration:** A launcher to configure video and input settings before launching the game.
- **Readable and modifiable code:** Ongoing decompilation to facilitate research, bug fixes, and future mods.

These are **development goals**, not currently available features. The internal rendering scale remains 1x; resizing the window does not increase the internal resolution. Likewise, decoupled 144 FPS rendering independent of physics is not yet implemented.

## How It Works

The main game code is translated ahead-of-time (AOT) into C++ and compiled to run natively on **x86-64**. Host PC integration provides a native Windows window, **Vulkan**, audio, and input.

The current architecture is a **hybrid recompilation**: IOP and Vector Units (VU) remain interpreted, and PS2 hardware services are software models. The current Vulkan backend also preserves the constraints of the original graphics subsystem. This does not prevent evolving the presentation, timing model, or renderer; subsystem migrations will be undertaken as proven necessary.

For implementation details, see [Architecture](docs/ARCHITECTURE.md).

## Next Steps

1. Establish fully reproducible ahead-of-time recompilation and clean build generation.
2. Qualify controls, audio, saves, and stability across more real-world gameplay scenarios.
3. Profile execution bottlenecks, improving performance and frametime consistency.
4. Decouple game logic timing from presentation; generate intermediate frames between simulation steps to deliver high-refresh rendering.
5. Advance progressive output, internal resolution scaling, and graphical features.

We work in small, incremental steps with recorded evidence and verification before merging changes. The [Roadmap](docs/ROADMAP.md) outlines the planned sequence, dependencies, and validation criteria. There is no announced timeline for completing these milestones.

## Testing and Development

The project currently requires a **development environment with prepared private assets**. This repository does not distribute game assets, ISOs, BIOS dumps, decompiled code, prebuilt binaries, or save files. The current build also relies on a pre-generated private recompiled library; a clean end-to-end extraction pipeline from ISO remains future work.

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
- [ROADMAP](docs/ROADMAP.md): Roadmap for framerate decoupling, rendering upgrades, and native subsystem evolution.
- [ARCHITECTURE](docs/ARCHITECTURE.md): Architecture of the current base.
- [BUILD](docs/BUILD.md): Build environment setup and verification procedures.
- [LEGACY](docs/LEGACY.md): Preservation and retrieval of archived work.

Active source code resides in `native/`, with tooling in `scripts/`, automated tests in `tests/`, and technical records in `docs/`. The active development fork is [`moesuito/gt4-staticrecomp`](https://github.com/moesuito/gt4-staticrecomp).
