# High-FPS study: decoupling presented frame rate from game speed

Date: 2026-10-15. Read-only study. No repository file edited, nothing built, no game launched, PCSX2 sources not opened.
Labels: **Confirmed** (read in code or counted in logs), **High confidence**, **Hypothesis**, **Unknown**.
Paths: repo = `C:\Antigravity\gt4-staticrecomp`; decomp = `private/slice151-research/gt4-decomp`; donor = `private/references/ps2-web-recomp`.

## 0. Summary

- Today the guest and the presented frame are the same thing: one guest VBlank = one present, and the EE blocks at each VBlank until the wall-clock grid allows it. Guest speed is the number of VBlanks per wall second, capped at 60 by pacing and lowered by host work (Confirmed).
- Physics is already frame-locked: `RaceBase__updateRace` calls the update with a constant `dt = 1/60` (Confirmed). Interpolation approaches A, B and C do not touch physics. Decoupling physics (a different dt) is the out-of-scope part.
- (a) Frame drops at full speed: **feasible only with VU1 skipping** (VU1 outputs feed only the GS), and that is the expensive part. Medium to high risk. Higher-value lever: keep reducing VU1/EE cost (MTVU, O3).
- (b) 120/144 Hz with real rasterized in-between frames:
  - **A (GS primitive interpolation):** best candidate. Extra cost is GPU raster plus a cheap lerp on a separate thread, so it does not lengthen the guest critical path. Quality risk: primitive correspondence, disocclusion, HUD. Cheapest proof-or-kill: an offline oracle (section 6).
  - **B (VU1 input interpolation):** highest quality per pixel, but every in-between frame costs a VU1 run. Showroom VU1 was 16.2 ms before slice 157, so 120 Hz is not affordable without a 2x VU1 speed-up.
  - **C (EE render-state interpolation):** same cost class as B plus per-object EE hooks. Not first.
- D (simpler): repeat frames with VRR (no interpolation, no latency), or late-stage camera reprojection from VRAM depth (not a rasterized frame; does not meet the literal (b)).
- Recommendation: prototype A offline on captured consecutive frames before any engine change (section 6). Do not implement (a) before VU1 is cheaper.

## 1. How a frame is produced today

**Chain (Confirmed):** EE translated code (AOT, `unit_XXXX.cpp`) builds VIF/DMA packets → VIF1 shadow parser (`vif.cpp`, MTVU design) → MSCAL starts VU1 microcode → VU1 AOT program (`tools/vu_recomp`, overlay `scripts/vu1_aot_overlay.inc`) transforms vertices and emits GIF packets via XGKICK (PATH1) → `gif::transfer` → GS front end (`gs/gs.cpp`, donor) → batches in `g_data`/`g_params` → native Vulkan compute raster (`native/native_vulkan.cpp`, `gpu_*.inc`) → present (`native/window.cpp:on_vblank`).
- VU1 writes screen-space XYZ2 and ST/RGBAQ into the GIF stream (the GS takes post-projection coordinates). VU1 registers and memory are not reachable from EE or VU0; DMA cannot source VU memory (`dmac.cpp:36-39`, `cop.cpp:29,47-50`, MTVU-PLAN 1.1 and 2). **High confidence** that VU1 output reaches only the GS. Open point: pointer-based EE reads of VU memory (MTVU-PLAN, "[U]").
- Guest work per frame (last measured per scene, see section 4): EE AOT 2-3 ms (slice 150 baseline), VU1 5-16 ms (pre-157 numbers), GS wait 0.0-4.1 ms.

