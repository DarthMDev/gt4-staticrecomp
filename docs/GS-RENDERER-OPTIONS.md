# GS Renderer Options: paraLLEl-GS, PCSX2 Hardware, ARMSX2

2026-10-15. Research only; nothing integrated. Scratch clone of paraLLEl-GS at
`private/slice157-gs-renderer-study/parallel-gs` (HEAD `cc6184a`).
[C] = Confirmed, [H] = Hypothesis.

## paraLLEl-GS (github.com/Arntzen-Software/parallel-gs)

- [C] LGPL-3.0+ (dual licensing "may be possible"); Granite engine (MIT) as a
  submodule; external contributions not accepted.
- [C] Standalone Vulkan compute GS emulator, not GSdx: 32x32 binning, barycentric
  rasterization in one ubershader, bindless textures, CLUT cache. Super-sampling
  2x/4x/8x/16x, weave deinterlacer, automatic progressive for FFMD=0, CRTC field
  blend. Hacks: disable mipmaps, unsynced readbacks, backbuffer promotion, blend
  demotion. Explicitly not bit-accurate on interpolation.
- [C] Readbacks are page-tracked with per-page timeline semaphores: a CPU read
  waits only for the GPU work that touched those pages (our renderer waits for
  everything). Relevant to the night-race readback wait.
- [C] Interface (`gs/gs_interface.hpp`): `gif_transfer`, `write_register`,
  `map_vram_write`/`end_vram_write`/`map_vram_read`, `read_transfer_fifo`, `flush`,
  `vsync` → `ScanoutResult`, privileged register state, signal/finish/label
  callbacks, `set_super_sampling_rate`, `set_hacks`.
- [C] Requires descriptor indexing, timeline semaphores, 8/16-bit storage,
  `shaderInt16`, scalar block layout, full subgroups with size control 16-64.
  No Android/iOS code or testing; mobile status Unknown.
- [C] Shipped in the libretro LRPS2 core; PCSX2 integration is out-of-tree patches.
- [C] No GameDB or GT4-specific hacks; channel-shuffle detection exists.
- [H] Our donor GS front end (`write`, `write_packed`, `write_image`, `read_image`,
  signals, privileged registers) maps closely onto its interface; vsync/scanout
  is the main gap. Its code expects a Granite `Vulkan::Device`, so it needs a
  device shim or porting onto our Vulkan context.

## PCSX2 hardware renderer (GSdx lineage)

- [C] About 38k lines in the HW and Vulkan renderer directories (more with GS core).
- [H] Fastest, cheapest upscaling, texture replacement, the most GT4 coverage
  through GameDB hacks; large and coupled to PCSX2 GS-thread state.

## ARMSX2

- [C] No paraLLEl-GS. Backends include Vulkan (with frame generation), Metal,
  OpenGL and others. Mobile work: GPU driver-bug profiles (Adreno, Mali, PowerVR,
  Xclipse, Apple) and a pass scheduler that batches draws per render target for
  tile-based GPUs. ARMSX2 itself uses an ARM64 JIT (not usable here); its GS code
  is separable.

## Recommendation

1. **Accurate PC renderer:** spike paraLLEl-GS as an additional backend behind our
   GS front end, keeping the donor renderer as fallback and reference. Gates:
   night-race GPU wait, lens-flare readback stall, image diff against the donor,
   SSAA quality and cost. Risks: Granite shim effort, not bit-exact, GT4 hacks
   to re-derive.
2. **Light/mobile renderer:** stay with the PCSX2 hardware renderer plan
   (`docs/HW-RENDERER-PLAN.md`), borrowing ARMSX2's driver profiles and pass
   scheduler; paraLLEl-GS is unproven on mobile.
3. **Licensing (updated 2026-10-15):** the project is MIT and its release
   executable contains translated game code (decision 0067). LGPL-3.0+ code
   (paraLLEl-GS) ships as a replaceable DLL. GPL-3.0 code (PCSX2 hardware
   renderer, ARMSX2) cannot go into that executable. Keep notices and record
   provenance in `native/THIRD_PARTY.md`.
