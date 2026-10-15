# Post-processing and texture replacement study for GT4Recomp

Date: 2026-10-15. Read-only study. No checkout was modified, no builds, no game launches.
Labels: [C] Confirmed (read in source or file), [H] High confidence, [Hyp] Hypothesis, [U] Unknown.

## Sources examined

- paraLLEl-GS clone, HEAD `cc6184af7` (matches the pin in `docs/PARALLEL-GS-PLAN.md`, line 10):
  `C:\Antigravity\gt4-staticrecomp\private\slice157-gs-renderer-study\parallel-gs`
  - Granite submodule is EMPTY in that clone (`.gitmodules` points to github.com/Themaister/Granite). Granite
    sources were fetched from GitHub raw (`https://raw.githubusercontent.com/Themaister/Granite/master/...`)
    and are saved under `...\scratchpad\web\`.
- Port side (read only): `C:\Antigravity\gt4-wt-012\native\parallel_gs_backend.cpp`,
  `parallel_gs_backend.hpp`, `parallel_gs_frontend.inc`, `docs/PARALLEL-GS-PLAN.md`, `docs/GS-RENDERER-OPTIONS.md`.
- External (GitHub, read): PCSX2/pcsx2 default branch (not pinned), libretro/slang-shaders, Granite, AMD FidelityFX,
  NVIDIA FXAA 3.11 (copy in paulhoux/Cinder-Samples).

## Part A: post-processing on the final image

### A.0 What paraLLEl-GS already provides

- [C] No post filter in the core renderer. Its public interface ends at `vsync()` → `ScanoutResult`
  (`gs/gs_interface.hpp`, around lines 298 and 160-230). There is no FXAA, SMAA, CAS or CRT pass in `gs/`.
- [C] FSR1 + RCAS is used only by the optional PCSX2 integration and the replayer:
  - `README.md` line 30: "To upscale the anti-aliased content to screen, AMD FSR1 + RCAS can be used".
  - `README.md` line 85: PCSX2 "Bilinear filtering. The sharp bilinear option uses FSR1 + RCAS."
  - `tools/gs_stream_replayer.cpp` lines 137-210: `upscale_program` and `sharpen_program` on an RGBA8 render target.
  - `misc/arch-pkgbuild-pcsx2/0001-*.patch` line 1565-1566 and `0002-*.patch` (analog options) do the same in PCSX2.
- [C] CRT / composite / scanline pipeline exists in `analog/` (LGPL-3.0+ per SPDX in each file):
  - `analog/analog_video.hpp:95` `class CRTFilter`; `analog/analog_video.cpp` (1100 lines, composite encode/decode, bloom).
  - `analog/shaders/scan.frag` (122 lines): Gaussian scanline beam model, RGB aperture-grille mask (`grille()`),
    feedback and bloom threshold.
  - `analog/shaders/composite.comp` (476 lines), `iir.comp`, `sinc.frag`, `bloom.frag`.
  - Linked only into `parallel-gs-stream` (`tools/CMakeLists.txt`), not into our port.
- [C] README line 30 and the owner's mental model agree: paraLLEl-GS prefers SSAA over upscaling, and says SSAA
  "works much better with CRT emulation".
- [C] The port's present path (`C:\Antigravity\gt4-wt-012\native\parallel_gs_backend.cpp` lines 421-476) has no
  shader pass. It clears the swapchain, then `cmd->blit_image(...)` with `VK_FILTER_LINEAR` or `VK_FILTER_NEAREST`
  (lines 466-470). `present_filter()` (`parallel_gs_backend.hpp` lines 71-79) chooses LINEAR only for a
  non-integer high-resolution downscale. `high_resolution_target()` (lines 86-93) keeps the 447-line 480i rule.

### A.1 Build and shader pipeline

- [C] SPIR-V is pre-generated and committed. `gs/shaders/slangmosh.json` lists `sharpen_frag` (line 72),
  `upscale_frag` (line 80), `sharpen_vert`, `upscale_vert`, all taken from Granite `assets/shaders/post/ffx-fsr/`.
  Their SPIR-V is embedded in `gs/shaders/slangmosh.hpp` (`spirv_bank`, line 13; `request_shader` at lines
  19714-19718). The default build does not compile GLSL; regeneration is `gs/slangmosh.sh` (slangmosh tool).
- [C] Granite can also compile shaders at runtime when `GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER` is ON
  (`native/parallel_gs.cmake` lines 39-53; which branch is active depends on a condition I did not trace) [Hyp].
- [C] Our port already runs on the Granite `Vulkan::Device` (the WSI and swapchain are Granite's). A post pass
  therefore needs no new device or interop. It needs a render-target image plus a `request_program` /
  `request_shader` from the existing `Shaders<>` suite.
- [H] Adding FSR needs no new build tool. Add the shader names to our port's shader manifest or use the
  existing `gs/shaders/slangmosh.hpp` entries, and regenerate the pin only if the shader list changes.
  Recheck the CMake pin rules in `CLAUDE.md` before any change.

### A.2 Candidates

| Option | Source and licence | Where it runs | Interaction with SSAA / hi-res | Cost estimate (GPU) | Effort |
|---|---|---|---|---|---|
| (1) FSR1 EASU + RCAS | Granite `post/ffx-fsr/*` (Granite MIT, [C]). AMD FidelityFX-FSR repo is MIT ([C] via GitHub API `license.spdx_id`); `ffx_a.h` header is AMD "Copyright (c) 2021 ... Permission is hereby granted, free of charge" ([C]). | Two fragment passes on the scanout image, before the swapchain blit. The result goes into a 16:9 target, or EASU writes directly to the swapchain. | Best at 1x (640x448 to 1920x1080). At 4x+ SSAA or hi-res (1280x896) the source is already close to output size, so EASU is mostly a plain filter and RCAS is the useful part. | [Hyp] EASU at 1920x1080 about 0.1-0.3 ms on RX 9060 XT; RCAS about 0.05 ms. Measure with Granite timestamps. | 1-2 agent-days (shader wiring, intermediate image, integer-scale and 447-line rule, headless PNG diff). |
| (2) FXAA | NVIDIA FXAA 3.11 header (`nv_fxaa3_11.h`, copyright NVIDIA 2010-2011, "AS IS" warranty disclaimer, no explicit open licence in the header itself) [C]. Granite `post/fxaa.frag` has no attribution header [C] and is [Hyp] a derivative. PCSX2 `bin/resources/shaders/common/fxaa.fx` is GPL-3.0+ ([C], header) and MUST NOT be copied. | Single fragment pass on the scanout (or on the final frame). | Redundant at 4x+ SSAA, which already anti-aliases geometry. Only useful at 1x, where it blurs text. | [Hyp] under 0.1 ms at 1080p. | 1 agent-day to wire; licence clearance is the real cost. |
| (2b) SMAA (alternative to FXAA) | Granite `post/SMAA.hlsl` and `smaa_*.frag/vert` ([C] MIT-style grant in the header). | Three passes plus area and search textures. | Same redundancy as FXAA. | [Hyp] higher than FXAA (edge-detection, blend-weight passes). | 1.5-2 agent-days. |
| (3) CAS / simple sharpen | AMD FidelityFX-CAS repo spdx MIT ([C] via API). RCAS in (1) is the same family. | One compute or fragment pass. | Best after SSAA, which gives the cleanest edges to sharpen. | [Hyp] under 0.05 ms. | 0.5 agent-day as a standalone pass, or free as the RCAS half of (1). |
| (4a) CRT scanline (libretro `crt-lottes`) | libretro `crt/shaders/crt-lottes.slang`, header line 53: "PUBLIC DOMAIN CRT STYLED SCAN-LINE SHADER" ([C]). | One pass, or two with a Gaussian scan. | Pairs well with SSAA (per owner's README). | [Hyp] under 0.1 ms. | 1-2 agent-days (port push-constants, gamma, mask). |
| (4b) CRT from paraLLEl-GS `analog/` | LGPL-3.0+ (`analog/shaders/scan.frag`, `analog/analog_video.hpp`). Static library in its CMake. | Multi-pass composite plus scan plus bloom (`composite.comp` 476 lines, 1100-line C++). | Designed for SSAA input. | [Hyp] several passes; larger than (4a). | 2-3 agent-days, plus LGPL static-link obligations. |
| (4c) other libretro CRTs (crt-royale, crt-geom, ...) | Several are GPL. Check per file [Hyp]; only use files with a permissive header. | Varies. | Varies. | Varies. | Not recommended without a per-file licence check. |

Licence rule for an MIT exe: copying code from PCSX2 (GPL-3.0+) or paraLLEl-GS `analog/` (LGPL-3.0+)
into `native/` makes the release binary subject to those licences. Use these only as design references
(clean-room) or as a DLL, and keep notices in `native/THIRD_PARTY.md` (decision 0066 path). The NVIDIA
FXAA header has no explicit open licence in the copy I read [U]; treat it as not cleared until the owner
decides.

### A.3 Interaction with paraLLEl-GS super-sampling and hi-res scanout

- [C] Super-sampling is resolved inside paraLLEl-GS before `vsync()`. `high_resolution_scanout` needs at least
  4x SSAA (`gs/gs_interface.hpp` lines 188-193) and produces 1280x896 at 4x (`docs/PARALLEL-GS-PLAN.md` line 54).
  The port exposes this through `GT4_PGS_SSAA` and `GT4_PGS_HIRES` (`parallel_gs_frontend.inc` lines 34-60).
- [C] A post pass sees only the `ScanoutResult` image. It cannot see the 16 samples. So post-AA on top of 16x
  SSAA is redundant.
- [H] The 480i FFMD=1 case produces 447 active lines. `high_resolution_target()` keeps 448-line scale and a black
  bottom line (`parallel_gs_backend.hpp` lines 81-93). Any post pass must write only the `target` rectangle,
  not the full 16:9 rectangle, or it changes that output.
- [H] Order: scanout → optional post (sharpen or upscale) → blit to swapchain rect. The blit becomes a sampled
  copy when a post pass is on. Keep `VK_FILTER_NEAREST` for integer scales.
- [C] paraLLEl-GS `dynamic_super_sampling` (set in `native/parallel_gs_backend.cpp` line 248) can change SSAA at
  runtime. Post resources must be sized from the current scanout, not cached at startup (the replayer does
  this in `on_swapchain_created`, `gs_stream_replayer.cpp` lines 142-152).

### A.4 Recommended order

1. Keep nearest/linear as the default. No change to the donor path.
2. RCAS (CAS-like sharpen, MIT). It is cheap, and most useful after SSAA. This is the first candidate.
3. FSR1 EASU + RCAS for 1x and non-integer targets (MIT, Granite already has the shaders in SPIR-V).
   Measure with Granite timestamps first, as `docs/PARALLEL-GS-PLAN.md` requires.
4. CRT scanline as an optional setting, default off. Use the public-domain `crt-lottes` as a reference, or
   write a small MIT shader. Do not link `analog/` into the release exe.
5. FXAA or SMAA last, and only if the owner wants AA at 1x after clearing the FXAA licence. SSAA is the preferred AA.

Total for steps 2-4: about 3-5 agent-days [Hyp]. Step 5 adds 1-2 agent-days plus licence work.

## Part B: custom texture replacement and HD packs

### B.1 paraLLEl-GS support

- [C] No texture dump or replacement feature exists. Searches of `gs/`, `dump/`, `tools/` and `analog/` for
  replace, dump, png, hash and override found only internal hash-cache keys (`gs/gs_interface.cpp` lines
  1607-1621, `gs/page_tracker.cpp` lines 372-438) and GS register dumps in `dump/`.
- [C] `README.md` features and "Known missing features" do not list texture replacement.
- [U] Upstream issues: `gh search issues --repo Arntzen-Software/parallel-gs` returned no results for "texture
  replacement" or "texture dump" (issues may be disabled; README lines 124-127 say external contributions are not
  accepted). Blog posts by Hans-Kristian Arntzen were not searched.
- [C] Upstream HEAD still equals the pin `cc6184af7` at the time of this study.

### B.2 How PCSX2 identifies textures (design reference only, GPL-3.0+)

- [C] Key: `GSTextureCache::HashCacheKey` (PCSX2 `pcsx2/GS/Renderers/HW/GSTextureCache.h` lines 100-121;
  `GSTextureCache.cpp` lines 9287-9317). Fields are `TEX0Hash` (XXH3-based hash of the GS memory blocks of the
  texture, all mip levels when used), `CLUTHash` (palette bytes, zero for direct-colour textures), a masked TEX0
  (PSM, TW, TH), TEXA and the region size.
- [C] Lookup and injection: `GSTextureCache::LookupHashCache`, `GSTextureCache.cpp` lines 7070-7150. When
  `LoadTextureReplacements` is on, `GSTextureReplacements::LookupReplacementTexture` runs (line 7130). A hit
  returns a full RGBA replacement with `paltex = false` (line 7135), so the texture is sampled un-indexed.
- [C] Names and directories (`pcsx2/GS/Renderers/HW/GSTextureReplacements.cpp`): sub-folders `replacements` and
  `dumps` under the game's texture directory (lines 41-42, 269-271). Filenames follow
  `<TEX0Hash hex>-<CLUTHash hex>-<32-bit bits hex>[-r<w>x<h>...][-mipN].png` (format macros lines 35-40, writers
  lines 274-330, parser `ParseReplacementName` lines 200-260). Loaders: PNG and DDS
  (`GSTextureReplacementLoaders.cpp`).
- [C] Hash algorithm: XXH3 via `pcsx2/GS/GSXXH.h` (GPL-3.0+ wrapper around the BSD-licensed xxHash). The exact
  block walk (`HashTextureLevel`, `BlockHashState`) is in the same GPL files.

### B.3 Where a fork would hook into paraLLEl-GS

- [C] Texture key: `gs/gs_interface.cpp` lines 1607-1621 hash TEX0, TEX1, TEXA, mip and clamp state, palette bank
  and sample count. This is a STATE hash, not a content hash.
- [C] Miss path: `tracker.find_cached_texture` (line ~1704) → `renderer.create_cached_texture(desc)` (line 1709,
  implemented in `gs/gs_renderer.cpp` lines 1356-1420, RGBA8 UNORM at line 1372, `layers = samples + 1` for SSAA
  at 1375-1377) → `tracker.register_cached_texture` (line 1728) → `commit_cached_texture` (line 1743).
- [H] The natural hook is between lines 1709 and 1728: if a replacement file matches, bind a replacement image
  instead of the decoded image, and register that image under the same state hash.
- [C] Sampling: `gs/shaders/ubershader.comp` uses `tex_info.sizes` (line 1061), normalized UVs with `inv_scale`
  and `bias`, region clamps (`region_coords`, line 1058), a phase LUT for SSAA (lines 1160-1170), and bindless
  `textureLod` (lines 1208-1250).
- [Hyp] A replacement of a different pixel size may be sampled if `tex_info.sizes` and the `inv_scale` path carry
  the replacement's size. Nearest and SSAA-phase code assume the GS texel grid; they need an audit before this
  works.

### B.4 Core difficulties

1. Content hash. PCSX2 hashes the source GS memory on the CPU for each lookup. paraLLEl-GS keeps VRAM in GPU
   buffers (`vram16`, `vram32` in `ubershader.comp`). CPU reads need `begin_host_vram_access()` and a
   synchronous readback, and only single-page textures are promoted to CPU (`page_tracker.cpp` lines 446-455,
   498). A fork would hash on the GPU in a compute pass and read back an 8-byte result asynchronously. The page
   timeline makes that possible [Hyp]. Expect the first texture to be late by a few frames.
2. Sampling path. A replacement needs its own sampler path or a size-aware UV path (B.3). Mips must come from the
   pack, which PCSX2 handles with `-mipN.png`.
3. SSAA. SSAA textures are a separate layered image (`create_cached_texture` layers = samples+1). A
   replacement should bypass SSAA at first (1x only). `super_sampled_textures` is off by default
   (`gs/gs_interface.hpp` GSOptions).
4. Invalidation. Replacements must be dropped when the page tracker invalidates the source pages, which is what
   the cache already does for its own images (`page_tracker.cpp` line ~876, `invalidate_texture_hash`).
5. Compatibility with PCSX2 packs. Existing PCSX2 packs are keyed by PCSX2's exact hash (XXH3 over PCSX2's block
   layout). A fork can read the same PNG/DDS files only if it reproduces that hash exactly. That means
   reimplementing GPL code, which must be a clean-room rewrite from documented behaviour, not a copy [Hyp].
   Packs for GT4 SCUS-97328: a `gh search repos` query returned nothing [U]; no GT4 pack was found.
6. Dumping tools. paraLLEl-GS has none. A fork would need a dump-on-miss path and an offline tool that writes
   the same key names, so pack creators can reuse their work.

### B.5 Memory and performance

- [Hyp] RGBA8 at 2048x2048 is 16 MiB, or about 21 MiB with a full mip chain. A GT4 pack with many track and HUD
  textures would need an LRU bound (PCSX2 uses `MAX_HASH_CACHE_SIZE`). Expect VRAM and system memory use to rise
  by hundreds of MiB for large packs.
- [Hyp] Hash cost at 1x is small (a few KB to read per texture). The async readback is the main latency risk.

### B.6 Licensing and the PCSX2 alternative

- [C] PCSX2 hardware renderer has replacement support built in (`pcsx2/GS/Renderers/HW/GSTextureReplacements.*`,
  GPL-3.0+ per the files' SPDX headers, e.g. `GSXXH.h` line 2).
- [C] Using PCSX2 code in the exe would make the whole release GPL-3.0+. That conflicts with the MIT release model
  (commit `23769e2` "MIT license, decision 0067"). Clean-room reference is possible but the owner must decide.
- [C] paraLLEl-GS itself is LGPL-3.0+ (`COPYING.LGPLv3`, README lines 120-122). A fork that changes paraLLEl-GS
  internals must release those modifications under LGPL-3.0+ as source. A texture-replacement layer kept in our
  MIT port code, outside paraLLEl-GS internals, avoids that obligation.
- [C] `docs/GS-RENDERER-OPTIONS.md` says LGPL, MIT and GPL are "all compatible with the project's GPL direction".
  That predates the MIT licence commit and should be updated by the owner. (Not changed here.)

### B.7 Effort (agent-days, [Hyp])

- 1x-only proof of concept (PNG load keyed by a GPU-hashed TEX0 + CLUT state, one replacement path): 3-6 agent-days.
- Correct behaviour (SSAA bypass, mips, region keys, invalidation, LRU, dump tool): 2-4 weeks.
- Full PCSX2 pack compatibility (exact hash): 1-2 weeks more, plus the clean-room licence review.

## Recommendation

- Part A: adopt RCAS first (MIT, cheap, useful after SSAA), then FSR1 EASU + RCAS for 1x and non-integer targets.
  Measure cost with Granite timestamps. Defer CRT (use the public-domain `crt-lottes` or an own MIT shader, default
  off). Skip FXAA until the NVIDIA header licence is cleared; use SSAA for AA. Effort about 3-5 agent-days for
  steps 2-4.
- Part B: paraLLEl-GS has no texture replacement. Do not start a fork for HD packs yet. If the owner wants packs,
  the realistic routes are (a) a clean-room, state-hash replacement layer in our port (1x first, 3-6 agent-days
  PoC, weeks for correctness) that does NOT reuse PCSX2's exact hash, or (b) the PCSX2 hardware renderer, which
  supports packs natively but forces GPL-3.0+ on the release. The owner should decide (b) against the MIT model
  before any work.

## Unknown and open items

- [U] Whether upstream paraLLEl-GS issues or blog posts discuss texture replacement or FXAA/CRT.
- [U] NVIDIA FXAA 3.11 licence text for redistribution (only the disclaimer header was read).
- [U] Existence of any public GT4 texture pack.
- [U] The exact GPU cost of each pass (no measurements; all cost numbers are [Hyp]).
- [U] Exact `ParseReplacementName` 32-bit field semantics (read only the format string).

## Files and URLs

- paraLLEl-GS (pinned clone): `C:\Antigravity\gt4-staticrecomp\private\slice157-gs-renderer-study\parallel-gs`
  - `README.md` 27-30, 82-85, 120-122
  - `analog/analog_video.hpp` 95; `analog/shaders/scan.frag`; `analog/shaders/composite.comp`
  - `gs/shaders/slangmosh.json` 72-84; `gs/shaders/slangmosh.hpp` 13, 19714-19718
  - `gs/gs_interface.hpp` 160-230, 298, GSOptions
  - `gs/gs_interface.cpp` 1607-1621, 1704-1745
  - `gs/gs_renderer.cpp` 1356-1420, 1473-1500
  - `gs/page_tracker.cpp` 372-498, ~876
  - `gs/shaders/ubershader.comp` 1058-1250
  - `tools/gs_stream_replayer.cpp` 137-210
  - `tools/CMakeLists.txt` (analog link), `COPYING.LGPLv3`
- Port: `C:\Antigravity\gt4-wt-012\native\parallel_gs_backend.cpp` 248, 263-270, 421-476;
  `parallel_gs_backend.hpp` 56-93; `parallel_gs_frontend.inc` 34-60, 119-125;
  `docs\PARALLEL-GS-PLAN.md` 10, 54, 62-88; `docs\GS-RENDERER-OPTIONS.md` (licence sentence, Recommendation 3).
- Granite (fetched, saved under `...\scratchpad\web\`):
  `https://github.com/Themaister/Granite/blob/master/assets/shaders/post/ffx-fsr/` (sharpen.frag, upscale.frag,
  ffx_fsr1.h), `.../post/fxaa.frag`, `.../post/SMAA.hlsl`, `LICENSE` (MIT, "Copyright (c) 2017-2026 Hans-Kristian Arntzen").
- AMD FidelityFX-FSR (MIT): `https://github.com/GPUOpen-Effects/FidelityFX-FSR`. FidelityFX-CAS (spdx MIT):
  `https://github.com/GPUOpen-Effects/FidelityFX-CAS`.
- NVIDIA FXAA 3.11 header copy: `https://github.com/paulhoux/Cinder-Samples/blob/master/FXAA/assets/Fxaa3_11.h`
- PCSX2 (default branch, not pinned): `https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/Renderers/HW/GSTextureReplacements.cpp`,
  `.../GSTextureReplacements.h`, `.../GSTextureCache.cpp` (7070-7150, 9287-9339), `.../GSTextureCache.h` (100-121),
  `.../pcsx2/GS/GSXXH.h`, `bin/resources/shaders/common/fxaa.fx` (GPL-3.0+).
- libretro CRT (public-domain header): `https://github.com/libretro/slang-shaders/blob/master/crt/shaders/crt-lottes.slang` (line 53).
