# PCSX2 hardware GS renderer as a GT4Recomp renderer option: feasibility study

Date: 2026-10-15. Read-only study. No checkout was modified, nothing was built, compiled or launched, and no network was used.

Labels: **Confirmed** = read in source or docs during this study; **High confidence** = strongly implied by read code; **Hypothesis** = estimate or inference; **Unknown** = not established.

Sources read:
- PCSX2 source: `C:\Antigravity\gt4-staticrecomp\private\tooling\pcsx2-slice101-source`, git HEAD **`aa7ab4306e269075784c7ac3eb4b45e6e6c53445`** ("R5900: Early return when Rt part of register is zero in LD."), the commit pinned by decision 0066 and `native/THIRD_PARTY.md`.
- `C:\Antigravity\gt4-staticrecomp\private\pcsx2` is **not** PCSX2 source. It is a runtime data directory (BIOS, config, save states), so it was not used.
- Docs in `gt4-wt-012`: `docs/PARALLEL-GS-PLAN.md`, `docs/GS-RENDERER-OPTIONS.md`, `docs/POSTFX-TEXTURES-STUDY.md`, `native/parallel_gs_frontend.inc`. The backend `.cpp/.hpp` and `parallel_gs.cmake` were not re-read; the plan's descriptions were used for them.
- Docs in the main checkout: `docs/HW-RENDERER-PLAN.md`, `docs/PCSX2-LESSONS.md`, `docs/decisions/0066-gpl-reuse-accepted.md`, `docs/decisions/0067-mit-license-and-release-model.md`, `native/THIRD_PARTY.md`.
- Not re-read: `scripts/prepare_external_vulkan.py` (splice mechanics). Its behavior is taken from `parallel_gs_frontend.inc`, which states it is "Spliced into the derived gs.cpp by scripts/prepare_external_vulkan.py --parallel-hooks".

---

## Verdict

**Feasible, but expensive and licence-bound.** Technically, the PCSX2 HW renderer is the strongest upscaling and texture-replacement path available. It is about 45k lines of core code (see section 1), plus about 2k lines of Vulkan GLSL for `tfx`. It can be cut to an in-process Vulkan HW renderer with no PCSX2 JIT. The real blocker is licensing, not code. Our executable contains translated game code, so GPL-3.0+ code cannot be linked into it (decision 0067).

**Recommendation:** build it as a **separate GPL-3.0+ renderer executable** (licence route (a) in section 5), fed by our GS front end over shared memory. Keep paraLLEl-GS as the LGPL DLL it already is, and keep the donor renderer as fallback.

**Effort:** roughly 4-6 engineer-months to a GT4-validated 1x-to-6x renderer, plus about 3-5 weeks for the process split. These are Hypothesis figures for one engineer. They are an extension of the HW-RENDERER-PLAN estimate (P0-P3 about 10-15 weeks), and that earlier figure looks optimistic once the Host/MTGS coupling and the process split are counted.

---

## 1. What the PCSX2 GS hardware renderer depends on

### Code layout and size (Confirmed by line count, `pcsx2/GS` at aa7ab43)

| Area | Files | Lines (.cpp/.h/.hpp/.inl) |
|---|---|---|
| `pcsx2/GS/*.cpp,h` (GSState, GSLocalMemory, GS.cpp, GSCapture, GSDump, GSPng, GSLzma, GSPerfMon, vector types, GSRegs, GSTables, GSXXH, GSRingHeap...) | 49 | 32,964 |
| `Renderers/HW` (GSRendererHW 11,230; GSTextureCache 9,329; GSHwHack 1,529; GSTextureReplacements*, GSVertexHW.h) | 11 | 25,606 |
| `Renderers/Vulkan` (GSDeviceVK 6,735; GSTextureVK; VKLoader; VKSwapChain; VKBuilders; VKStreamBuffer; vk_mem_alloc) | 16 | 12,566 |
| `Renderers/Common` (GSDevice 2,053; GSRenderer 1,253; GSTexture; GSDirtyRect; GSVertex; GSFunctionMap...) | 16 | 7,148 |

Top-level `GSState.cpp` is 7,652 lines and `GSLocalMemory.cpp` is 814.

