# 12 — Integration with GT4Recomp

Status: specification for TASK-027. Labels: **Confirmed** (read in our code),
**Decision** (specification team choice), **Hypothesis**.

This chapter says where the new renderer (called **HWR** in this specification:
"hardware renderer") plugs into GT4Recomp. It reuses the hook points already
proven by the paraLLEl-GS integration on branch `task/012-parallel-gs`, so the
donor DMA/VIF/GIF/interrupt model stays the front end and the renderer is
chosen once per run.

## 12.1 Where the GS stream comes from (Confirmed)

```
 EE / VIF1 / VU1 XGKICK / DMAC path 3
            |
            v
   donor gif::transfer(path, data, qwc)   <-- hook: forward raw qwords
            |                                  (renderer parses GIF itself)
            v
   +--------------------+       privileged writes (PMODE..BGCOLOR, CSR, IMR)
   |  HWR backend       | <---- donor gs::write_privileged  <-- hook: mirror
   |  (own GIF parser,  |
   |   GS state, draws, | <---- SetGsCrt HLE (hw.cpp)      <-- hook: SMODE1/2 synthesis
   |   caches, Vulkan)  |
   +--------------------+ ----> SIGNAL/FINISH/LABEL inline --> donor gs::signal(0x60/0x61/0x62)
            |  ^
            |  +---- donor gs::read_image (LOCAL->HOST FIFO)  <-- hook
            v
   present() once per guest VBlank (window.cpp pacing)  <-- hook
```

The paraLLEl-GS hook set (`scripts/prepare_external_vulkan.py`, table
`PARALLEL_HOOKS`, anchors in donor `gs.cpp`, `gif.cpp`, `hw.cpp`) is the
template. Every anchor that routes to paraLLEl-GS must route to HWR when HWR is
selected. The boundary is the **GIF transfer**, not individual register writes,
because the donor's packed-register path updates ST/Q/RGBAQ/UV/FOG without
going through its generic register writer (Confirmed in the paraLLEl-GS plan,
section 2). HWR therefore owns a complete GIF parser (chapter 02).

## 12.2 Renderer selection (Decision)

- `GT4_RENDERER=donor|parallel|hw`. Unset means donor until HWR qualifies;
  any other value stops the process with a clear message (project rule: no
  silent fallback).
- The splice script gains a mode that splices a single **renderer dispatch
  front end**: the existing hooks call neutral functions (`alt_active()`,
  `alt_gif_transfer()`, `alt_read_image()`, `alt_vblank()`, `alt_present()`,
  `alt_mirror_privileged()`, `alt_attach_window()`, `alt_resize_window()`,
  `alt_wait()`, `alt_scanout_frame()`, `alt_sync_memory()`,
  `alt_reset()`, `alt_reset_gif()`, `alt_gif_busy()`), and those route to
  paraLLEl-GS or HWR. The anchors and the "anchor must occur exactly once" rule
  do not change. The donor path stays byte-for-byte as today when the variable
  is unset (a Python test already enforces this for paraLLEl-GS; extend it).
- HWR lives in `native/hw_gs/` with a **Vulkan-free public header**
  (`hw_gs_backend.hpp`), the same discipline as `parallel_gs_backend.hpp`:
  only the backend translation units include Vulkan headers, so header
  versions never meet the PCSX2-tree headers used by the donor runtime.

## 12.3 Backend interface (Decision)

The HWR backend exposes the same operations as the paraLLEl-GS backend so the
front end code is shared. Behavioural contract per operation:

