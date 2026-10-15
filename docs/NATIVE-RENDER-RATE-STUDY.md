# Native render rate study (TASK-031): extra presents by re-running the render path

Date: 2026-10-15. Read-only research. No repository file edited, nothing built, no game launched, PCSX2 sources not opened.
Labels: **Confirmed** (read in code, or counted in owner logs), **High confidence**, **Hypothesis**, **Unknown**.
Paths: repo = `C:\Antigravity\gt4-staticrecomp`; decomp = `private/slice151-research/gt4-decomp` (abbreviated `D:`); donor = `private/references/ps2-web-recomp` (abbreviated `DN:`); logs = `private/manual-play/session-*/probe.log`.
Baseline read first: `CLAUDE.md`, `docs/HIGH-FPS-STUDY.md` (sections 0-3 read in full).

Model under study: simulation stays at 60 Hz (dt = 1/60 in the race update). Extra presents (120/144 Hz) re-run the game's own render path (EE scene traversal, VIF/DMA packets, VU1, GS) with car, camera and object transforms interpolated between two simulation steps.

## 0. Verdicts (short)

1. **Clean split exists, but not clean enough.** Simulation is `RaceBase__update` -> `updatePlay`/`updateRace` (dt constant). Render is `RacePS2Base__render` and its helpers. But the sim update itself calls a render-side function at its end (`CourseData__render_course_for_renderer`), and render calls update-side functions (`RaceEntryCar__update`, `RaceEntryBase__sortEntryCar`). A "render twice" is therefore not render-only without guards. **High confidence.**
2. **Render mutates state.** Confirmed mutators: a per-render shift register (`CF5A`), a VRAM feedback/accumulation effect (`func_003429E0`, fixed VRAM offsets), a first-render latch in the spark particles, a model state latch, a car-list sort and entry-car toggles. Each must be guarded or snapshotted for a repeat render. **Confirmed (code), High confidence (effect on repeats).**
3. **Transforms exist as functions of time, not as stored snapshots.** Camera getters already take a float `t` (`CameraBase` virtuals). Car and wheel world matrices are **not located** in the decomp (no fields in the headers; `RigidBodyManager__Update` has no body). Snapshot-and-lerp is cheap once the addresses are found. **Unknown (car matrices), Confirmed (camera t API).**
4. **Cost.** Owner F8 intervals post-slice-157 show EE dispatch about 1.6-2.7 ms, VU1 AOT 4.8-9.8 ms, VIF/GIF 1-3.5 ms per frame. 120 Hz is borderline with MTVU for race-like scenes and fails in heavy scenes (37k primitives, VU1 9.8 ms). 144 Hz fails in heavy scenes. **Hypothesis (scene attribution and render-pass cost not measured).**
5. **Hook mechanics.** A full machine savestate exists in the donor (`save_state`/`load_state`, covers RAM, VU, GS, VIF, DMAC, IOP). That makes an offline "render N twice / render with lerped state" proof feasible. An in-engine EE re-entry needs a host-callable EE entry, which I did not find (the donor has `call_guest` on the IOP side only). **Confirmed (savestate), Unknown (EE re-entry API).**
6. **Verdict.** Offline proof: feasible, 2-3 sessions. Real-time C (repeat EE render with lerped transforms): not feasible on the current budget without VU1 under about 6 ms per scene and an EE re-entry API (15-25 sessions). Real-time B: borderline with MTVU only for race-like scenes. A (GS-primitive interpolation, prior study) remains the cheapest real-time candidate. Minimal proof: section 7.

## 1. Race main loop: update and render, and the frame boundary

### 1.1 Frame boundary and VBlank wait (Confirmed)

- Donor clock: `frame_cycles() = kEeClock / 60` (DN `runtime/src/system.cpp:85`). `vblank()` at `DN:system.cpp:127-142`: increments `g_frames`, calls `g_options.on_vblank`, `hw::vblank_start()`, `iop::vblank(true)`, `kernel::on_vblank_start()`, reschedules itself.
- Guest wait: the guest arms a vsync flag through kernel syscall `0x73` (`DN:runtime/src/kernel.cpp:692`: `g_vsync_flag = arg(c,0); g_vsync_data = arg(c,1)`). `kernel::on_vblank_start()` (`DN:kernel.cpp:433-437`) sets the flag and clears it. The EE thread waits on that flag; this is the only per-frame EE wait I found. **Confirmed.**
- The SDK call that issues syscall `0x73` (sceGsSyncV-like) is **not named** in the decomp (no `SyncV`, `DrawSync`, `sceGsSync` symbols; `knowledge/architecture.md:219`: "the native frame loop and the vertical-sync wait live ... not identified"). **Unknown (which guest function issues the wait).**
- Native host: `native/window.cpp:176-190` (`release_frame`) waits on the wall-clock grid in paced mode, then `window.cpp:192-241` (`on_vblank`) pumps messages, reads input, pumps audio, and calls `ps2::gs::present()` (`window.cpp:241`). So: one guest VBlank = one present, and the EE is released by the host wait. **Confirmed.**
- Order per guest frame (High confidence): VBlank N (donor event) -> host `release_frame` -> host present of the last completed display -> `kernel::on_vblank_start` releases the EE -> EE runs update and render for frame N+1 -> GS batches go to the async ring (3 slots, `native/gs_submit_ring.hpp`) -> VBlank N+1 presents.