Shaders (Confirmed): `bin/resources/shaders/vulkan/` holds 3,552 lines of GLSL, of which `tfx.glsl` is 1,990 lines. The other files are `convert`, `merge`, `interlace`, `present`, `shadeboost`, `cas` and `imgui`.

### Minimum subset (Hypothesis, derived from the above)

Keep:
- **GSState / GSLocalMemory / GSDrawingContext / GSDrawingEnvironment / GSClut / GSRegs / GSTables / GSVector / GSVector4i / GSVector8 / GSBlock / GSRingHeap / GSUtil**: GIF register state, transfers, CLUT and swizzle. About 12-15k lines of the top-level set.
- **Renderers/HW**: GSRendererHW, GSTextureCache, GSVertexHW.h, GSHwHack (GameDB hacks), and GSTextureReplacements* (texture replacement).
- **Renderers/Common**: GSRenderer (merge, interlace, upscale selection), GSDevice, GSTexture, GSVertex, GSDirtyRect, GSFunctionMap.
- **Renderers/Vulkan**: GSDeviceVK, GSTextureVK, VKBuilders, VKStreamBuffer, VKLoader, vk_mem_alloc.
- **Shaders**: the Vulkan `tfx.glsl` and the post shaders listed above.

Drop:
- **SW renderer** (`Renderers/SW`, including `GSDrawScanlineCodeGenerator.all.cpp` and the arm64 code generators). This is runtime x86/ARM code generation, which the no-JIT rule forbids. **Confirmed** that the code generator is present in the tree.
- **DX11, DX12, OpenGL, Metal, Null renderers**, `GSCapture`, `GSDump`, `GSPng`, `GSLzma`, `GSPerfMon`, `GSXXH` (replace with an equivalent hash), `MultiISA`, and the `GSShaderCompileIndicator` / ImGui overlay.
- `GS.cpp` as a whole (1,452 lines). Replace it with our own thin front end (section 2).

Estimated extraction: about 45-55k lines kept, out of roughly 78k in the four trees. This matches the HW-RENDERER-PLAN figure.

### Host and threading coupling (Confirmed)

- **Host and config.** `GS.cpp` has 98 references to `Host::`, `MTGS::`, `EmuConfig`, `GameDB`, `VMManager` and similar. `GSRenderer.cpp` has 55 and `GSDeviceVK.cpp` has 45 (44 of them ImGui or Host). Everything in the HW path reads `GSConfig` (`GSConfig.UserHacks_*` is about 95 references in `Renderers/HW`).
- **Threading.** `GS.cpp` dispatches through `MTGS::RunOnGSThread` (10 calls), `MTGS::WaitGS` and `MTGS::ApplySettings`. The renderer assumes it runs on the GS thread, and the EE thread talks to it through the MTGS ring (see `PCSX2-LESSONS.md`).
- **Filesystem.** `GSTextureReplacements.cpp` resolves texture folders with `EmuFolders::Textures` (lines 271, 281) and `FileSystem::` calls (about 9 references).
- **Licence headers.** 173 files in `pcsx2/GS` carry `SPDX-License-Identifier: GPL-3.0+` (167) or `GPL-3.0` (6). No LGPL header was found under `pcsx2/GS`. **Confirmed.** Some `common/` helpers may be LGPL; that was not checked.

### Build system (Hypothesis)

PCSX2 builds with CMake and its own `common/` library. Its Vulkan path loads the library dynamically (`VKLoader.cpp:46`, `LoadVulkanLibrary`) and does not need volk. Unknown: whether the GS tree needs `fmt`, `zstd`, `xxhash`, `libpng`, `shaderc` (the HW-RENDERER-PLAN says runtime GLSL goes through shaderc; not re-verified here), or VMA beyond the bundled copy. Our build is clang-cl + Ninja. Compiling PCSX2 code with clang-cl is probably workable, but it needs a sweep for MSVC-only idioms. Estimate: 1-2 weeks for an isolated static library that compiles and links with stubs for `common/` and `Host`.

Note: the GS tree is C++20 with `std::bit` and similar, and our build is C++20, so the language level looks compatible (High confidence).

---

## 2. Interface mapping from our GS front end

### Our front end today (paraLLEl-GS path, Confirmed from `parallel_gs_frontend.inc`)

