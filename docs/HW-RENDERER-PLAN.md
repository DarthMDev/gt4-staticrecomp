# GT4-Specialist Hardware Renderer (based on PCSX2) — Study

2026-10-15, slice 151. Read-only study; nothing imported. GPL-3.0 reuse is
accepted ([decision 0066](decisions/0066-gpl-reuse-accepted.md)). Sources:
PCSX2 `aa7ab43` (private checkout) and the owner's ARMSX2 checkout.

## PCSX2 GS architecture (Confirmed)

- Entry points in `pcsx2/GS/GS.cpp`: GIF transfers per path, CSR writes, FIFO
  readback, local-memory reads, vsync; factories for renderer (HW/SW/Null) and
  device (Vulkan, Metal on Apple).
- Layers: `GSState` (GIF registers, transfers, `GSLocalMemory`) → `GSRenderer`
  (vsync, merge, interlace, upscale) → `GSRendererHW` (draws) with
  `GSTextureCache` → `GSDevice` → `GSDeviceVK` (6.7k lines). Vulkan GLSL shaders
  (`tfx.glsl` about 2k lines) are compiled at runtime with shaderc; Metal shaders
  at build time.
- GT4 hardware fixes come from the GameDB (half-pixel offset 4, auto flush) and a
  CRC-keyed hack (`GSC_PolyphonyDigitalGames`, channel shuffle).
- SIGNAL/FINISH/LABEL interrupt state lives on the EE/GIF side in PCSX2 too, which
  matches the donor design.

## Size and dependencies (Hypothesis)

About 45-55k lines of C++ plus 4.6k lines of GLSL (GS core, common renderer, HW
renderer, texture cache, Vulkan device), plus PCSX2 `common/` utilities and fmt,
zstd, xxhash, libpng, shaderc and VMA. OSD/ImGui, capture/dumps, DX11/DX12/
OpenGL/SW backends, multi-ISA and post filters can be stubbed or dropped.

## Integration (Hypothesis)

Keep the donor DMA/VIF/GIF units and interrupt handling. Replace the donor GS
front end (`runtime/src/gs/gs.cpp`) with an adapter that mirrors privileged
registers into a PS2-layout block, forwards GIF data per path and calls vsync.
EE readbacks map to PCSX2's FIFO/readback paths.

## Mobile (Confirmed from ARMSX2)

ARMSX2 ships a Metal backend (usable on iOS; MetalFX optional) and Android
changes: a GPU driver-bug profile database (Adreno/Mali/PowerVR), a tile-friendly
pass scheduler, a framebuffer-fetch policy for Adreno tile memory, interlace and
presentation policies, and a front/back GS split still in progress
(`GSBackQueue.h`), relevant to our [async design](GS-ASYNC-DESIGN.md).

## GT4 specialization

First add a census in the current GS front end: PRIM flags, texture formats and
CLUT modes, mipmap settings, alpha blend equations, depth/alpha tests,
dithering, frame/depth formats, transfer directions, render-to-texture and
channel-shuffle shapes, interlace registers. Remove PCSX2 paths GT4 never uses.

## Validation

The current compute renderer at 1x is the reference: per-vsync image diff with a
defined tolerance (filtering/dithering). PCSX2 `.gs` dumps and its headless
runner can replay identical input. Enable upscaling only after 1x is clean.

## Phases (Hypothesis, one engineer)

| Phase | Work | Estimate |
|---|---|---|
| P0 | GS feature census and dump/replay harness, no PCSX2 code | 1-2 weeks |
| P1 | Build GSState + HW renderer + texture cache + Vulkan device offline with stubs; replay GT4 dumps | 3-5 weeks |
| P2 | Adapter from the donor GIF/GS front end, readbacks, 1x pixel diff | 4-6 weeks |
| P3 | Upscaling, dithering control, deinterlacing, mipmapping, GT4 fixes | 2-3 weeks |
| P4 | Android build and device tests | 3-4 weeks |
| P5 | iOS Metal backend from ARMSX2 | 3-4 weeks |

Risks: GPL distribution obligations, size/churn of `GSState`/texture cache,
PCSX2 threading assumptions, shaderc size and compile time on mobile, mobile
driver bugs, and the native-scale assumptions in current fixtures.