### 1.2 Update (simulation) call chain

Decomp file names are the game's own names for the functions at those addresses (from `include/gt4/RaceBase.h` and the `src/race/RaceBase/` files).

- `RaceBase__update(Self *self)` (`D:src/race/RaceBase/RaceBase__update.cpp`): entry from the game object loop. Reads a vtable entry at `vt+0x248` (gate, returns int). If the gate is true:
  - if `fCD8 == 0`: calls the entry at `vt+0x250` with **constant `0x1.111110p-6f` (1/60)** (Confirmed: the `float` argument in the call);
  - else calls `vt+0x268` with no dt;
  - then `vt+0x270`. Then `RaceEventQueue__get(..., 0xA)` and `vt+0x280`.
  - Note on naming: `include/gt4/RaceBase.h:94-97` names slot 73 `updatePlay(float)`, slot 74 `updateRace()`, slot 77 `updateOther()`. The offsets are `slot*8` (`0x250 = 74*8`), but the call-site signatures disagree with the header (`0x250` takes a float, header slot 74 does not). **Unknown: exact vtable name for each offset.** Identify by the call signature and the function bodies.
- `RaceBase__updateRace(char *s)` (`D:src/race/RaceBase/RaceBase__updateRace.cpp`): sets `float dt = 0x1.11111p-6f` and passes it to `func_00394D80(...,dt)`, `DynamicsConductor__Update(a)`, `func_0034DB30`, the entry at `+0x158` (with dt), `RaceDisplayLapDiffEvent__structor_2`, and `RaceBase__updateReplayEndingFrames`. Replay frame index is read from `replay+0x11c` (`s+0xD28`). **Confirmed (dt constant in code).**
- `RacePS2Base__updatePlay(char *p, f32 dt)` (`D:src/race/RaceBGMPS2_RacePS2Base/RacePS2Base__updatePlay.cpp`), the PS2 race update:
  - `RaceBase__updatePlay(p, dt)`: multiplies dt by a real-time factor from a query (`vt+0x108`) and calls `RaceOrganization__raceRealTimeUpdate(s, dt*v)`. So real-time scaling is inside the sim, on top of the 1/60 base.
  - `func_004965D8`, `FlareClass__updateFlare(p+0xDA0, dt)`, `RaceSmoke__update(p+0x31D0, dt)`, `RigidBodyManager__Update(p+0x35E8, dt)`, `ParticleManager__Update(p+0x3238, dt, ...)`.
  - Input-derived toggles (extend button, flags at `CF34`, `D3C`).
  - `CF38` (boost-like, clamped 0..3), `CF3C` (decay by `dt/CF40`), `CF48/CF50` (two values advanced by `dt*rate`, clamped 0..1).
  - **Last line: `CourseData__render_course_for_renderer(..., CF5C)`** (render-side data built inside the sim update). **Confirmed (code).**
- Physics is frame-locked (confirmed by the study doc and the code above). Physics is not decoupled here; this is consistent with the out-of-scope rule.

### 1.3 Render call chain

- Object-level render entries (names from `include/gt4/RaceBase.h:26-35`, addresses from the header comments): `GranTurismo4::GameObjectPS3::update(float)` at `0x00101140`, `render()` at `0x00101148`, `internal_update` at `0x00101308`, `internal_render` at `0x001013D0`, `render(RenderContext&)` at `0x00101288`, `render(GSBuffer&)` at `0x001012B8`. `D:src/startup/unit_00101400/GranTurismo4__GameObjectBase__internal_render.cpp`: `internal_render` dispatches `vt+0x40`. **Confirmed (names), Unknown (which dispatch is called per frame and in what order).**
- Race render: `RacePS2Base__render(self, a1)` (`D:src/race/RaceBGMPS2_RacePS2Base/RacePS2Base__render.cpp`): guard `f_cd4 != 0 && sub->flag == 0`; if `f_d80` and `f_cf5b` in 1..2 it calls `func_00342400` (the render double buffer, section 2.3); else it either calls the hook `f_d8c` or runs four vtable entries (`vt+0x348, 0x350, 0x358, 0x360`) with `a1`. **Confirmed.**
- Render helpers (all `RacePS2Base__*` in `D:src/race/RaceBGMPS2_RacePS2Base/`):
  - `render_course`: `ModelSet2__texLOD(0)`, `CourseData__render_course` or `func_00397048`, `CourseData__render_course_for_renderer(s2, 0.0f)`, `ModelSet2__render(v, 0)` for a model list.
  - `render_post_effect`: the frame-feedback effect (section 2.2).
  - `render_reflection_cars`: sorts the entry list (`RaceEntryBase__sortEntryCar`), gets the reflection texture (`GSBuffer__getTexture`), and draws each car through its virtuals with `rate`.
  - `render_back_mirror`, `render_dash_panel`, `render_panel`, `render_concourse`, `render_other`, `render_exchange_tire`, `render_use_back_mirror`, `render_after`.
  - `computePitCamera` (not render; pose producer, section 3.3).