| Operation | Contract |
|---|---|
| `init(options, error)` | Creates instance, device, queues, allocator, pipeline layout, shader modules, persistent resources. Fails with a readable list of missing Vulkan features (chapter 12.6). Never presents. |
| `set_signal_handler(h)` | SIGNAL, FINISH and LABEL are delivered **inline, during `gif_transfer`, in stream order**, after every draw that precedes them in the stream has been *recorded* (not necessarily executed). Returning true asks for a flush. |
| `gif_transfer(path, bytes)` | Parses GIF tags for path 1/2/3; path state persists across calls (partial tags, NLOOP remaining, REGLIST position, IMAGE remaining). Must accept transfers split at any qword boundary. |
| `gif_busy(path)` | True while the current tag of that path still expects data. |
| `reset_gif_paths()` | Clears all paths' tag state (GIF reset). |
| `write_privileged(addr, value)` | Stores 0x12000000-0x12001FFF writes by address. CSR/IMR/SIGLBLID semantics stay in the donor; HWR only reads display registers (PMODE, SMODE1, SMODE2, SRFSH, SYNCH1/2, SYNCV, DISPFB1/2, DISPLAY1/2, EXTBUF, EXTDATA, EXTWRITE, BGCOLOR). Returns false outside the range. |
| `reset_context()` / `clear_vram()` | GS reset: registers, vertex queue, CLUT buffer; `clear_vram` zeroes GS memory **and** drops all cached targets/sources. |
| `read_transfer_fifo(dst, qwords)` | Returns LOCAL->HOST data produced by the most recent TRXDIR=1 (chapter 10). Reading past the end returns zeros and counts a fault. |
| `map_vram(offset, size)` | Returns a CPU view of GS memory that is **coherent with all GPU work recorded so far** (forces target write-back for the range, chapter 10). Used for VRAM frame dumps and diagnostics. |
| `flush()` | Submits recorded work; never waits. |
| `next_frame()` | Headless frame boundary (deferred deletions, age counters). |
| `wait_idle()` | Blocks until the GPU is idle. |
| `scanout(request)` | Builds the display image exactly as `present` would, at the internal resolution, and copies it to host RGBA8 (red in the low byte). |
| `attach_window / resize_window / present(phase)` | Swapchain on the existing HWND; one present per guest VBlank (12.5). |
| `stats()` | Counters for the F8 report (12.7). |

Threading (Decision): phase 1 is **single caller**: every operation is called
on the emulation thread that calls `gif::transfer` today (same as paraLLEl-GS).
HWR may use worker threads internally only for (a) pipeline compilation,
(b) texture-replacement file loading and decoding, (c) texture hashing of large
uploads. A later milestone may move the whole backend to a GS thread following
`docs/GS-ASYNC-DESIGN.md`; the interface above already allows it because no
operation returns GPU results except `read_transfer_fifo`, `map_vram`,
`scanout` and `stats`.

## 12.4 Video mode, scanout and SetGsCrt (Confirmed behaviour to keep)

- The HLE `SetGsCrt` does not program SMODE1 by itself; the front end
  synthesises SMODE1 and SMODE2 through `gt4::gs::map_set_gs_crt` (native
  `gs_video_mode.*`) and writes them through `write_privileged`. HWR must not
  produce an image when SMODE1 describes no known video format; it returns
  "no image" and the presenter shows black (same as paraLLEl-GS).
- Scanout phase for interlaced output = the field the CRTC reads, taken from the
  donor's `g_odd_field` at present time (same function as
  `parallel_scanout_phase`).
- GT4 boots 480i (FFMD=1 in menus, interlaced), and races can run in 480p from
  the in-game option. Both must be handled (chapter 13).

## 12.5 Device, presentation and pacing (Decision)

- **HWR owns its Vulkan instance, device and swapchain**, exactly like the
  paraLLEl-GS backend owns Granite's. Our `native_vulkan` Context is not created
  in HWR mode: renderers are exclusive per run, and that Context's pipeline
  layout is specific to the donor compute renderer. Reusable pieces of
  `native_vulkan.cpp` (loader, Win32 surface creation, swapchain rebuild on
  resize, present-mode parsing, presentation rectangle arithmetic) should be
  factored into a small shared helper rather than duplicated.
- Present path per VBlank: acquire, render the output stage (chapter 13:
  merge circuits, deinterlace, optional post-processing) **directly into the
  16:9 rectangle** of the swapchain image with a fragment pass (not a blit,
  because post-processing and filtering need a shader), clear the borders to
  black, present. The rectangle arithmetic is `gt4::vulkan::presentation_rect`
  with `Aspect::widescreen` (the same as `widescreen_rect` in the paraLLEl-GS
  backend); a CTest compares both.
- Present modes: `GT4_PRESENT_MODE=fifo|mailbox|immediate`, FIFO default, same
  parser (`gt4::pacing::parse_present_mode`). At most 2 presentations in flight
  (same as `Context::kMaxPresentsInFlight`).
- Swapchain format: UNORM (B8G8R8A8 or R8G8B8A8). The PS2 output is already
  display-referred; an sRGB swapchain would encode twice.