**Where 60 Hz is set (Confirmed):**
- Guest: `frame_cycles() = kEeClock / 60` with `kEeClock = 294,912,000` (`private/references/ps2-web-recomp/runtime/src/system.cpp:85`, `include/ps2/system.hpp:12`). 4,915,200 EE cycles per frame. VBlank event scheduled at `system.cpp:141` and `:363`. `vblank()` at `system.cpp:127-142` increments `g_frames`, calls `g_options.on_vblank`, `hw::vblank_start()`, `kernel::on_vblank_start()`.
- Game loop: the EE waits on a vsync flag. `kernel.cpp:433-438` (`on_vblank_start`) sets `g_vsync_flag`. IOP has its own VBlank waiters (`iop/libs.cpp:118,920-932,1056`).
- Host: `native/window.cpp:176-190` (`release_frame`) waits on the wall-clock grid in paced mode, then `frame_pacer->release` (`native/pacing.cpp:52-87`). The grid rate is `guest_vblank_rate = 60` (`native/pacing.hpp:24`). Clock: `native/frame_clock.cpp` (QPC, high-resolution waitable timer, 0.5 ms spin tail).

**What game speed is tied to (Confirmed):**
- Guest time is virtual: `now() = g_base + (g_given - budget)` (`system.cpp:181`), advanced by EE cycles actually executed, with idle skipping (`system.cpp:144-152`). So the game advances as fast as the host executes EE code. There is no catch-up when the host falls behind (OPTIMIZATION-PLAN section 4 O4).
- Frame count (VBlanks) drives game logic. Physics has a constant step: `RaceBase__updateRace.cpp` sets `float dt = 0x1.11111p-6f` (= 1/60) and passes it to `DynamicsConductor__Update` and the other updates; `RaceSolitaire__updatePlay.cpp` takes the same `dt` and only overrides it with a debug value at `self+0xf0f4`. `RigidBodyManager__Update` also takes `f`. So **physics runs once per guest VBlank at 1/60 s**, not per wall-clock present.
- `RaceBase__getFrameInterval` returns constant 1 (decomp `src/race/RaceBase/RaceBase__getFrameInterval.cpp`); `PlayStation3__RenderLoopGetFlipCycle` returns a global (`D_006186E4`), not used for timing in the decomp we read.
- Result: game speed = min(60, host throughput) in VBlanks per wall second. Confirmed by the owner's scene table (STATUS "Current state"): 60.0 at 100% in Time Trial and showroom, 46.15 at 76.9% in the night race before slice 155-157.

## 2. Part (a): keep 100% game speed while dropping presented frames

**Current behavior (Confirmed):** the EE does not skip work. Under load, the EE is slower (game speed drops below 100%). With MTVU, the EE blocks at each VBlank hard sync, and the GS submit ring blocks when all three slots are in flight (`native/gs_submit_ring.hpp`, GS-ASYNC-DESIGN "Ring of slots").

**Where the time is (Confirmed/High confidence):** the bottleneck is CPU-side guest work (VU1 + EE + GS front end), not the GPU. The owner measured the RX 9060 XT at about 30 W of 180 W in play (README/OPTIMIZATION-PLAN note in backlog). So presenting fewer frames does not reduce CPU work unless VU1/GS front-end work is skipped.