- A separate render routine `func_0044E660` (`D:src/engine/unit_0044D510/func_0044E660.cpp`, reached from `func_0033E960` in `RacePS2Base`): issues GS calls, builds two buffers (`Buf b0, b1`), a `VisionList__getVoronoi` visibility list, and calls `RaceEntryCar__update(1)` / `RaceEntryCar__update(0)` around draw calls (lines 60-70). **Confirmed: render calls an update-side function.**
- Render-side GS helpers in `unit_00101400` (`func_00105C48`, `func_00104B68`, `func_00109080`) are used for the 480i overlay and the screen transitions. Their call sites are **Unknown** (not named).

Answer to Q1: yes, there is a sim half (`RaceBase__update` -> `updatePlay`/`updateRace`, dt = 1/60) and a render half (`RacePS2Base__render`), but the boundary leaks in both directions (sim calls `CourseData__render_course_for_renderer`; render calls `RaceEntryCar__update`). The frame-level order is set by the donor VBlank, not by the race code. **High confidence.**

## 2. Render path: scene build, packets, and mutated state

### 2.1 Packet building and double buffering

- The decomp does not name the VU1 packet builder or the DMA/VIF builder. `knowledge/architecture.md:221`: "The 3D renderer: GS/VU1 packet building and the display-list code are not named; they sit in unnamed clusters." **Confirmed (decomp gap).**
- Repo-side VU1 and VIF path: EE builds VIF/DMA packets, VIF1 shadow parser (`vif.cpp`), MSCAL starts VU1 (AOT, `tools/vu_recomp`), XGKICK sends GIF to the GS front end (`docs/HIGH-FPS-STUDY.md` section 1). **Confirmed (repo docs).**
- Is it double-buffered? Two different things:
  - The guest waits for VBlank before building frame N+1 (section 1.1), so the EE does not build N+1 while N is being built. **Confirmed.**
  - The GPU overlaps: GS batches for N are rasterized on a 3-slot async ring while the EE builds N+1 (`native/gs_submit_ring.hpp`, GS-ASYNC-DESIGN). **High confidence.**
  - The guest's own render double buffer (`D_0061850C`, section 2.3) is a separate ping-pong of render targets. Its flip site in race code is **Unknown** (it is flipped in `PhotoPause` `func_003C5430.cpp:52,81`).

### 2.2 Per-render state mutations (what breaks "render twice")

Each item: file, what changes, effect on a second render of the same sim state.

1. **`CF5A` render shift register** (`RacePS2Base__render.cpp`: `f_cf5a = (f_cf5a << 1) | 1` per render; read in `render_post_effect` to choose a branch). Repeats change the bit history and therefore the post-effect branch. **Confirmed (code), High confidence (effect).** Guard: snapshot and restore `CF5A`, or count repeats as one.
2. **Frame-feedback accumulation** (`RacePS2Base__render_post_effect.cpp` and `func_003429E0.cpp`): when `CF58 != 0` and `CF40 != 0`, it writes `CF3C = max(1 - (1/60)/CF40, 0)` (decay with a frame constant), copies the previous frame into `D_0061850C` buffer 1 - cur, and writes VRAM at `base+0x134` plus fixed offsets `+0x23000` and `+0x46000` (via `func_004A9378(s0, b1, b2, 0x280, 0xE0)`, a 640x224-ish VRAM copy). It also blends with `func_004A3998(6, min(f, 0.5))`. Repeats accumulate twice. **Confirmed (code). Effect: motion-blur or frame-blend (Hypothesis, the name is not in the decomp).** Guard: skip post-effect on the extra presents (show the previous accumulated result), or accept it as an approximation.
3. **Render-target flip** (`D_0061850C`, two elements with `cur` at `+0x78`, element stride `0x3C`): read by `func_003429E0` and `func_00342400` (race) and by `func_00473900` (GS text, `unit_0046A050`); written by `PhotoPause func_003C5430` (`flip()`). A second render in the same interval reads the same `cur`, so it is safe only if the flip is not in render. **Confirmed (read sites), Unknown (race flip site).**
4. **Spark particles** (`D:src/race/ParticleManager_SparkParticle/SparkParticle__Render.cpp`): `flags |= 1` on first render (after which it draws a line from `last` to `pos`). The first render differs from later ones; repeats are harmless after the first. **Confirmed.**
5. **Spark spawn RNG** (`func_003C2160.cpp`, `pglmRandom()`): spawns particles with random speed and direction. The caller `func_00395C98` is unnamed; if spawn runs in render, repeats advance the RNG and diverge. **Unknown (caller).**
6. **Model state latch** (`mCarModelPS2__virtual_51.cpp`): writes a 200-byte state block at `+0x1DC`, sets `+0x15C = 1` once and `+0x1D8 = 0` otherwise, under a lock at `+0x11C`. Idempotent after the first call. **Confirmed (code), Unknown (call frequency).**
7. **Car list sort for reflections** (`RacePS2Base__render_reflection_cars.cpp`): `RaceEntryBase__sortEntryCar(list, ...)` reorders the entry list in place each call. Stable sort on unchanged keys is idempotent; **Hypothesis**.
8. **Entry-car toggles** (`func_0044E660.cpp:60-70`): `RaceEntryCar__update(1)` / `(0)` around draws. Body not in decomp (`RaceEntryCar` has structors only). **Unknown (side effects).**
9. **Counters in the object frame loop** (`D:src/startup/unit_00101400/func_00109080.cpp`): decrements `n7C`, `n80`, `n88` and sets `f74` per call, gated by `func_004A7C60() == 4` (a GS state query). Call site **Unknown**. If it runs per render, repeats advance the transition timers. **Unknown.**
10. **`CourseData__render_course_for_renderer` in the sim** (`updatePlay`): data for the renderer is produced in the sim update. A repeat render uses the same data, so it is safe if that data is not advanced by the call. **Unknown (body not in decomp).**
11. **Donor frame counter and dump:** `g_frames` advances only at VBlank (`DN:system.cpp:127-130`), so extra presents must not call `vblank()`. The F8 counters count VBlanks, so extra presents do not change the frame count (design rule). **Confirmed.**