The hooks are: `renderer_mode()` (`GT4_RENDERER` = donor | parallel), `parallel_gif_transfer(path, data, qwc)`, `parallel_gif_busy`, `parallel_reset`, `parallel_reset_gif`, `parallel_read_image` (read FIFO), `parallel_wait`, `parallel_vblank` (advances the frame context), `parallel_attach_window`, `parallel_resize_window`, `parallel_present`, `parallel_scanout_frame` (dumps), `parallel_mirror_privileged(address, value)`, `parallel_sync_memory` (VRAM copy-back for the donor decoder) and `note_crt_mode` (SetGsCrt to SMODE1/SMODE2). Signals, finish and label are forwarded through `SignalHandler`.

### GSdx entry points (Confirmed, `pcsx2/GS/GS.h:55-116`)

| Our hook | PCSX2 entry point | Notes |
|---|---|---|
| open / close / reset | `GSopen`, `GSclose`, `GSreset`, `GSreopen` | `GSopen` takes `Pcsx2Config::GSOptions`, a renderer type, and device/window info. We would pass a shim config. |
| GIF path data | `GSgifTransfer(mem, size)`, `GSgifTransfer1/2/3` | Path 1, 2 and 3 map to our paths. Packet format matches the GIF tag stream. |
| Privileged registers | `GSwriteCSR`, `GSgifSoftReset` | Privileged writes (PMODE, SMODE1/2, DISPFB, DISPLAY) are written inside `GSState` through the register path. Our mirror function feeds those. |
| Read FIFO / readback | `GSInitAndReadFIFO`, `GSReadLocalMemoryUnsync` | Our `read_image` maps to `GSInitAndReadFIFO`. |
| VSync | `GSvsync(field, registers_written)`, `GSThrottlePresentation`, `GSPresentCurrentFrame` | `GSvsync` is the scanout trigger. |
| Presentation | `GSDeviceVK` + `VKSwapChain` (own instance, device and swapchain) | See below. |
| Internal resolution | `GSgetInternalResolution`, `GSgetDisplayMode`, `GSGetMaxUpscaleMultiplier` | Used to size the output. |
| Hooks/config | `GSUpdateConfig`, `GSConfig.UserHacks_*` | GameDB hacks arrive here. |

### Presentation and the device (High confidence, design)

PCSX2's `GSDeviceVK` creates its own Vulkan instance and device, plus `VKSwapChain` for presentation. Our donor Vulkan context (`native_vulkan.cpp`) and paraLLEl-GS's Granite device are both separate devices. We cannot run three devices against one window, so the design has to choose one of:

1. **Device adoption.** Change `GSDeviceVK` to accept an external `VkInstance`/`VkDevice`/queue (`VKLoader` already loads functions per device). Cost: moderate, a few hundred lines in `GSDeviceVK`/`VKLoader`. Risk: feature mismatch (see Unknowns).
2. **Image hand-off (same pattern as paraLLEl-GS).** The HW renderer returns a `GSTexture` (`ScanoutResult`-style), and our existing present/blit draws it into our swapchain. This avoids `VKSwapChain` entirely and matches the existing `parallel_present` design. This is the preferred route.
3. **Separate process (section 5).** The renderer process owns its own device and presents either to its own window or to a shared surface. Our game process then receives no image and does no GPU work, which fits the process split.

### Our constraints on the entry points

- **Threads.** PCSX2 expects GS calls on its GS thread. Our GS-ASYNC-DESIGN gives us the same shape (a GS thread with a ring), so this maps cleanly.
- **Readbacks.** GT4's lens flare and night-race readbacks (see PARALLEL-GS-PLAN) currently wait for all GPU work. PCSX2's HW readback path also waits on the GPU, so GT4 readbacks are likely to stay a stall unless we use the per-page tracking that paraLLEl-GS has (Confirmed in GS-RENDERER-OPTIONS.md). Hypothesis: PCSX2's `GSTextureCache` has no equivalent page-tracked wait, so this is a regression source to watch.
- **Donor VRAM copy-back.** `parallel_sync_memory` copies VRAM into the donor decoder. PCSX2's HW path keeps VRAM only on the GPU and needs an explicit readback for CPU reads. This matters only if we keep donor-side CPU decode for frame dumps.