- Pacing: `window.cpp` is unchanged. It calls `ps2::gs::present()` once per
  guest VBlank; the single clock (`pacing.cpp`, `frame_clock.cpp`) and audio
  rate control remain the only pacing authority. HWR must never sleep or wait
  for vblank on its own, except the swapchain acquire/present waits implied by
  FIFO, which are accounted as `presentation_wait`.
- Window resize and fullscreen toggle (Alt+Enter, F11) only set a flag; the
  swapchain is rebuilt before the next present. **AMD RDNA4 (the owner's
  RX 9060 XT)**: recreating a swapchain while passing the old one as
  `oldSwapchain` stalls the next 2-3 acquires for about 2 seconds (Confirmed by a
  separate study, also a known vendor workaround). HWR waits for the old
  swapchain's presents, destroys it, then creates the new one without
  `oldSwapchain` (on all AMD devices; chapter 13.6). The same rule should be
  applied to the shared helper used by the donor and paraLLEl-GS paths.
- Submission model: one primary command buffer per "GPU batch" (normally one
  per guest frame, split earlier only when a readback or a ring-full condition
  forces it), a timeline semaphore for ordering, and a 3-slot ring of
  per-frame resources (vertex/index/uniform stream buffers, staging) like
  `gs_submit_ring.hpp`. The CPU waits only for: a readback that the guest
  needs now, ring full, resize, shutdown.

## 12.6 Vulkan requirements (Decision)

Required (refuse to start without them, with a list): Vulkan 1.2 core,
`timelineSemaphore`, `independentBlend` not needed, `dualSrcBlend`,
`fragmentStoresAndAtomics`, `shaderStorageImageWriteWithoutFormat` not needed,
`samplerAnisotropy` optional, `sampleRateShading` not needed, depth format
`D32_SFLOAT` (always available on desktop), `R16_UINT`/`R32_UINT` sampled
images, `R8G8B8A8_UNORM` and `R16G16B16A16_UNORM` colour attachments with
blending, `R32_SFLOAT` colour attachment.

Optional, with a fallback each (chapter 08 explains the use):
`VK_EXT_rasterization_order_attachment_access` or
`VK_EXT_attachment_feedback_loop_layout` (framebuffer read without a copy;
fallback = copy the touched rectangle of the target to a temporary texture
before the draw), `VK_KHR_dynamic_rendering` (simplifies render-pass
management; fallback = classic render passes with a cache),
`VK_EXT_line_rasterization` (Bresenham lines; fallback = lines expanded to
quads), `VK_KHR_push_descriptor` (fallback = descriptor pools per frame),
`VK_EXT_memory_budget` (fallback = fixed budget, chapter 04),
`VK_EXT_provoking_vertex` in last-vertex mode (fallback = de-indexed flat
colours, chapter 03.2), `wideLines`/`largePoints` covering the scale (fallback =
vertex-shader expansion, chapter 03.2), `textureCompressionBC` (fallback = CPU
decode of BC replacement textures, chapter 11.4), `dualSrcBlend` is required
only for blend level `basic`/`medium` mixed modes and SPLIT_RGB_ONLY (fallback =
SHADER blending and PASS_THEN_FAIL, chapter 08). Every optional feature found or
missing is logged once at start and recorded in `result.json`.

Every limit used for sizing (max image dimension 2D, max framebuffer size,
max push constants, max storage buffer range) is read from the device. The RX
9060 XT driver reports `maxComputeWorkGroupCount[0] = UINT32_MAX`; fixtures
must never hard-code limits (project rule).

## 12.7 F7/F8 performance categories (Decision)

HWR reuses the existing categories in `native/perf_accounting.hpp`; it adds no
enum values in phase 1 so the F8 report format and its Python parser stay
stable.

| Category | HWR work accounted there |
|---|---|
| `vif_gif` | unchanged (donor DMA/VIF side) |
| `gs_upload` | GIF parsing, register state, vertex queue, host->local transfers into the shadow memory, texture decode/hash/upload recording |
| `gs_execute` | draw translation: target/source lookup, pipeline key, pass building, command recording, submit calls |
| `gs_wait` | CPU waits for GPU results: LOCAL->HOST readbacks, `map_vram`, ring full |
| `presentation` | output stage recording and present submission |
| `presentation_wait` | swapchain acquire and FIFO back-pressure |