Summary: items 1, 2, 4 and 6 are the hard ones (state changes that a repeat must see or skip); items 5, 7, 8, 9 and 10 need call-site or body reads. Item 2 is the main quality question: a repeated feedback pass changes the picture even when the transforms are identical.

### 2.3 Other render-side state (not repeated by the sim)

- Shadow and reflection targets: `render_reflection_cars` draws into a texture (`GSBuffer__getTexture(camera, pv)`) and calls `func_0038E7F8`/`func_0038EFF8` around each car; the reflection texture is written each render. Repeating it doubles the work and rewrites the same texture (idempotent if the inputs match). **High confidence.** Shadow passes: **Unknown** (no shadow functions named).
- LOD hysteresis: `ModelSet2__texLOD(0)` and `ModelSet2__texLevel` are called in render (`render_course.cpp`, `render_back_mirror.cpp`). `texLOD` may hold hysteresis state. **Unknown (body).** The owner note in STATUS (LOD or extra pass changes the showroom cost) is a Hypothesis already recorded in the journal (2026-10-15, slice 153).
- HUD and text: `RaceDisplay__render*`, `RaceMiniMap`, and similar `render_main` functions are in `src/race/`. Repeating them is safe if they only draw; their helper state is **Unknown**.

## 3. Transforms to interpolate

### 3.1 Camera (Confirmed API, Unknown source)

- `include/gt4/CameraBase.h` (generated from the decomp, addresses from the header comments): virtuals `getPositionX/Y/Z(float)` at `0x0044F1E0/F1F0/F200`, `getProjectionMatrix(float*, float)` at `0x0044F210`, `getModelviewMatrix(float*, float)` at `0x0044F280`, `getDirection(float*, float)` at `0x0044F2F0`. Every getter already takes a `float` argument: a time-like parameter. **Confirmed (signature), Unknown (what `t` means in each subclass).**
- `D:src/engine/CameraBase/CameraBase__getModelviewMatrix.cpp`: calls `vt[3]` with `t`, then `func_004A7550(target)` writes the matrix. The decomp's `vt[3]` is an 8-byte VEntry (not the header's C++ slot 3), so the mapping to `getPositionX` is **Unknown**.
- Camera subclasses: `DevelopCamera` (54 own methods, `0x371CA8`), `CameraSys::*` follow/on-board/heli/pit (`unit`s `src/race/DevelopCamera/`). Their update functions are not read here.
- Pit camera pose (**Confirmed**, `D:src/race/RaceBGMPS2_RacePS2Base/RacePS2Base__computePitCamera.cpp`): the race object holds a camera block at `+0x362C`: `float m[3][3]` at `+0`, `f24` at `+0x24`, flags `i30`, `i34`. `computePitCamera` copies it into an output struct each call. This is a snapshot-friendly layout: 9 floats plus two scalars.

Snapshot and lerp for the camera: 3x3 rotation (convert to quaternion, slerp), position (lerp). Feasible once `t` semantics and the pose block are confirmed for the on-board, follow and pit cameras. **High confidence (feasible), Unknown (which cameras use `t`).**

### 3.2 Car body and wheel matrices (Unknown locations)