---

## 3. Upscaling and texture replacement

### Upscaling (Confirmed, inside the HW renderer)

- `GSConfig.UpscaleMultiplier` is a float. `GS.cpp:188-201` clamps it to `GSGetMaxUpscaleMultiplier(max_texture_size)`, so the cap depends on the device's max texture size. On this machine the device limit is not known (Unknown; the AMD driver reports unusual compute limits per CLAUDE.md, so derive the limit from the device, as the project already requires).
- `GSState.cpp:102` sets `m_nativeres = UpscaleMultiplier == 1.0f`. `GSRenderer.cpp:447` applies the multiplier to output size. `GSTextureCache.cpp` has about 328 scale-related references, so the texture cache, render targets and sampling all scale together.
- Hypothesis: 3x-6x is well within reach. At 6x, a 640x448 render target becomes 3840x2688 (about 41 MB per RGBA8 target), which fits in the 16 GB of an RX 9060 XT class card, but many render targets and the texture cache will multiply that. paraLLEl-GS's high-resolution scanout tops out at 2x per axis (PARALLEL-GS-PLAN.md section 11), so this is a real quality difference.

### Texture replacement (Confirmed, code present)

- `GSTextureReplacements.cpp` (1,098 lines), `GSTextureReplacements.h` (95) and `GSTextureReplacementLoaders.cpp` (631) implement dump, load and precache. The HW renderer calls it through `GSConfig.LoadTextureReplacements`, `PrecacheTextureReplacements`, `DumpReplaceableTextures` and `ShouldDump`.
- Folder layout: `EmuFolders::Textures/<serial>/...` (`GSTextureReplacements.cpp:271`). Our port would point this at a configured directory and key it on the GT4 serial `SCUS-97328`.
- Hashing: the replacement key is a hash of the texture data. `GSXXH.cpp` provides xxHash. The loader formats (PNG and similar) depend on what is compiled in; `GSPng` is the PNG encoder (dropped in the minimum subset, so the loader needs a PNG decoder anyway).
- Host plumbing needed: a texture-root path, a config flag for on/off, and a dump-directory option. This is small (about 100-200 lines of adapter) once the dependencies above are stubbed.
- Caveat (High confidence): HD texture packs only help when the replaced textures are actually sampled at the upscaled resolution; a pack made for one scale may look wrong at another. Packs are per game and per texture hash, so GT4 coverage is Unknown.

---

## 4. GT4 specifics

### GameDB entries for SCUS-97328 (Confirmed, `bin/resources/GameIndex.yaml`)

- The `SCUS-97328` entry is at line **11870** (aliases at 11878 and 12313).
- The Gran Turismo 4 entry uses the anchor `gran-turismo-4-gs-fixes` (defined at line 524), which merges `gran-turismo-4-prologue-gs-fixes` and then sets `halfPixelOffset: 4`, with the comment "Fixes weird edge shadows and depth bleed which happens on the edge as well."
- `clampModes` anchor at line 522: `vuClampMode: 2` (preserve sign, as `PCSX2-LESSONS.md` notes).
- `roundModes`: `eeRoundMode: 3`, `eeDivRoundMode: 3`. These are EE round modes, which our donor/AOT EE path would have to match; the study did not check whether our EE translator already uses chop rounding (Unknown).
- `autoFlush` and `getSkipCount: GSC_PolyphonyDigitalGames` are inherited from the prologue fixes (per `PCSX2-LESSONS.md`, line about 204; not re-verified in this session).

### Hacks in code (Confirmed in part)

- `GSC_PolyphonyDigitalGames` is in `pcsx2/GS/Renderers/HW/GSHwHack.cpp` (`PCSX2-LESSONS.md` cites line 644+). It replaces GT's render-target channel shuffle through a CLUT ramp with a single draw. This is a per-title skip hack.
- `GSConfig.UserHacks` is used about 95 times in the HW renderer. Several hacks are per-title and would need GT4-specific validation.

### Known GT4 issues on the HW renderer

Unknown. This study did not look at the PCSX2 issue tracker (no network, by instruction). The owner should check PCSX2's GT4 compatibility notes before committing. Expect at least: half-pixel offset tuning, the channel-shuffle path (our donor handles it by executing the original sequence, which the HW hack replaces), and possible depth-precision edge cases at upscaled resolution.