Extra lines through `Probe::set_report_section` (one block per F8 report,
values per frame averaged over the interval plus totals): GS draws (guest
batches), GPU draw calls, render passes begun, pipelines bound, pipelines
**compiled during the interval** (stutter indicator), texture uploads
(count/bytes), source cache hits/misses, hash-cache hits, targets created /
evicted, target memory (MiB), readbacks (count, bytes, wait microseconds),
shuffles converted, barriers or framebuffer-read copies, replacement textures
loaded, GPU time per frame from timestamp queries (when `GT4_HW_GPU_TIMING=1`).
A Python fixture parses the new block.

## 12.8 Options (Decision)

Environment variables in phase 1 (the launcher, TASK-019, writes them later):

| Variable | Values | Default | Chapter |
|---|---|---|---|
| `GT4_HW_SCALE` | 1..8 integer | 1 | 06 |
| `GT4_HW_HALF_PIXEL` | `native`, `normal`, `off` | `native` | 06 |
| `GT4_HW_SPRITE_FIX` | comma list: `round`, `round_full`, `align`, `merge` | empty | 06 |
| `GT4_HW_NATIVE_POSTFX` | `off`, `normal`, `aggressive` | `off` | 06 |
| `GT4_HW_BLEND` | `basic`, `medium`, `high`, `full` | `high` (GT4 needs exact blending) | 08 |
| `GT4_HW_ACCURATE_ATEST` | `0`, `1` | 0 | 08 |
| `GT4_HW_MIPMAP` | `off`, `basic`, `full` | `full` | 03 |
| `GT4_HW_FILTER` | `ps2`, `nearest`, `forced` | `ps2` | 03 |
| `GT4_HW_DITHER` | `native`, `fine`, `off` | `native` | 06, 08 |
| `GT4_HW_AUTOFLUSH` | `sprites`, `all`, `off` | `sprites` | 02 |
| `GT4_HW_SYNC_MODE` | `cache`, `writeback` (reference mode) | `cache` | 00, 10 |
| `GT4_HW_GLM` | `1`, `0` (diagnostic: write back instead) | 1 | 04 |
| `GT4_HW_READBACK` | `exact`, `full`, `none` | `exact` | 10 |
| `GT4_HW_READBACK_SPIN` | `0`, `1` | 0 | 10 |
| `GT4_HW_DIRTY_FILTER` | `auto`, `linear`, `nearest` | `auto` | 05 |
| `GT4_HW_CLUT_ALWAYS_LOAD` | `0`, `1` | 0 | 07 |
| `GT4_HW_VERIFY_SHUFFLE` | `0`, `1` (debug comparison against the GLM path) | 0 | 09 |
| `GT4_HW_DEINTERLACE` | `auto`, `off`, `weave`, `bob`, `blend`, `adaptive` (optional `_bff` suffix) | `auto` | 13 |
| `GT4_HW_ANTIBLUR` | `1`, `0` | 1 | 13 |
| `GT4_HW_POSTFX` | comma list: `shadeboost`, `fxaa`, `cas`, `scanlines` | empty | 13 |
| `GT4_HW_SHADEBOOST` | `b,c,s,g` integers (50 = neutral) | `50,50,50,50` | 13 |
| `GT4_HW_CAS_SHARPNESS` | 0..100 | 50 | 13 |
| `GT4_HW_PRESENT_FILTER` | `bilinear`, `nearest`, `sharp` | `bilinear` | 13 |
| `GT4_HW_CROP` | `l,t,r,b` native pixels | `0,0,0,0` | 13 |
| `GT4_HW_TEXTURES` | directory of the texture root | unset = off | 11 |
| `GT4_HW_TEXTURES_PRECACHE` | `0`, `1` | 0 | 11 |
| `GT4_HW_DUMP_TEXTURES` | `0`, `1`, `mips` | 0 | 11 |
| `GT4_HW_DUMP_FORMAT` | `g`, `p` | `g` | 11 |
| `GT4_HW_GAME_FIXES` | `1` (GT4 pattern rules on), `0` (diagnostic only) | 1 | 09, 14 |
| `GT4_HW_DUMP_SOURCE` | `vram`, `scanout` | `vram` | 12.9 |
| `GT4_HW_GPU_TIMING` | `0`, `1` | 0 | 12.7 |
| `GT4_HW_GPU_DECODE` | `0`, `1` (later optimisation: decode memory sources from the GLM) | 0 | 04 |

