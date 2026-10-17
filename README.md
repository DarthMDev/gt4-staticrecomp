<div align="center">

# GT4Recomp

**Gran Turismo 4, recompiled to run natively on PC.**<br>
No JIT, no emulator window, full speed.

![60 FPS](https://img.shields.io/badge/60%20FPS-100%25%20game%20speed-2ea44f?style=for-the-badge)
![No JIT](https://img.shields.io/badge/guest%20JIT-none-2ea44f?style=for-the-badge)
![Windows](https://img.shields.io/badge/Windows%20x64-Vulkan-1f6feb?style=for-the-badge)
![MIT](https://img.shields.io/badge/license-MIT-1f6feb?style=for-the-badge)

![EE](https://img.shields.io/badge/EE%20(main%20CPU)-100%25%20AOT-2ea44f)
![VU0 macro](https://img.shields.io/badge/VU0%20macro-100%25%20AOT-2ea44f)
![VU1 micro](https://img.shields.io/badge/VU1%20microcode-100%25%20AOT%20%C2%B7%202%2C048%2F2%2C048-2ea44f)
![VU0 micro](https://img.shields.io/badge/VU0%20microcode-100%25%20AOT%20%C2%B7%2014%2F14-2ea44f)
![IOP](https://img.shields.io/badge/IOP%20system%20libraries-native-2ea44f)
![GS](https://img.shields.io/badge/GS-PCSX2%20GS%20%2B%20CoreGS%201.1-1f6feb)

![Time Trial](https://img.shields.io/badge/time%20trial-60%20FPS-2ea44f)
![AI race](https://img.shields.io/badge/AI%20race-60%20FPS-2ea44f)
![Night race](https://img.shields.io/badge/night%20race-60%20FPS-2ea44f)
![Rally](https://img.shields.io/badge/rally-60%20FPS-2ea44f)
![Showroom](https://img.shields.io/badge/showroom-60%20FPS-2ea44f)
![Frame pacing](https://img.shields.io/badge/frame%20pacing-16.7%20ms%20flat-2ea44f)
![Licenses](https://img.shields.io/badge/license%20tests-playable-2ea44f)

</div>

![Toyota Supra on the Nordschleife at 60 FPS](docs/images/race-nordschleife-60fps.webp)

GT4Recomp is a static recompilation of **Gran Turismo 4 (NTSC-U, SCUS-97328)** for **Windows x86-64**.
The game's own code is translated ahead of time into C++ and compiled to native machine code; the PS2
hardware it talks to is reproduced in software, and graphics run on **Vulkan**. Nothing generates code
while the game runs, which is also what makes future **Android and iOS** builds possible.

## Where we are

- **The whole game runs natively** at **60 FPS and full game speed** with a flat **16.7 ms** frametime:
  Time Trial, AI races, night races, rally and the car showroom (Ryzen 7 5700X + RX 9060 XT).
- **All game code is precompiled:** the main CPU (EE), every VU1 and VU0 microprogram; no interpreter on
  the normal path.
- **Graphics:** the **PCSX2 GS** renderer runs in a separate process (`gt4-gs.exe`), with upscaling,
  deinterlaced menus and a performance overlay; **CoreGS 1.1** is built in as the fallback.
- **Races in 480p**, 16:9 with borderless fullscreen; crash-safe memory card.
- **Next:** a one-click **launcher and installer**, then the first stable release, **1.0.0**.

## Performance

Same PC, same scene (Time Trial, Nordschleife, Supra):

| Step | Frame time | FPS | Game speed |
|---|---|---|---|
| Starting point (VU1 interpreted) | 33.3 ms | ~30 | 51% |
| VU1 compiled ahead of time | 22.0 ms | ~45 | 75% |
| + asynchronous GPU submission | 16.7 ms | 60 | 100% |
| + single-clock frame pacing | **16.67 ms flat** | **60** | **100%** |

<img src="docs/images/frametime-flat.webp" alt="Flat frametime graph at 60 FPS" width="460">

## Known issues

- **Some license demos and menu backgrounds do not play**; every license test is playable. This matches
  PCSX2 with default settings ([why](docs/LICENSE-DEMO-CIPHER.md)).
- **The MSI Afterburner / RivaTuner overlay does not show** with the PCSX2 GS (suspected: its Arbitrary
  Code Guard protection). The built-in performance overlay works.
- **Menus are interlaced** like the original game (races are 480p); the PCSX2 GS deinterlaces them.
- Racing wheels and long sessions are not fully qualified yet.

## Roadmap

| Phase | Platform | Renderer |
|---|---|---|
| **1** (current) | Windows PC | PCSX2 GS (separate process) · CoreGS 1.1 built in · CoreGS 2.0 planned |
| **2** | Android | ARMSX2 GS pipeline (Vulkan) |
| **3** | iOS | ARMSX2 GS pipeline (Metal) |

Every piece of work is a numbered task in [TASKS.md](TASKS.md), tested in game before it is merged.

## How it works

The EE game code is translated ahead of time into C++ and compiled for x86-64. The VU1 microcode, stored
encrypted inside the executable, is recovered and compiled offline (all 2,048 entry points), verified
against a reference interpreter on real game state; the 14 VU0 microprograms are compiled the same way.
IOP system services are native. Frames are paced by one high-resolution clock with rate-controlled audio.
This is a **hybrid recompilation**: PS2 hardware is still modeled in software, so we do not claim zero
emulation or full PS2 fidelity. Details: [Architecture](docs/ARCHITECTURE.md).

## Playing and development

The project currently needs a **development environment with your own game files** (see the
[Build guide](docs/BUILD.md)); the planned launcher replaces this with an installer and a Play button.
This repository does not include game files, disc images, BIOS dumps or save files.

| Key | Action |
|---|---|
| Arrow keys | Navigate / Steer |
| Z | Confirm / Accelerate |
| A | Brake |
| S / X | Triangle / Circle |
| Enter | Start / Pause |
| Alt+Enter, F11 | Borderless fullscreen |

## Credits

| Project | Author | What it gives this project |
|---|---|---|
| [ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp) (MIT) | snuri00 | The runtime foundation: EE-to-C++ translator, PS2 hardware models, the GS compute renderer and the original VU1 compiler. |
| [gt4-decomp](https://github.com/gustavocanepa/gt4-decomp) (MIT) | gustavocanepa | GT4's decompilation: function names, class layouts and engine notes ([identity](docs/DECOMP-IDENTITY.md)). |
| [Nenkai's GT research](https://github.com/Nenkai) | Nenkai | File formats and the VU1 microcode encryption scheme ([details](docs/VU1-MICROCODE-SOURCE.md)). |
| [GT4FS](https://github.com/Razer2015/GT4FS) | Razer2015 | GT4 volume file-system research. |
| [PCSX2](https://github.com/PCSX2/pcsx2) (GPL-3.0) | PCSX2 team | Decades of PS2 knowledge; its GS runs only in the separate `gt4-gs.exe`. |
| [ARMSX2](https://github.com/ARMSX2/ARMSX2) (GPL-3.0) | ARMSX2 team | The mobile GS pipeline planned for Android and iOS. |
| [paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs) · [Granite](https://github.com/Themaister/Granite) | Hans-Kristian Arntzen | Renderer seam and capture code (the renderer itself was dropped). |
| [naga](https://github.com/gfx-rs/wgpu/tree/trunk/naga) · [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | gfx-rs · Khronos | Shader compilation at build time · Vulkan API headers. |

Exact revisions and licenses: [THIRD_PARTY.md](native/THIRD_PARTY.md).

## License and legal

- GT4Recomp's own code is [MIT](LICENSE). The game executable does not contain or link GPL code; the
  PCSX2 GS (GPL-3.0-or-later) runs only in the separate `gt4-gs.exe`, built from
  [`moesuito/gt4recomp-gs`](https://github.com/moesuito/gt4recomp-gs) with its source published on each release.
- You bring your own disc. The project does not include Gran Turismo 4, disc images, BIOS files, keys or saves.
- "Gran Turismo" and "PlayStation" are trademarks of Sony Interactive Entertainment. Gran Turismo 4, its
  logos and artwork belong to Sony Interactive Entertainment and Polyphony Digital. GT4Recomp is an
  unofficial fan project, not affiliated with or endorsed by either.

## Documentation

[TASKS](TASKS.md) · [STATUS](docs/STATUS.md) · [BUILD](docs/BUILD.md) · [ARCHITECTURE](docs/ARCHITECTURE.md) ·
[Renderers](docs/GSDX-PLAN.md) · [Release 1.0.0 plan](docs/RELEASE-1.0.0-PLAN.md) ·
[Decisions](docs/decisions/) · [Journal](docs/journal/)