**What must be skipped to save time on a dropped frame:**
1. Presentation only (`gs::present`): cheap to implement. It saves almost nothing (the present fence is already merged into the slot submission, GS-ASYNC-DESIGN "Presentation") and does not make the game faster. Not a fix on its own.
2. GPU raster of the frame's PATH1 draws: saves GPU time only. GPU is not the bottleneck, so little gain.
3. VU1 execution and the PATH1 GIF output (the real saving: VU1 is the largest CPU category). Consequences:
   - VU1 has persistent memory across calls. A program that caches state in VU memory (matrices, double-buffered constants) would change later frames. **Unknown** for GT4; needs a differential check (dropped vs not dropped). A fixed-seed headless run can detect divergence only if the schedule drops frames.
   - PATH2/PATH3 (texture uploads, VRAM writes from the EE) must still run. They are the source of textures and must not be dropped.
   - Render-to-texture or frame-buffer feedback in PATH1 would leave stale VRAM in later frames. **Unknown** whether GT4 uses it (the donor's "backbuffer promotion" hack hints the donor deals with it, not GT4 specifically).
   - Readbacks (DMA ch1 reverse) read VRAM written by dropped draws. Night-race readbacks are the likely case (section 4).
   - MTVU makes this clean in one way: the worker receives whole VU1 jobs with journal placeholders (MTVU-PLAN section 2). A drop policy can skip a job at the worker and emit no PATH1 journal ops for it. Journal ordering stays intact. This is where (a) would live.

**Verdict (a):** feasible with medium effort (about 3-5 agent sessions: drop policy, MTVU skip, F8 counters, headless divergence checks), but risk is medium-high (VU memory state, VRAM feedback, readbacks) and the visible result is a frame hold (judder), not smoothness. Recommend after VU1 speed-ups; the O3/MTVU levers reduce the same cost without visible holds.

## 3. Part (b): presenting 120 or 144 Hz with rasterized in-between frames

Common rules (Confirmed by the structure of the loop): an in-between frame at time t between guest frames N and N+1 needs N+1 to exist, so the display shows N, then N+t, and each displayed frame is delayed one guest frame (about +16.7 ms latency). Extrapolation (from N-1 and N) removes latency but increases artifacts. For 144 Hz, each guest frame yields 2.4 displayed frames, so 1.4 in-between frames at t = 0.4 and 0.8 (and 0.2 for the next). 120 Hz needs one in-between at t = 0.5.

Only real intermediate frames are considered here: no optical flow, no generative frame generation (owner rule).

### A. GS-stream (primitive) interpolation

**Mechanism:** keep the post-VU1 primitive streams for frames N and N+1 (the donor batches already hold them in `g_data`, rewritten each submit: GS-ASYNC-DESIGN "Ring of slots"), match primitives between N and N+1, lerp screen-space XYZ, Z, and perspective-correct ST (lerp s/q, t/q and 1/q using the Q of each vertex), lerp RGBA, and rasterize at t with the same raster path. The in-between frame reuses the frame-N+1 textures and the frame-N VRAM state (approximation).

**Feasibility by facts:**
- Screen-space interpolation of VU1 output is affine in screen space per primitive, which is close to exact for small per-frame motion. Error grows with near-camera geometry (car bodies in external view) and with camera rotation.
- Primitive correspondence: the stream is ordered (`gs.cpp` batches; GS-ASYNC-DESIGN). Counts per frame: showroom about 46k primitives per frame (STATUS, slice 153 notes); race counts Unknown (the "1,021,676" in the journal table is interpreted VU1 calls, not primitives). Culling and LOD change counts between frames, so matching must be by key (draw context PRIM/FRAME/ZBUF/TEX0, texture base, vertex count and kind) with order as tiebreak. Stability is **Unknown** until measured. A mismatch means fallback to frame N or N+1 for that batch.
- Disocclusion: a moving car uncovers background that frame N did not draw. Lerp of N's list has holes. Fill from N+1 coverage (draw N+1 primitives that are not covered by N) is the usual fix. Hypothesis: visible seams on the car silhouette at 120 Hz.
- HUD and text: GT4 HUD sprites (speed, lap time) change between frames. Lerping numeric text is wrong. Fallback: take HUD batches from N+1 unchanged. Identification of HUD batches is **Unknown** (needs the draw-context signature).
- Particles, transparency, depth order: preserved if primitive order is preserved; blend and Z tests still run in the in-between raster.
- Interlace: menus are 480i (field output); races are held 480p by the owner fix (`force_race_480p_at_vblank`, `window.cpp`). A is only specified for progressive frames.
- Lerp location: a compute pass can do the lerp on the GPU using the batch buffers already uploaded (cheap); a CPU lerp of about 46k primitives is also cheap (Hypothesis: 0.3-1 ms).

**Effort:** offline prototype 2-3 sessions (section 6); in-engine 6-10 sessions (stream matching, lerp compute, in-between raster in its own submit slot, pacing for 120/144 present, F8 counters). **Quality risk: medium-high**, mainly disocclusion, HUD, and render-to-texture passes. **GPU cost:** one extra raster per in-between frame, Hypothesis 1-3 ms each on this GPU (GPU timestamps are not yet available: PS2_GPU_PROFILE throws in native init, GS-ASYNC-DESIGN). **CPU cost:** near zero on the guest thread if the in-between frame is built on a separate thread.

**Where it lives:** `native/` between the GS batch upload (`gpu_helpers.inc`, submit ring) and the raster dispatch, plus a present scheduler in `native/window.cpp`/`pacing.cpp` for 120/144 Hz. No donor change needed beyond capturing the batch buffers.

### B. Transform-level (VU1 input) interpolation

**Mechanism:** each draw is an MSCAL call with a VU1 entry and data blocks uploaded by VIF UNPACK. Per draw, take the data block for frame N and N+1, lerp the float fields (positions, matrix elements, colors, ST), keep integer fields (indices, flags, addresses) from N, run the same VU1 entry, and rasterize. This is more exact than A: the perspective divide and clipping happen per interpolated vertex.

**Feasibility facts:**
- The MTVU worker already sees each VIF packet and MSCAL with entry, TOP/ITOP and FBRST (MTVU-PLAN 1.3 and 2). The structure "one MSCAL sequence per frame" is a better key than GS primitives: MSCAL counts are 724-815 per frame, MSCALF about 0.6, MSCNT 0 (MTVU-PLAN section 2, owner logs).
- VIF UNPACK types tell float fields (V4-32) from integer fields (V4-8, etc.). **High confidence** that this is enough to split lerpable fields.
- The per-object matrix layout in VU memory is **Unknown** in the repo (VU1-MICROCODE-SOURCE.md does not document it). The decomp (`ModelSet2__render`, `CameraBase__getModelviewMatrix(self, target, float t)` with a `vt[3]` entry, `CameraBase__applyProjection`) names where the matrices come from on the EE side.
- Lerp of matrix elements is a good approximation for small rotations (error is second order in the per-frame angle). At 60 Hz a car rotates a few degrees per frame. Hypothesis: acceptable.

**Cost:** one VU1 run per in-between frame. Pre-157 owner numbers: VU1 showroom 16.2 ms, rally 11.3 ms, night 3.7 ms interpreted plus 7.1 ms AOT (STATUS, journal table). Post-157 per-scene VU1 is **Unknown** (only frame rates are known: showroom 60.0, night 58.9, rally 59.9). At 120 Hz the worker must do 2 VU1 runs per 16.7 ms in showroom (about 32 ms): **not affordable**. Feasible only where VU1 is under about 8 ms per run.

**Effort:** 10-15 sessions. Needs VIF capture for two frames, a field-type table per entry, a lerp pass, and a VU1 re-run path; the VU1 speed-up is a prerequisite. **Quality: highest per pixel** (exact geometry for matched draws; holes still happen where draws appear or vanish).
**Where it lives:** MTVU worker (`native/mtvu_*`, donor `vif.cpp`/`gif.cpp` anchors) and the VU1 overlay (`scripts/vu1_aot_overlay.inc`, `tools/vu_recomp`). Requires new overlay anchors (MTVU-PLAN 1.2).

### C. Game-logic / render-state interpolation (fixed step plus render interpolation)

**Mechanism:** keep `RaceBase__updateRace` at 60 Hz (dt = 1/60), snapshot object and camera transforms each update, and at each in-between present re-run the EE render submission with the transforms lerped (camera via `CameraBase__getModelviewMatrix(self, target, t)` already takes a float `t`; car and object render via `ModelSet2__render` and the car model code). Then VU1 and GS run as usual.

**Feasibility facts:** names and float `t` parameters exist in the decomp (CameraBase family, ModelSet2 family, 12 `ModelSet2__render` units), so the hook points are identifiable. But the EE code is translated AOT, so hooks need overlays in the producer (`scripts/ee_producer.py`) or call-site patches. Object lists and transform storage per object type are not mapped in the repo (**Unknown** beyond names).

**Cost:** EE render submission per in-between frame (AOT 2-3 ms, Hypothesis) plus VU1 and GS per in-between frame, like B. Effort 15-25 sessions. Quality: correct camera and object timing; risk in game code that reads render state back (particle spawns, camera feedback) — **Unknown** count.

**Verdict:** correct in principle, most invasive. Not first.

### D. Simpler options

- **D1, VRR or repeated presents (no interpolation):** present each guest frame when it is ready (FIFO or mailbox already exist: `GT4_PRESENT_MODE`, `native_vulkan.cpp`). On a variable-refresh monitor this removes judder without any extra work. Whether the owner's monitor has VRR is **Unknown**. Without VRR, 120 Hz is 2x repetition (smooth, no motion gain) and 144 Hz is 2.4x (judder). Effort 1-2 sessions. Zero quality risk.
- **D2, late-stage camera reprojection:** for each in-between present, warp the last real frame by the camera delta (rotation and translation), using the Z buffer in VRAM (ZBUF) as depth. No GS raster, a compute pass of about 0.2-0.5 ms (Hypothesis). Works for camera pans; car motion is not corrected; disocclusion holes. This is not a rasterized frame, so it does not meet the literal (b). Effort 3-5 sessions. Owner decision needed.

## 4. Cost estimate on this machine (Ryzen 7 5700X, RX 9060 XT)

| Item | Value | Label |
|---|---|---|
| Frame budget | 16.67 ms per guest VBlank; 8.33 ms per 120 Hz present; 6.94 ms per 144 Hz present | Confirmed (arithmetic) |
| Guest frame, current, showroom/race/night | 60.0 / 60.0 / 58.9 VBlanks/s post-slice-157 (STATUS, README) | Confirmed |
| VU1 per frame | showroom 16.2 ms, rally 11.3 ms, night 7.1 ms (AOT) + 3.7 ms interpreted; pre-157 numbers, post-157 **Unknown** | Confirmed (pre-157) |
| GS fence wait | night 4.1 ms (slice 153), 4.2-4.3 ms (slice 155); cause suspected lens-flare readbacks (journal 2026-10-15) | Confirmed (counts), cause Hypothesis |
| "8 KiB readback" | the owner's note; not in repo docs (only "readback suspected") | Unknown |
| GPU busy | Unknown; power 30 W of 180 W in play suggests far from saturated | Hypothesis |
| Extra raster per in-between frame (A, D2 excluded) | 1-3 ms GPU, on a separate thread | Hypothesis |
| Extra VU1 per in-between frame (B, C) | = VU1 cost of the scene (5-16 ms pre-157) | Confirmed mechanism, value Hypothesis |
| CPU lerp of 46k primitives (A) | 0.3-1 ms on a worker, or near zero on GPU | Hypothesis |

**Consequence:** the current guest is at its frame budget in showroom (60.0 with little margin). A adds GPU work and CPU lerp off the guest thread, so it fits better. B and C add VU1 work per in-between frame: at 120 Hz they need VU1 at about 8 ms or less per scene. At 144 Hz they need more.

## 5. Where each approach must live (summary)

| Approach | Location | Touches physics? | Touches guest code? |
|---|---|---|---|
| (a) drop | MTVU worker job skip + GS present policy (`native/mtvu_*`, `window.cpp`) | no | no (VU1 skipped on dropped frames) |
| A | `native/` GS batch capture, lerp, in-between raster, present scheduler | no | no |
| B | MTVU worker, VIF capture, VU1 overlay (`scripts/vu1_aot_overlay.inc`, `tools/vu_recomp`) | no | no |
| C | EE overlays via producer (`scripts/ee_producer.py`) + decomp-named hooks | no (fixed 60 Hz kept) | yes |
| D1 | `native_vulkan.cpp` present mode; monitor setting | no | no |
| D2 | native compute pass after present; VRAM Z read | no | no |

"Physics decoupled from frame rate" (CLAUDE.md out-of-scope) is not needed for any of these: physics already runs per guest VBlank at 1/60, and the in-between work only changes presentation (A, B, D2), or render submission (C).

## 6. Recommendation and minimal proof or kill for A

**Order:**
1. D1 first (1-2 sessions, zero risk): check the owner's monitor for VRR; test `GT4_PRESENT_MODE` with 120 Hz repeats. Owner test needed.
2. Oracle study for A (2-3 sessions, offline, no game launch): the decisive test (below).
3. If the oracle passes: in-engine A prototype behind a switch (`GT4_INTERP=1`), 6-10 sessions.
4. (a) and B only after VU1 is measured per scene after slice 157 and cheaper (MTVU and O3 levers). C last.

**Oracle study for A (the proof-or-kill):**
- Input: consecutive real frames from `GT4_PGS_CAPTURE` on branch `task/012-parallel-gs` (`native/parallel_gs_backend.cpp:111-135`; records 'G' path bytes, 'P' register writes, 'R' readbacks, 'V' frame numbers). The format is already defined and the owner can record it. Captures are private and stay under `private/`.
- Method: from captured frames N-1 and N+1, build frame N by the A lerp at t = 0.5 and compare with the real frame N rasterized by the same replay backend (`tools/gs_replay` on the branch). Use the metrics of `docs/hw-renderer-spec/14-gt4-specifics-and-validation.md` section 14.4 (pct_bad > 8, PSNR). Report primitive matching rate per frame (matched / total) and holes per frame.
- Scenes: showroom (rotating car: worst case for lerp), night race (readbacks), a race straight (camera motion), menu (480i, expected to fail).
- Kill criteria (proposed, owner to confirm): matched primitive rate below 95% in race scenes, or PSNR below 32 dB on race scenes, or visible holes on the car silhouette. Pass: PSNR of the lerp against the real frame within the gate, with the missing primitives only at disocclusions.
- Expected output: a Python/C++ offline tool under `tools/` on a new `task/` branch, with CTest fixtures on synthetic primitive lists and a Python test on a capture slice.
- Effort: 2-3 agent sessions; the owner records captures (2-3 scenes, 120-300 frames each).

**Not decided here:** whether late-stage reprojection (D2) or interpolation counts as "real rasterized frames" under the owner's rule; this needs the owner's decision.

## 7. Open questions (Unknown)

1. Post-157 VU1 cost per scene (needs an F8 interval per scene; the owner has the data).
2. Whether any PATH1 draw in GT4 is render-to-texture or frame-buffer feedback (needs a GS capture scan).
3. Whether EE code reads VU1 memory through pointers (MTVU-PLAN 2 "[U]"). Affects (a).
4. The per-object matrix layout in VU memory (affects B).
5. Primitive matching stability across consecutive frames (oracle study).
6. Monitor VRR support and the owner's preferred presentation mode.
7. The "8 KiB readback" in the night race: no repo evidence; count downloads per frame (GS-ASYNC-DESIGN "CPU observation points").
8. GPU busy time per frame (needs timestamps; `PS2_GPU_PROFILE` throws in native init).

## 8. Sources read

CLAUDE.md, README.md (status and scene table), docs/ARCHITECTURE.md, docs/OPTIMIZATION-PLAN.md, docs/GS-ASYNC-DESIGN.md, docs/MTVU-PLAN.md (sections 0-2 in full; sections 3-4 read), docs/STATUS.md (current state), docs/journal/2026-10-15.md (F8 tables, MTVU notes), docs/hw-renderer-spec/14 (capture and metrics), docs/GS-RENDERER-OPTIONS.md, native/pacing.cpp, native/frame_clock.cpp, native/window.cpp (`release_frame`, `on_vblank`), native/pacing.hpp (constants), donor `runtime/src/system.cpp` (VBlank, clock, idle), `runtime/src/kernel.cpp` (on_vblank_start), decomp `RaceBase__updateRace.cpp`, `RaceSolitaire__updatePlay.cpp`, `RigidBodyManager__Update.cpp`, `CameraBase__getModelviewMatrix.cpp`, `RaceBase__getFrameInterval.cpp`, `PlayStation3__RenderLoopGetFlipCycle.cpp`, and `git show task/012-parallel-gs:native/parallel_gs_backend.cpp`.