- `include/gt4/RaceCarModel.h` and `include/gt4/mCarModelPS2.h`: no matrix or position fields (grep for matrix, position, transform, quat, wheel: nothing). **Confirmed (absence in headers).**
- `RigidBodyManager__Update(p+0x35E8, dt)` (called from `RacePS2Base__updatePlay`): the body is **not in the decomp** (`D:src/race/RigidBodyManager/` has structors only). The physics pose writer is therefore **Unknown**. This is the key missing piece for both B and C.
- `RaceEntryCar` (AI and replay cars): the entry holds a car object at `+0x2880` (`render_reflection_cars.cpp`: `car = F(n, 0x2880)`, vtable at `car+8`, virtuals at `+0x118`, `+0xA8`, `+0xF0`, `+0xF8`, `+0x40`, `+0xB8`). Transform fields inside that object: **Unknown**.
- Wheel matrices: `knowledge/architecture.md:169` says car, tire, wheel and wing are separate file sets rendered through `mCarModel(PS2)`. The wheel pose fields are **Unknown**.
- Replay ghost: `RaceBase__updateRace` drives replay from input frames (`replay+0x11c`, `s+0xD28`). The ghost car poses are therefore outputs of the same simulation driven by recorded inputs (no separate pose store). **Hypothesis.**
- Measured route (not done, proposed): the donor `save_state` writes RAM at a frame (`DN:system.cpp:269-282`, `save_state_frame`). Two snapshots at N and N+1 (the physics runs at 1/60 per VBlank) differ in exactly the pose words. Filtering float words that change at every VBlank and fit car-scale ranges locates the matrices without decoding the physics. **High confidence in the method; cost: one save per frame, offline.**

### 3.3 Other moving objects

- Particles (`ParticleManager__Update`, sim): positions advance in sim, so interpolation would need the same snapshot (positions only; render reads them). **Hypothesis.**
- Billboards and sprites (`engine/unit_00450248/BillboardSet__render*`): sim-driven, not measured.

Can transforms be snapshotted before and after each step and lerped? Yes for the camera (pose block at `+0x362C`, getters with `t`) and for any object whose pose is a plain float block written once per update. For car bodies, feasibility depends on finding the pose words (section 3.2). **High confidence (method), Unknown (car pose locations).**

## 4. Cost estimate on this machine (Ryzen 7 5700X, RX 9060 XT)

### 4.1 Owner F8 intervals after slice 157 (Confirmed counts, scene attribution Hypothesis)

Source: `private/manual-play/session-*/probe.log`, `manual (F8)` intervals with the following `exclusive wall ms/frame` line. Sessions with `measurement_mode=vu-split`. The logs carry no scene name; attribution uses the STATUS table (night 58.9, rally 59.9, showroom 60.0 post-157) and the GS fence signature (night: about 4.1-4.4 ms, see journal).

| Session (2026-10-15) | VBlanks/s | prims/frame | ee_dispatch | vu1_aot | vif_gif | gs_fence_wait | unattributed | host_vblank | Attribution (Hypothesis) |
|---|---|---|---|---|---|---|---|---|---|
| 061851 | 58.62 | 20,119 | 2.50 | 5.12 | 1.10 | 4.23 | 2.27 | 0.35 | night race |
| 084551 | 58.85 | 19,515 | 2.43 | 4.97 | 1.05 | 4.13 | 2.59 | 0.37 | night race |
| 091614 | 58.94 | 19,639 | 2.15 | 4.79 | 0.97 | 4.42 | 2.74 | 0.36 | night race |
| 163129 | 59.07 | 19,511 | 2.27 | 4.88 | 0.97 | 4.32 | 2.57 | 0.34 | night race |
| 063018 | 60.00 | 21,524 | 1.59 | 6.52 | 1.14 | 0.32 | 5.44 | 0.34 | race or showroom at 60 |
| 170744 | 55.50 (short) | 37,229 | 2.00 | 9.77 | 3.54 | 0.00 | 0.64 | 0.36 | heavy scene (Unknown) |
| 160746 | 51.92 (short) | 38,584 | 1.94 | 9.88 | 3.24 | 0.00 | 2.19 | 0.38 | heavy scene (Unknown) |
| 103545 | 59.53 | 0 (GS counters off) | 2.22 | 7.06 | 1.99 | 0.00 | 3.30 | 1.65 | Unknown |
| 084551 (2nd) | 59.90 | 25,452 | 2.20 | 7.57 | 1.36 | 0.00 | 4.09 | 0.34 | race at 60 (Hypothesis) |