---

## 5. Licensing (most important)

Facts:
- PCSX2 GS is **GPL-3.0+** (173 files with SPDX headers, Confirmed).
- Decision 0066 accepts GPL-3.0 reuse but requires the project licence to be updated before any binary containing GPL code ships.
- Decision 0067 sets the project to MIT. The prebuilt installer contains translated game code. It says GPL components "must stay out of that executable or be shipped separately", and it names an LGPL paraLLEl-GS **DLL** as the precedent for separate shipping.
- GPL-3.0 has no linking exception. LGPL-3.0 allows a replaceable library linked into a non-GPL program.

Options, with risk notes (not legal advice):

**(a) Separate GS process over IPC. Recommended.**
- Our game process keeps the translated code and MIT code, and the GPL renderer runs as its own executable. The two talk through a shared-memory ring carrying GIF packets, privileged register writes, and readback requests/responses, plus a small control channel.
- The renderer contains no game code, only the GS packet stream the game produces. This matches the spirit of 0067: GPL code ships separately and users can replace it.
- Risk (Hypothesis, honest): "mere aggregation" over a separate process is the usual argument, but a tightly coupled shared-memory protocol that carries game-derived data is closer to a combined work than a pipe to a generic tool. Keep the protocol narrow, document it, and ship the renderer with its full GPL source and notices. Still, this is an argument, not a guarantee.
- Cost: throughput is fine if GIF packets go through shared memory (Hypothesis: tens of MB/s at most for GT4, Unknown in practice). The real cost is **synchronous readbacks**: each sync readback becomes a cross-process round trip, likely 0.1-0.5 ms plus the wait. Some GT4 frames depend on readbacks, so this is the main performance risk. Effort: 3-5 weeks on top of the in-process work (IPC, lifecycle, crash isolation, window handling).

**(b) GPL DLL loaded into the game process.** Not a solution. A GPL library linked into the game executable makes the combined program GPL-only, which conflicts with the game code and 0067. Treat as rejected.

**(c) Build on the user's PC (plan B from 0067).** The user builds the game code locally from their own disc with a bundled toolchain. The GPL renderer then sits inside a binary the user builds for themselves, and the project distributes only MIT and GPL source. This is the cleanest licence position but the worst UX (a full local build on every user's PC, a toolchain download, long build times). It also depends on the 0067 fallback, which the owner has not yet triggered.

**(d) Personal, non-distributed builds.** Fine for the owner's own testing and benchmarking. Not a distribution route. Any build the owner shares becomes distribution.

**Recommendation:** route (a) for the PC release. Keep the paraLLEl-GS LGPL DLL in-process as already decided. Fall back to (c) only if Sony or a legal review objects. Use (d) for development now. This is a decision the owner must make; the agent should not decide it.

Mobile note: ARMSX2 is a PCSX2 fork and is very likely GPL-3.0 too (Hypothesis; its LICENSE was not checked). On **iOS and in general on mobile**, a separate helper executable is not an option in the same way; the realistic equivalents are an XPC service or an app extension, which is a much larger engineering and App Store review question. On Android a separate process inside the same APK is awkward but possible. So route (a) does not carry cleanly to phases 2 and 3, and the mobile GS route needs its own decision (see section 6).

---

## 6. Effort per milestone (Hypothesis, one engineer)

| Milestone | Work | Estimate |
|---|---|---|
| M0 | Licence decision (owner): route (a) vs (c). Feature census of GT4's GS usage against the PCSX2 tree (the P0 census in HW-RENDERER-PLAN) | 1-2 weeks, mostly decision time |
| M1 | Extract the minimum subset, compile with clang-cl + Ninja, stub `common/`, `Host`, `MTGS`, `GSConfig` and ImGui. Build a standalone library that runs GS dump replay | 4-6 weeks |
| M2 | GS dump replay harness. Use PCSX2 `.gs` dumps and our own captured streams (GSdx dumps are cheap to produce from our donor side) and compare 1x output to the donor renderer | 1-2 weeks |
| M3 | Adapter from our front end (section 2): GIF per path, privileged mirror, read FIFO, vsync, present as an image hand-off | 3-4 weeks |
| M4 | Presentation: image hand-off into our swapchain (preferred), or device adoption | 1-2 weeks |
| M5 | GT4 validation at 1x (image diff against the donor, GT4 hacks, half-pixel, channel shuffle, readback waits) | 3-5 weeks |
| M6 | Upscaling 2x-6x: device limits, memory budget, mipmap and texture-cache behavior at scale | 1-2 weeks |
| M7 | Texture replacement: wire the loader, PNG decoder, config, hash dumps | 2-3 weeks |
| M8 | Launcher options: a new `GT4_RENDERER` value, and launcher settings for upscale and texture packs. TASK-020 is the launcher | about 1 week |
| M9 (route a only) | Separate GPL process, IPC protocol, crash isolation, shutdown, versioning | 3-5 weeks |