Unknown values stop the process. All chosen values are logged once at start
and recorded in `result.json` by the launch scripts (the scripts scrub
inherited `GT4_*` variables, so they must pass these explicitly).

## 12.9 Frame dumps and the 1x reference (Decision)

- `GT4_HW_DUMP_SOURCE=vram` (default): `gs::display` calls `alt_sync_memory`,
  which makes the shadow GS memory coherent (every target overlapping the
  displayed rectangle is written back at **1x**, taking the top-left sub-sample
  of each native pixel when the scale is above 1, chapter 10.4) and then uses the
  donor's own decode. This makes HWR
  frames directly comparable with donor frames and is the primary 1x
  validation path (chapter 14).
- `GT4_HW_DUMP_SOURCE=scanout`: the HWR output image before the 16:9 placement,
  at internal resolution; used for upscaling and post-processing review.
- Save states: refused with a clear error in phase 1 (same as paraLLEl-GS).

## 12.10 Build (Decision)

- CMake option `GT4_HW_GS` (default OFF until qualified), linking
  `native/hw_gs/*` into the Vulkan runtime library. Third-party code allowed in
  HWR: xxHash (BSD-2-Clause, exact XXH3 needed for pack compatibility),
  a PNG decoder/encoder with a permissive licence (for example `stb_image` /
  `stb_image_write`, public domain/MIT, or libspng BSD-2), a DDS/BC reader
  written by us, VMA (MIT) optional. Pin each in `native/THIRD_PARTY.md` and
  `docs/inputs/`.
- Shaders: GLSL 4.50 written by the implementation team, compiled **at build
  time** to SPIR-V by a pinned glslang, embedded in the binary; specialisation
  constants and a small set of compiled variants select features. No runtime
  GLSL compiler (keeps start-up deterministic and the binary small). The GPU
  driver compiles pipelines at run time, which the project allows.
- Pipeline cache: a `VkPipelineCache` saved under the user data folder (not in
  the repository), keyed by driver UUID; the F8 counter "pipelines compiled
  during the interval" tells whether it works.

## 12.11 Licence and provenance (Decision 0069)

Every HWR source file carries the MIT header and a line "Written from the
TASK-026 specification (docs/hw-renderer-spec)". Every implementation commit
message states the chapter(s) it implements. Implementers must not open
PCSX2 GS sources, PCSX2 shaders or GSdx-derived code (ARMSX2 included); they
may read this specification, public hardware documentation (GS User's Manual
knowledge, ps2tek), our donor renderer (MIT) and the Vulkan specification.

## Sources

- `C:\Antigravity\gt4-wt-012\native\parallel_gs_frontend.inc` (whole file):
  hook functions, option parsing style, SetGsCrt mapping, VRAM sync.
- `C:\Antigravity\gt4-wt-012\native\parallel_gs_backend.hpp` (whole file):
  backend contract, 16:9 rectangle, present filter, 447-line rule.
- `C:\Antigravity\gt4-wt-012\scripts\prepare_external_vulkan.py` (lines
  1-180): pinned donor sources, hook anchors, splice mechanics.
- `C:\Antigravity\gt4-wt-012\native\native_vulkan.hpp` (whole file): ring,
  timeline, presentation rectangle, present modes.
- `C:\Antigravity\gt4-wt-012\native\perf_accounting.hpp`, `perf_probe.hpp`,
  `window.cpp` (F7/F8/F11 handling, `release_frame`).
- `docs/PARALLEL-GS-PLAN.md` on `task/012-parallel-gs`, sections 0-5.
- `docs/GS-ASYNC-DESIGN.md` on `task/012-parallel-gs`.
- `docs/GSDX-STUDY.md` on `task/012-parallel-gs`, section 2 (entry points).
- Research report `output-postfx-vulkan-perf.md` part B (device features,
  formats, swapchain, RDNA4 recreation stall, streaming, descriptors).
- Research report `upscaling-clut-readbacks.md` C.2, C.7, C.8 (FIFO reads,
  SIGNAL/FINISH/LABEL, GS thread model).
- Research report `gt4-gamedb-hacks-donor.md` 3.4, 4.4 (donor environment
  variables, headless comparison, GT4_PGS_CAPTURE format).
- Coordinator note 2026-10-15 (RDNA4 swapchain stall study).