Other readings:
- Pre-157 (journal, slice 153-154, `--mode vu1-flags`): showroom VU1 16.2 ms (slice 153) and 9.3 ms (slice 154); arcade race 8.4 ms (153), 5.7-5.9 ms (154). **Confirmed (owner table).**
- Periodic lines (`[perf] exclusive wall`, all intervals, 2026-10-15): ee_dispatch averages 1.9-7.3 ms per interval and vu1_aot 0-6.2 ms; the medians are below the averages above. Use the F8 manual rows as the reference.
- MTVU worker (slice 171 measure session `123739`, 58.34 VBlanks/s, scene Unknown): worker `vu1_program 6.05 ms/frame`, `output_path12 0.35`, `busy 6.40`, `idle 8.33`, `jobs/frame 7683`; EE wait by reason total 0.027 ms. **Confirmed (counts), scene Unknown.**
- Night race: GS fence wait 4.1-4.4 ms in every night-like interval (cause suspected lens-flare readbacks, `docs/journal/2026-10-15.md` slice 155). **Confirmed (counts), cause Hypothesis.**

### 4.2 Split of EE time: sim versus render (Unknown, not measured)

- `ee_dispatch` is one category. The sim (`RacePS2Base__updatePlay`) and render (`RacePS2Base__render*`) are both EE code in the same dispatch. The split is **Unknown**.
- Upper bound from the categories: the EE thread busy time per guest frame is about `ee_dispatch + unattributed + vif_gif` = 2.2 + 2.6 + 1.1 = **about 6 ms (night), 2.2 + 5.4 + 1.1 = 8.7 ms (60 fps interval)**. Render's share of that is **Hypothesis: 3-6 ms**, since the sim is dt-driven physics plus particles and the render is traversal, packets and VIF. This is the number to measure.
- Measurement to add (not run): one probe scope around `RacePS2Base__updatePlay` and one around `RacePS2Base__render` (decomp names give the entries; the donor probe categories need a new category). This is a new F8 category, so it is a source change, not part of this study.

### 4.3 Extra render pass cost, and fit at 120 and 144 Hz

Per extra present (render pass, no sim): EE render share (Hypothesis 3-6 ms on the EE thread, see 4.2) + VIF/GIF (1.0-3.5 ms, measured) + VU1 AOT (4.8-9.8 ms, measured, or on the MTVU worker) + GS raster on GPU (Unknown; GPU timestamps are not available, `PS2_GPU_PROFILE` throws in native init, `docs/HIGH-FPS-STUDY.md` section 4).

Frame budgets: 120 Hz = 8.33 ms per present (1 extra render per guest frame); 144 Hz = 6.94 ms (1.4 extra renders per guest frame).

| Case | Race-like (VU1 ~5 ms, night) | Heavy (VU1 ~9.8 ms, 37-38k prims) |
|---|---|---|
| 120 Hz, EE thread (no MTVU) | EE per guest frame ~9-11 ms, plus one extra render 3-6 ms: about 12-17 ms of 16.7 ms: **borderline** | ~16-21 ms: **fails** |
| 120 Hz, MTVU (VU1 on worker) | worker 5 ms + output 0.3 per present (8.33 ms budget): **fits**; EE +3-6 ms: fits | worker 9.8 ms > 8.33: **fails** |
| 144 Hz, MTVU | worker 5.3 ms per present (6.94 budget) and 1.4 renders per frame: **borderline** | **fails** |

Consequence (Hypothesis, to verify with the measurement in 4.2): 120 Hz is plausible for race-like scenes only with MTVU on; heavy scenes need VU1 under about 6.5 ms before 120 Hz, and 144 Hz needs VU1 under about 5 ms. The showroom (VU1 6.5 ms after slice 157 if the 063018 interval is it) sits at the limit.

The F8 `vu1_aot` figure already counts all VU1 calls, so the extra pass adds the same VU1 time again, not more. VU1 is the largest category in every interval above.

## 5. Hook mechanics in this recompiler

### 5.1 What exists (Confirmed)

- The EE is AOT-translated C++ (`unit_XXXX.cpp`, `switch(entry)` labels, one unit per 16 KiB window; `CLAUDE.md` Architecture). A call to an address without a translated entry halts. There is no EE interpreter on the normal path. **Confirmed (CLAUDE.md).**
- Savestate: `DN:runtime/src/system.cpp:230-266` (`machine_state`) serializes `g_cpu`, RAM (`kRamSize`), scratchpad, timing, events, kernel, hw, DMAC, VIF, VU (`vu::state`), IPU, GS, GIF, IOP, SIF. `save_state`/`load_state` at `DN:system.cpp:269-304`. A frame-triggered save exists (`save_state_frame`, `DN:system.cpp:140`). **Confirmed.** Consequence: VU1 memory, GS registers and VRAM are part of the snapshot, so a replay of one frame from a snapshot is possible offline.
- Host-callable guest functions: the donor has `call_guest(address, a0..a3)` on the IOP side (`DN:runtime/src/iop/kernel.hpp:87`). I did not find an EE equivalent. **Unknown (EE re-entry API).**
- Frame dump: `DN:system.cpp:87-92` (`dump_frame`, PPM per `frame_every`). Usable as the image source for the proof.

### 5.2 Options for "render with interpolated state"