Total: roughly **4-6 engineer-months** (M1 to M8, Hypothesis), plus 3-5 weeks for M9 under route (a).

### Main risks

- **Coupling to PCSX2 globals and threads.** `GS.cpp`, `GSRenderer` and `GSDeviceVK` read `GSConfig` and `Host` in many places. Stubbing these touches hundreds of call sites.
- **MTGS assumptions.** PCSX2 assumes its GS thread and ring. Our front end must reproduce its ordering and synchronous-read behavior exactly.
- **Readbacks.** GT4 relies on readbacks. The HW path waits on the GPU; in route (a) each readback also crosses a process boundary.
- **Keeping a pinned fork current.** We would pin `aa7ab43`, and pulling in upstream fixes later means re-applying our patches. PCSX2 also changes its GS renderer often.
- **Device features.** `GSDeviceVK` may require Vulkan features or extensions that differ from our donor and the RX 9060 XT driver. **Unknown:** not checked, needs a feature-list diff before M1.
- **Scope creep into shaders.** GLSL is compiled at runtime in PCSX2 (per HW-RENDERER-PLAN). Our pipeline precompiles SPIR-V (`tools/wgsl_spirv`, paraLLEl-GS uses prebuilt SPIR-V). Moving to precompiled SPIR-V is a separate task.
- **Mobile.** Route (a) does not carry to iOS (see section 5). ARMSX2's GS pipeline is the same lineage and is also GPL (Hypothesis), so phase 2/3 needs its own licence decision.

---

## 7. Comparison with paraLLEl-GS (short)

| | paraLLEl-GS (LGPL-3.0+) | PCSX2 HW (GPL-3.0+) |
|---|---|---|
| Upscaling | 1x-16x SSAA, high-res scanout capped at 2x per axis (Confirmed, plan section 11) | Arbitrary float multiplier up to device limit (Confirmed in code) |
| Texture replacement | None (Confirmed in options doc) | Yes (Confirmed, code present) |
| GT4 hacks | None GT4-specific; channel-shuffle detection only | GameDB + `GSHwHack` (Confirmed) |
| Licence in our exe | LGPL DLL, allowed (decision 0067) | GPL, needs route (a) or (c) |
| Integration cost | Granite device shim, about 80 lines WSI, interface already wired | Several weeks more: extraction, stubs, device adoption, process split |
| Readbacks | Page-tracked, per-page semaphores (Confirmed) | GPU-wide wait (High confidence) |

Both can coexist as options. paraLLEl-GS stays as the accurate, cheap-to-integrate LGPL option. PCSX2 HW becomes the upscaling and HD-texture option, and the donor stays as fallback. The two cannot run in one process with their own devices without the adoption work above, so route (a) also solves the device conflict.

---

## Unknowns to close before committing

1. Owner decision on route (a) vs (c) (section 5). Nothing else should start before this.
2. Device feature diff: `GSDeviceVK` requirements against the RX 9060 XT driver (no build needed, a read of the required feature list).
3. GT4 readback count per frame (lens flare, night race): needed to size the IPC cost.
4. Whether `common/` or any other GS dependency is LGPL, and the exact licence of ARMSX2 (for the mobile phase).
5. Known GT4 issues and PCSX2 GameDB notes, from the PCSX2 issue tracker (not checked, no network).
6. Whether our EE translator already uses chop rounding (GameDB `eeRoundMode: 3`).

No checkout was changed, no build ran, and no game was launched. This file is the only output.