- **Option 1, host re-entry with state swap (offline or heavy):** save at VBlank N; write the lerped pose words into RAM at the addresses found in 3.2 and 3.1; run the EE render entry (`RacePS2Base__render`) through the donor's host call path (needs an EE re-entry API, not found); collect the GS batches as usual; then `load_state` to restore. Cost of save and load: RAM 32 MB plus VRAM and VU memory per pass. Hypothesis: 10-30 ms per pass, too slow for real time, fine for proof. **Feasible offline if the EE entry can be called.**
- **Option 2, overlay in the producer (real time):** patch the translated render code to read transforms from a side buffer written by the sim (`scripts/ee_producer.py`, the `unit_XXXX.cpp` generator). This is C in section 6. Needs the pose addresses and a second entry that does not mutate the state in section 2.2.
- **Option 3, GS-side (A):** no EE re-entry at all. The GS primitive lists of N and N+1 are interpolated on the GPU or a worker (`docs/HIGH-FPS-STUDY.md` section 3.A). Cheapest real-time path, lowest EE coupling.

### 5.3 State to save and restore for a repeat render

- Must restore or freeze (Confirmed list from section 2.2): `CF5A` (RacePS2Base), `CF40`/`CF3C` and the VRAM feedback buffers (`base+0x134`, `+0x23000`, `+0x46000`), `D_0061850C` `cur`, the particle `flags` latch (only the first draw), the car model latch `+0x15C/+0x1D8/+0x1DC`, the entry-car list order.
- Must not be restored: sim state (positions, physics, timers, counters advanced at VBlank), RNG if the spawn is in sim (Unknown, item 5).
- VU1 memory: the VU1 program keeps state across calls (`docs/HIGH-FPS-STUDY.md` section 2, VU1 persistent memory). A repeat render must either restore VU memory or the program must be stateless per call. **Unknown for GT4 (needs a differential run).**
- VIF/DMA chains: packets are built per frame by EE code; a repeat builds new packets, so the DMA chain state is reset per pass. The DMAC and VIF state in `machine_state` (`DN:system.cpp:250-256`) covers it.
- GS registers and VRAM: covered by `gs::state` in the snapshot. Feedback VRAM (section 2.2 item 2) is the state that carries a repeat's result into later frames.

Feasibility without an EE interpreter: yes, the snapshot and AOT re-entry do not need one, provided the EE entry can be called from the host. Risk: the first render's VRAM feedback is visible to the second (item 2), so a repeat is not idempotent unless the feedback is disabled on the extra presents.

## 6. Verdict and effort

| Approach | Feasible? | Main risk | Effort (agent sessions) | Verdict |
|---|---|---|---|---|
| C: repeat EE render with lerped camera and object transforms (overlay or re-entry) | Possible in principle | state in 2.2 (feedback, latches, sort, RaceEntryCar, RNG); car pose addresses unknown; EE re-entry API unknown; EE cost 3-6 ms per pass | 15-25 | Not first. Real-time only after 2.2 and 3.2 are closed |
| C-offline proof (savestate, host re-entry) | Yes in principle | EE re-entry API; savestate cost | 2-3 | Recommended as the proof |
| B: VU1 input interpolation | Yes in principle | VU1 cost per extra present (4.8-9.8 ms); VU1 field typing | 10-15 | Borderline with MTVU for race-like scenes only |
| A: GS primitive interpolation | Yes on paper | disocclusion, HUD, feedback VRAM; GPU cost unmeasured | 6-10 (in engine), 2-3 (oracle) | Cheapest real-time candidate (unchanged from HIGH-FPS-STUDY) |
| D1: repeat presents / VRR | Yes | no real motion gain | 1-2 | Owner test first |

Verdict for the native render rate: **feasible as an offline proof, not yet feasible in real time with the present render cost.** The real-time candidates remain A (cheapest) and B (after VU1 falls below about 5 ms, with MTVU). C is the most invasive and needs the state in section 2.2 closed first.

Main risks:
1. Feedback VRAM (`func_003429E0`): a repeat changes the picture; needs an explicit policy.
2. Unknown car pose storage (section 3.2): blocks B and C.
3. EE render cost share (section 4.2): unmeasured; decides 120 Hz feasibility.
4. VU1 memory persistence (section 5.3): unknown for GT4, needs a differential check.
5. Race-side flips and latches (`D_0061850C`, `CF5A`, model latch): flip sites in race code not found.
6. EE entry re-entry API: not found; blocks any real-time C.
7. Menus are 480i (interlaced, `GT4_FORCE_480P` applies to race only): extra presents in menus need field handling; out of scope for races.

## 7. Minimal proof steps (cheapest first)

1. **Pose-word discovery by RAM diff (no code change beyond existing save_state; offline):** save state at VBlank N and N+1 (`save_state_frame`), diff RAM, keep float words that change at every VBlank in plausible car-scale ranges. Expected output: a short list of candidate addresses for car body and camera pose (cross-check with the camera block at `RacePS2Base+0x362C`). **Decides section 3.2.** Confidence High in method.
2. **Repeat-render determinism check (offline):** from the N snapshot, run the frame twice with `load_state` in between (needs one EE entry call to re-run the frame: if no EE re-entry API exists, run the whole frame to the next VBlank twice in two processes from the same snapshot and compare `frame_*.ppm`). Expected outcome: identical images only if the feedback (section 2.2 item 2) is off. The diff image quantifies the feedback, which is the proof's quality gate. **Decides whether "render twice" is idempotent.**
3. **Camera-only lerp at t = 0.5:** after step 1 gives the camera pose, lerp the camera block between N-1 and N+1 snapshots, re-render (or re-present the GS of N with the lerped camera through the overlay) and compare with a real frame N+0.5 (none exists: use the lerp against the real N+1 at half motion for a static scene such as the replay of a parked car). **Quality check for C without car lerp.**
4. Only then: EE re-entry API design, and the producer overlay (C), or the GS oracle (A, HIGH-FPS-STUDY section 6).

Owner decisions needed: (a) whether feedback-based post effects may be skipped on extra presents (quality trade); (b) whether approach A (GS interpolation, no re-render) counts as "real rasterized frames" under the owner rules, since the request here is a re-run of the render path.

## 8. Open questions (Unknown), by priority

1. Car body and wheel pose storage (RigidBodyManager body missing; car object fields unknown).
2. Which guest function issues syscall `0x73` (the vsync wait) and the frame-loop order of the four object-level entries.
3. Meaning of `t` in `CameraBase` getters per subclass, and the camera update that writes `+0x362C`.
4. Race-side flip sites of `D_0061850C`, and whether they run in render.
5. Body of `CourseData__render_course_for_renderer` and `ModelSet2__texLOD` (LOD state).
6. Whether spark spawn RNG is in sim or render (`func_00395C98` caller).
7. VU1 memory persistence across MSCAL calls for GT4 (differential run).
8. EE-side host re-entry API (none found).
9. EE render share of `ee_dispatch` (needs a probe scope, section 4.2).
10. Scene attribution of the F8 intervals (logs carry no scene name; the probe should write one).

## 9. Sources read

- Repo: `CLAUDE.md`, `docs/HIGH-FPS-STUDY.md` (full), `docs/journal/2026-10-15.md` (scene tables, slices 153, 154, 155, 171-172), `docs/STATUS.md` (current state table), `docs/MTVU-PLAN.md` (section headers, 11.7, 12), `native/window.cpp` (`release_frame`, `on_vblank`), `native/gs_submit_ring.hpp` (ring, by reference from the study).
- Decomp: `include/gt4/RaceBase.h` (vtable names and addresses), `include/gt4/CameraBase.h` (virtuals with addresses), `include/gt4/RaceCarModel.h`, `include/gt4/mCarModelPS2.h`, `include/gt4/CarGeometry.h` (negative results), `knowledge/architecture.md` (sections 1, 8, unknowns), `src/race/RaceBase/RaceBase__update.cpp`, `RaceBase__updateRace.cpp`, `src/race/RaceBGMPS2_RacePS2Base/RacePS2Base__render.cpp`, `RacePS2Base__updatePlay.cpp`, `RacePS2Base__render_course.cpp`, `RacePS2Base__render_post_effect.cpp`, `RacePS2Base__render_reflection_cars.cpp`, `RacePS2Base__render_back_mirror.cpp`, `RacePS2Base__computePitCamera.cpp`, `func_003429E0.cpp`, `func_00342400.cpp`, `src/engine/CameraBase/CameraBase__getModelviewMatrix.cpp`, `src/engine/unit_0044D510/func_0044E660.cpp`, `src/engine/unit_00450248/ModelSet2__render.cpp`, `src/engine/unit_0046A050/func_00473900.cpp`, `src/gamedata/mCarModelPS2/mCarModelPS2__virtual_51.cpp`, `src/race/ParticleManager_SparkParticle/SparkParticle__Render.cpp`, `func_003C2160.cpp`, `src/race/RaceEntryCar/`, `src/race/RigidBodyManager/` (structors only), `src/startup/unit_00101400/` (GameObject render/update, `func_00109080`, `func_00104B68`, `func_00105C48`, `PlayStation3__RenderLoopGetFlipCycle`), `src/startup/CarIconMaker/GranTurismo4__GameObjectPS2__internal_update.cpp`.
- Donor: `runtime/src/system.cpp` (clock, `vblank()`, `dump_frame`, `machine_state`, `save_state`/`load_state`), `runtime/src/kernel.cpp` (syscall `0x73`, `on_vblank_start`), `runtime/src/iop/kernel.hpp` (`call_guest`).
- Owner logs: `private/manual-play/session-20261015-*/probe.log` (`manual (F8)` intervals and `exclusive wall ms/frame` lines, `[mtvu]` lines in session 123739), `result.json` (mode arguments only; no scene name).

Not opened: any PCSX2 source; no repository file edited; nothing built; no game run.
