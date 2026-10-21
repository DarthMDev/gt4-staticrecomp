# Native variable frame rate study (N1 fixed-step render interpolation vs N2 variable dt)

Date: 2026-10-21. Research only (no build). This is the study for TASK-082, planned after 1.0.0; the owner accepted its
recommendations on 2026-10-21 (see TASKS.md).
Agent status: **not PCSX2-tainted** (no PCSX2 source, no `gt4recomp-gs` fork, no `private/tooling/pcsx2-*`, no
`private/slice189-pcsx2-ref` opened). The BLACK-XBERecomp repository was **not** cloned or read; only the owner's
prose notes (`private/references/black-xberecomp-notes/BLACK-XBERecomp-FPS-Desbloqueado.md`) were used, for ideas only.

Labels: **Confirmed** (read in decomp/ELF/translated code or measured in RAM), **High confidence**, **Hypothesis**,
**Unknown**. Addresses are SCUS-97328 v2.00 guest addresses (identical to the gt4-decomp target).

Sources: decomp `private/slice151-research/gt4-decomp` (src, knowledge), ELF
`private/reconstructed/SCUS_973.28.elf` (vtables read with a throwaway script), translated units
`private/slice208-ee-entries-producer/generated`, `docs/data/ee-names.csv`, `docs/HIGH-FPS-STUDY.md` (main and
branch `task/031-high-fps` section 9), `docs/native-renderer/STUDY.md` (branch `native-renderer/main`),
`docs/ULTRAWIDE-NATIVE-STUDY.md`, `docs/GS-STREAM-PROTOCOL.md`, `docs/GS-LIVE.md`, `docs/SCENE-OBSERVATION.md`,
`native/pacing.{hpp,cpp}`, `native/window.cpp`, donor `runtime/src/system.cpp`, owner F8 logs
`private/manual-play/session-*/probe.log` (read only), ai-memory pages (current-state, project-rules,
native-renderer-branch).

Headless evidence (this study): `private/fps-study-01/` (script `scripts/run.sh`): slice211 `gt4vulkan.exe`, no
window, TASK-041 race schedule (Arcade, El Capitan, Ford Ka, 5 AI), copy of the `slice110-menu-pilot/name-control4`
seed card, RAM dumps (`PS2_DUMP`) at frames 9,600 (`r9600`) and 9,660 (`r9660`), frame dumps every 300 frames. Both
runs exit 0, no "no translated code". Frame 9,600 shows the race (HUD race time 0'10.950).

---

## 0. Summary

1. **GT4 already has a clean update/render split, and its render code is built for a render time `t`.**
   - A dedicated main-loop thread (`func_001016E8`) runs, per frame: update of every game object (manager A),
     render of every game object (manager B), DMA-safe "idle", then a flip that waits for at least
     `flipCycle` VBlanks (Confirmed).
   - The race object (here `RaceArcadeSingle` at 0x01BF6080) is in both managers. Its update calls
     `RaceBase::updateRace` with a constant dt = 1/60 (Confirmed). Its render calls
     `render_world(buf, t = 0.0f)` (Confirmed).
   - The live race camera has **interpolating virtuals**: with `t != 0` it lerps between two stored pose sets
     (`func_00379818`) and two fovs (`func_0037A2F0`); with `t == 0` it loads a cached matrix (Confirmed). Cars
     receive `t` too (whether they use it is Unknown).
   - `RaceBase::updatePlay` already contains a **fractional accumulator that runs N fixed 1/60 simulation steps
     per frame** (B-spec "accelerated time" and a 0.05 slow motion), and a `getFrameInterval` = flip cycle hook
     (Confirmed). The engine was designed for "render at one rate, simulate at fixed 1/60" in principle.
2. **Race time is an integer step clock**: `DynamicsConductor+0xF898` advances by exactly 50 units of 1/3000 s per
   simulation step; lap and total times = value / 3 in ms (Confirmed, RAM: +3,000 in 60 frames). Replays and
   ghosts are recorded inputs indexed by frame (High confidence). Any change of the physics step changes lap times
   and breaks replay/ghost determinism.
3. **N1 (fixed 60 Hz simulation plus render-only passes with interpolated or extrapolated state) is feasible and
   respects the scope rule.** Physics, AI, timers and the guest VBlank stay exactly as now. It needs:
   a main-loop ELF patch (render-only sub-loop, re-translation, new `ps2game.lib`), a host call for the frame plan,
   a dispatch hook that feeds `t` to `render_world`, a guest-clock freeze during render-only passes, an extra-present
   path (CoreGS and a GS stream 1.3 record for gt4-gs), and a present scheduler in `native/pacing`.
4. **N2 (BLACK-style variable dt)** is acceptable only for cosmetics (post-effect feedback decay, particle/smoke and
   course-VM animation, menu and HUD tweens). **N2 for vehicle physics (DynamicsConductor, tyres, AI, rigid bodies,
   lap clock) is the out-of-scope "change of the game's physics timing"**; it would need an explicit owner decision
   that accepts non-PS2 lap times, broken replays/ghosts and months of solver work. Recommendation: reject.
5. **Cost is the limit above 60.** Owner F8 race intervals: EE thread CPU 6.5-12 ms/frame (translated game code only
   1.4-2.9 ms of it), VU1 worker 3.3-8.5 ms, readbacks 0-2 ms, critical path 10-19 ms of 16.68. A render-only pass is
   most of that (Hypothesis: 7-15 ms critical path). So **N1 at 120 Hz does not fit in race scenes today**; 72-90 Hz
   may fit in light scenes. 120/144 need render-only passes about 2x cheaper (skip env map, mirror, reflection and
   readback on extra passes; VU1 and stream-path savings).
6. **Below 60 is the cheap, high-value half of N1:** "render skip" (update every guest VBlank, render only when the
   host keeps up) keeps game speed at 100% down to 20 presented FPS with the simulation bit-identical, using the
   same main-loop hooks. Audio and IOP are untouched because the guest VBlank stays 60 Hz.
7. **Recommended order:** P0 probe (shared with TASK-045) → P1 render skip below 60 (new, user-visible, low risk) →
   P2 camera-only render-only pass at 120 Hz in Time Trial (proof) → TASK-045 GS interpolation stays the practical
   route to 120/144 until render-only passes are cheap enough; N1 cars/scene follow only if P2 passes its cost gate.

---

## 1. GT4's frame loop

### 1.1 Main loop thread (Confirmed)

`func_00101858` creates a thread whose entry is `func_001016E8` (stack `D_006D67E0`, 0x8000 bytes) and stores its
id in `D_00617D80`. Thread stacks in an owner log (`session-20261017-162523`, `[thread] stack: ... 00101818 00101718
... 001016E8`) show this thread blocked in the flip, so it is the live loop.

```
func_001016E8 (main loop, thread):
  func_00107730, func_00107788            init display (wait 1 VBlank, set up pages)
  do {
    func_004A0690()                       PGL lock (scratchpad 0x70000110 sema, owner, depth)
    func_001077F8()                       swap packet double buffer D_006184C0.cur, open new DMA chain (004A1A00/004A1BB0)
    if (missedLastFlip) func_00578B10(1000)
    func_00101400()                       UPDATE then RENDER:
       if (!D_00617CB0) forEach(manager A D_00617CB8, func_00100EE0 = vt+0x60 internal_update)
       forEach(manager B D_00617CF8, func_00100F50 = vt+0x68 internal_render)
    func_00107840()                       wait DMA ch1/ch2 (func_004A0F68(1), (2))
    func_004A06F0()                       PGL unlock
    func_00101430()                       forEach(manager B, func_00100FC0 = vt+0x88 idle)  [race: DMAsafe]
    func_001015B8()                       debug pad combos (D_00617CB0 debug pause, D_00621320 toggle)
    lock D_006D67B0; switch the "current project" D_00617D40/44 (old vt+0x48 terminate, new vt+0x10 start); unlock
    missedLastFlip = func_00107878(D_00617D84)   FLIP
  } while (!D_00617D84)
```

`forEach` is `func_00109580(manager, functor)`: a locked walk of a priority-sorted list of nodes
`{object, ?, next, ?, priority}` (`GameObjectManager::append` 0x00109478). Functor calls are `jalr` (function
pointer), and the object methods are virtual: **every per-object update/render/idle call goes through our runtime
`dispatch`** (Confirmed by the translated code shape; ULTRAWIDE-NATIVE-STUDY section 5).

### 1.2 Game objects (Confirmed, ELF vtables)

Vtable entries are 8 bytes `{s16 delta, s16 index, u32 fn}`; the object's vptr is at +0x64. RacePS2Base vtable
0x00679378 (RaceArcadeSingle uses 0x0067BAD8 with the same slots):

| offset | function | role |
|---|---|---|
| +0x28 / +0x30 | `GameObjectBase::controlFetch` 0x109830 / `controlFeedback` 0x109868 | pad read / force feedback |
| +0x38 | `RacePS2Base::update` 0x33C470 → `RaceBase::update` 0x389638 | simulation |
| +0x40 | `GameObjectPS2::render` 0x101148 | builds the 3 page descriptors from `D_0061850C`, calls +0x78 |
| +0x60 | `GameObjectPS2::internal_update` 0x101308 | always controlFeedback; if running: controlFetch then update |
| +0x68 | `GameObjectPS2::internal_render` 0x1013D0 | if state < 2: `GameObjectBase::internal_render` → +0x40 |
| +0x78 / +0x80 | `render_2` 0x101288 / `RacePS2Base::render` 0x33D970 | the race frame |
| +0x88 | `RacePS2Base::idle` 0x33CE40 → vt+0x3C8 `DMAsafe` 0x33CD78 | course and per-car DMA-buffer swaps after the DMA |

RAM at frame 9,600 (Confirmed): manager A = {0x006D6708 (vt 0x659A28), 0x006DE820 (vt 0x659BA0), 0x01BF6080
`RaceArcadeSingle`}; manager B = {0x006D6708, 0x01BF6080, 0x006DE820 (priority 10000, last)}. `D_00617D40` (the
project slot) is 0 during the race.

### 1.3 Race update (Confirmed)

```
RaceBase::update (0x389638), once per main-loop iteration:
  if (vt+0x248 updateCommon()) {
    +0xD30 += +0xD34                      a float accumulator (0 in the sample)
    +0xD50 countdown → vt+0x160 replayStart at 0   (slow motion window, see below)
    if (!+0xCD8 paused) vt+0x250 updatePlay(dt = 1/60) else vt+0x268 updatePause
    vt+0x270 updateOther
  }
  RaceEventQueue event 0xA → vt+0x280 raceEnd

RaceBase::updatePlay (0x389990, called first by RacePS2Base::updatePlay 0x33C4F0):
  time-limit check (+0xD30 vs org+0xD0)
  RaceOrganization::raceRealTimeUpdate(org, dt * getFrameInterval())   getFrameInterval = flip cycle (RacePS2Base)
  if (isAccelarated())  { r = getAcceleratedRatio(); f = +0xD40 + r; n = (int)f; +0xD40 = f - n;
                          for i < n: +0xD44 = i; vt+0x258 updateRace() }
  else                  vt+0x258 updateRace()
  vt+0x260 (RacePS2Base virtual_75: view-offset matrix from the pad into camera+0xA40)
RacePS2Base::updatePlay adds, once per frame with dt: FlareClass::updateFlare, RaceSmoke::update,
  RigidBodyManager::Update, ParticleManager::Update, smoothing values +0xCF38/+0xCF3C/+0xCF48,
  CourseData::render_course_for_renderer(course, +0xCF5C = 1/60)  (course VM animation)

RaceBase::updateRace (0x389B08) + RacePS2Base::updateRace (0x33C858), once per simulation step:
  RaceOrganization::racePreUpdate (frame-count timers: timer = count * 60, decremented per step)
  RaceCourse::update, func_00394D80(course, dt = 1/60), DynamicsConductor::Update(...)   vehicle physics, AI
  func_0034DB30(race+0x70 DynamicsConductor, 0), updatePitmen(dt), lap-diff events, replay frame (+0xD28)
  camera manager vt+0x18 applyModelview(0.0), per-car matrix work, car model vt+0x110(particles, 0.0)
  RaceCourseSound::playUpdate, input vt+0xB0 (records inputs for replay)
```

- `isAccelarated` (0x38A2B0) = `+0xD50 > 0 || +0xD38`; `getAcceleratedRatio` (0x38A2C8) = 0.05 while `+0xD50 > 0`,
  else `+0xD3C`; `setAcceleratedTime(r)` (0x38A288) sets `+0xD38 = (r != 1)`, `+0xD3C = r`. RAM: `+0xD38 = 0`,
  `+0xD3C = 1.0`, `+0xD40 = 0`, `+0xD50 = 0` in the race (Confirmed). So: the game runs N whole 1/60 steps per
  frame with a carried fraction for B-spec speed-up, and one step every 20 frames in the 0.05 slow motion.
- Only `updateRace` (the vehicle simulation) is multiplied; flare, smoke, particles, rigid bodies and the
  real-time countdown stay once per frame (Confirmed in code). This tells us which systems the designers treated as
  "per presented frame" and which as "per simulation step".

### 1.4 Race render (Confirmed)

`RacePS2Base::render` (0x33D970): if a cross-fade capture is pending, `func_00342400`; else if a render callback is
set (`+0xD8C`), call it; else `render_world(buf, 0.0f)` (vt+0x348, `jalr`), `render_post_effect` (vt+0x350), shift
the history byte `+0xCF5A`, `render_panel` (HUD, vt+0x358), `render_other` (vt+0x360). `render_world` (0x33DEA8)
first runs `func_0033E8E0`, which **increments the render frame counter `+0xE32C`** (RAM: 0x777 → 0x7B3 over 60
frames, one per frame), then the env map, main 3D pass, glare, mirror, smoke, flares, filters (native-renderer
STUDY 2.1). Every pass takes `t`. `RacePS2Base::render_world_split` (0x33FD88) exists for split screen.

### 1.5 Cameras and `t` (Confirmed)

- Live race camera manager: embedded at race+0x80 (`RaceBase::getGT4CameraManager` = this+0x80), vptr 0x0067A078.
  `applyProjection(t)` = 0x00373490, `applyModelview(t)` = 0x00373548. Both pick the active camera
  (`vt[27](index)`; records at +0x158, stride 0x19C; RAM: index 0, five cameras of vtable 0x0067A5D8) and call:
  - `t == 0`: camera slot 13 `func_0037A208` (projection from current fov) / slot 14 `func_0037A3D8` (load the
    cached matrix at camera+0x100);
  - `t != 0`: slot 16 `func_0037A2F0` (fov = fov + (target - fov) * t) / slot 17 → `func_00379818`, which
    **interpolates two stored poses**: three angle triples (+0x64/+0xD0, +0x4C/+0xB8, +0x34/+0xA0, through the
    angle lerp `func_0036EC78`) and three translations (+0x58/+0xC4, +0x40/+0xAC, +0x28/+0x94), then rebuilds the
    modelview with rotate/translate calls.
  - Which set is "previous" and which is "current", and whether `t = 1` reproduces the cached matrix exactly, is
    **Unknown** (P0 measures it).
- Recorded/replay scene camera (vtable 0x006792D8): `func_0033B3B0` lerps `keys[1-cur]` → `keys[cur]` by `t`; keys are
  pushed by `func_0033B0E8` (swap `cur`, copy). Confirmed.
- Cars: `render_cars(aux, car, ..., t)`; the car render object (entry+0x2880, vptr at +8) gets `+0x118(t)` in render
  and `+0x110(particles, 0.0)` in update; `RaceCarModel::virtual_34` (0x3905B0) passes `t` in `$f12` to
  `func_004596A8` (pose to PGL rotations). Whether the car pose uses `t` (interpolation, extrapolation or ignored)
  is **Unknown** (decomp drops the float; P0 measures).

### 1.6 Flip, VBlank wait and the flip cycle (Confirmed)

`func_00107878(quit)` (the flip): `func_004AA4A0`, `func_004A1C98` (close the DMA chain; kick when PGL mode 0),
`func_00105510(page[cur], D_006186E0 ^ 1)` (field flag), interrupts off around `func_004A20C8` (wait VIF1, VU1 and GIF
idle, with reset on timeout), then the VBlank wait:

- `func_004A22C0(frames)`: scratchpad timing block at 0x70000050 `{now, last, ...}`. With `frames = -flipCycle` it
  waits (SleepThread in `func_004A21F0`, woken by the VBlank handler) until `now - last >= flipCycle`, returns 1 if no
  wait was needed (a missed flip), then `last = now`. `D_006186DC != 0` forces `frames = -1`.
- `flipCycle` = `D_006186E4`, read by `PlayStation3::RenderLoopGetFlipCycle` (0x107AC8), written by
  `func_00107AB8` ("set flip cycle"). Callers: `RacePS2Base::virtual_141` (0x33CF08, from `RaceBasic::terminate`
  and `RaceFreeRun::updatePause` with 1), `RaceMonitor` (0x3D8A9C, 1), media object `func_001C8990` (2) and its
  destructor `func_001C8778` (back to 1). RAM in the race: 1. **Hypothesis:** the media player (movies) runs at
  30 Hz with flip cycle 2.
- The VBlank counter `now` is advanced by the game's INTC VBLANK_S handler 0x004AB430 (GS-STREAM-PROTOCOL 7)
  (High confidence: the handler address is documented; the increment itself was not read).
- Then the flip toggles the draw/display pages: `D_0061850C.cur` (+0x78) and the second page set
  `D_006184D0+0xB8` (+0x78), and `func_00105608(page)` loads DISPFB/DISPLAY values into the PGL display shadow
  (`func_004A0B78`/`func_004A0C10`). **High confidence:** the five privileged writes seen every VBlank in GS-LIVE 9.2
  (PMODE, SMODE2, DISPFB2, DISPLAY2, BGCOLOR) come from that shadow at the VBlank, so a flip without a VBlank would
  not change the displayed page by itself.
- `RacePS2Base::getFrameInterval` (0x33CEF0) = flip cycle; `RaceBase::getFrameInterval` (0x38A2E8) = 1.

### 1.7 Per-frame counters and timers

| Counter / timer | Where | Advances per | Label |
|---|---|---|---|
| Guest VBlank count | scratchpad 0x70000050 (`now`), PGL wait | VBlank (handler 0x4AB430) | Confirmed (reader), High (writer) |
| Render frame counter | race +0xE32C (`func_0033E8E0`) | render pass | Confirmed (RAM +60/60 frames) |
| Render history bits | race +0xCF5A (shifted in `RacePS2Base::render`) | render pass | Confirmed |
| Dither toggle | `func_0033DC48` (DIMX per frame) | render pass | Confirmed (native-renderer STUDY) |
| **Race clock** | `DynamicsConductor` (race+0x70 → 0x01C08640) +0xF898 | **+50 per simulation step** (1/3000 s units) | Confirmed (32,950 → 35,950 over 60 frames; HUD 10.95 s ≈ 32,950/3 ms) |
| Lap/total time | `DynamicsConductor::CurrentTotalTime` = F898/3 ms; `CurrentLapTime_Normal` = F898/3 − lap start | derived from the step clock | Confirmed (code), so lap times are step counts |
| Display smoothing of time | `func_003F1148` (12-sample average into +0xF8BC) | step | Confirmed (code) |
| Race pre-start / countdown | `RaceOrganization::racePreUpdate`: `timer = count * 60`, −1 per step | step | Confirmed |
| Time-limit and message timers | `raceRealTimeUpdate(dt * flipCycle)`: +0xC8 −= dt*60, +0xD4 −= dt | frame (real time) | Confirmed |
| Accelerate/slow-motion fraction | race +0xD40 | frame | Confirmed |
| Replay frame | replay +0x11C → race +0xD28; ending frames `+0xD2C` (left × dt, 60-frame threshold) | step | Confirmed (code) |
| Replay/ghost data | `load/saveReplayInputs`, `load/saveGhostInputs`, input vt+0xB0 every step | step (inputs re-simulated) | High confidence |
| Course VM animation | `render_course_for_renderer(course, 1/60)` in updatePlay | frame | Confirmed |
| Flare, smoke, particles, rigid bodies | `RacePS2Base::updatePlay(dt)` | frame (1/60 const) | Confirmed |
| HUD widgets | `RaceValueDisplayBase::update` (1/60 constant), `GTMiniLogo::update` | frame | Confirmed (constant present) |
| Menu actors | `mButtonActor`, `mMagnifyActor`, `mSwitchActor`, `mFlashFace`, `mFlashPS2` (1/60 constants) | frame | Confirmed (constant present) |
| Sound | `RaceCourseSound::playUpdate`, `RaceCarSound::setDive` in updateRace; IOP sound driver on IOP VBlank/timers | step / IOP | High confidence |
| Movies | media object with flip cycle 2 | flip (30 Hz) | Hypothesis |
| Kernel/IOP frame counters | RAM 0x0007FFC8/D8/E8 (9,598 → 9,658) | VBlank | Confirmed (RAM diff) |

---

## 2. N1 mechanics on our runtime

### 2.1 Can the render half run again without the update half? (High confidence: yes, with care)

- The split is structural: manager A update, manager B render, manager B idle, flip. A render-only pass is
  "lock PGL, swap packet buffer, forEach(B, internal_render), wait DMA, unlock, forEach(B, idle), flip".
- **Render-side state mutations found so far** (each must be checked by the P0 purity test):
  1. race +0xE32C render counter and the DIMX dither toggle: advance per pass. Harmless for the simulation
     (Hypothesis); affects alternating effects (P0: list readers of +0xE32C).
  2. race +0xCF5A history byte: shifted per pass; gates the motion-blur/cross-fade feedback.
  3. `render_post_effect` blends the other display page (FIX alpha up to 0.8): at 120 Hz the visual decay per second
     doubles; needs `k^(60/Hz)` (cosmetic correction, an N2-type change on a render parameter).
  4. Car env map (`func_0044E660`), mirror, wet reflection, glare, smoke grab: render-to-texture into VRAM; repeated
     per pass (cost, not correctness). Extra passes can **reuse** the previous pass's env map and mirror (VRAM content
     persists) to save time (Hypothesis).
  5. Flare readback (64x8 buffer, `func_003B8848` → `func_004A5178`): one readback per pass; `updateFlare` in the next
     update reads the latest result. Extra passes add 0.7-2 ms readback waits each; skip or reuse on render-only
     passes (Hypothesis: safe because only the next update consumes it).
  6. `RacePS2Base::DMAsafe` (idle): `CourseData::DMAsafe` and per-car vt+0x28 (double-buffer swaps), plus the ghost
     car `checkToGhost` copy. Runs per pass; must stay paired with each render (High confidence).
  7. Random numbers: `CameraManager::initRandomSeed` exists (camera choice in replays); random use inside render is
     **Unknown** (P0 purity test covers it).
  8. Sound: no sound call found in the render path (High confidence from the code read).
- **The update depends on render only through** the flare readback result and the DMAsafe swaps (High confidence).
  So "update-only" frames (render skip, below 60) are plausible; P0 must prove determinism (section 5, P1 gate).

### 2.2 Snapshots for interpolation

| State | Source | Method |
|---|---|---|
| Camera | the game itself: race camera two pose sets (+0x28..+0x6C and +0x94..+0xD8) and fov/target; `t` in `render_world` | pass `t`; no host snapshot needed (Confirmed mechanism, semantics Unknown) |
| Replay/scene camera | keys[2] in the recorded camera | pass `t` (Confirmed) |
| Car body and wheels | car render object `+0x118(t)`; `RaceCarModel::virtual_34` passes `t` to the pose | pass `t` if the car pose interpolates (Unknown); otherwise host snapshots of the PGL matrices (H1 `pgluCallShape`) or of Automobile position/orientation per step |
| Other cars | same per entry (list race+0x6C → +0x60, sorted by distance) | same |
| Track objects | static; course VM animations step per frame in update | none (they move at 60 Hz) |
| Shadows | computed from car pose in render (Hypothesis) | follow the car |
| Skid marks | Unknown (no symbol, native-renderer STUDY H15) | P0 |
| Smoke, sparks, particles | updated in update at 60 Hz; drawn in render | leave at 60 Hz in P2; interpolate later by node identity |
| HUD | update at 60 Hz | redraw unchanged (numbers never blend) |

`CameraBase::getModelviewMatrix(self, target, t)` (0x44F280) and the per-pass `t` mean the game already defines
"state at render time t". The working hypothesis for P0 is that one pose set is the previous step and the other the
current, i.e. GT4 shipped with render interpolation hooks left at `t = 0` (perhaps for the 30 Hz flip-cycle mode or
the 0.05 slow motion).

### 2.3 Running extra render passes with one guest VBlank per simulation step

Constraints: the guest VBlank must stay 60 Hz for the IOP, SPU2, CDVD, pads, memory card, kernel timers and movies
(the BLACK lesson: keep platform time separate from presented frames). The flip waits for `now - last >= 1`
VBlank, so a second flip in the same VBlank period would block.

Design (Hypothesis, to prove in P2):

1. **Main-loop ELF patch** (code cave, re-translated): after the normal flip, ask the host for the frame plan
   (`extra = hostcall FRAME_PLAN`, see 4.2). For each extra pass: PGL lock, `func_001077F8`, `forEach(B, render)`,
   `func_00107840`, unlock, `forEach(B, idle)`, then a **flip without the VBlank wait** (a patched copy of
   `func_00107878` that calls `func_004A22C0(0)` — `frames == 0` returns `now` and does not touch `last`). Page
   toggles stay the game's own code, so FRAME/DISPFB alternation (HIGH-FPS 9.3) stays correct.
2. **`t` per pass**: a dispatch hook at the `jalr` to `render_world` inside `RacePS2Base::render` sets `$f12` from the
   host plan (0 on the normal pass unless the whole timeline is shifted, see 2.4).
3. **Guest clock freeze**: a runtime capsule stops virtual time during render-only passes (no events, no VBlank, no
   IOP slice; EE budget refilled without advancing `g_base`), so the update cadence, IOP cycles per VBlank and the
   SPU2 sample count stay identical. **Unknown:** whether DMA/VIF/GIF completion or MTVU waits rely on scheduled
   events; if they do, use "shift every pending event by the pass's cycles" instead. P2 measures IOP cycles and
   audio frames per VBlank with and without passes (must be equal).
4. **Present of the extra page**: the display shadow is applied at the VBlank (1.6), so the host must present the
   page the extra flip just selected:
   - CoreGS 1.1 (built in): call the present path for the new DISPFB (from the shadow written by `func_00105608`)
     right after the extra flip.
   - gt4-gs: GS stream protocol 1.3 record `PRESENT` (or the reserved AUX_FRAME_BEGIN/END 7-10) carrying the
     DISPFB/DISPLAY values; consumer presents without VSync semantics (no field change, progressive only). Fork
     change in `gt4recomp-gs` (done by a PCSX2-tainted agent; allowed for the GS fork, never for TASK-027).
5. **Interlace**: races are progressive (480p forced); menus stay 480i at 60 Hz (no extra passes in menus).
6. **MTVU**: extra passes are ordinary VIF1/VU1 jobs; journal ordering is unchanged. The privileged-write sync and
   readbacks happen per pass (cost, 3).
7. **GS stream back-pressure**: `frames_in_flight_limit` counts VSYNCs; a PRESENT record must count as a frame for
   back-pressure or the producer can run ahead (protocol detail for P2).

### 2.4 Timeline: interpolation vs extrapolation

- **Interpolation** (exact): every presented frame shows state at `p − 1 tick`; the main frame after step N shows
  N−1 interpolated toward N at α = 0; extra frames α ∈ (0,1). +16.7 ms latency on every frame, including the main one.
  If the game's `t` is "previous → current", interpolation is `t = α` with the timeline shifted by one tick.
- **Extrapolation** (zero added latency): the main frame shows N (`t = 0` path as today, byte-identical); an extra
  frame at N + 0.5 uses `t = 1.5` in a previous → current lerp (linear extrapolation, angles wrapped). Camera
  extrapolation over 8 ms is visually safe (High confidence); car extrapolation errs at collisions (small, one frame).
- Recommendation for P2: extrapolation as default (main frames stay byte-identical to today, which is a strong
  regression gate), interpolation as an A/B switch.

### 2.5 Below 60 FPS: render skip

- If the host is late, skip `forEach(B, render)` and `forEach(B, idle)` for that guest frame (and the extra passes),
  keep the update and the flip (which keeps the VBlank wait and the old page displayed). The update-only frame costs
  the update (translated code 1-2 ms, Hypothesis) plus IOP (0.2-0.5 ms) instead of 10-19 ms.
- Pacing (`native/pacing.cpp`): today `FramePacer::release` restarts the grid when behind ("missed frames are not
  replayed"). Render skip needs a bounded catch-up: when render-skipped guest frames are cheap, run them faster than
  the grid until the deadline debt is repaid (cap 4-6 frames of debt, then resync as today). Guest speed stays 100%
  while the presented rate drops to what the host can render (20-59 FPS).
- Audio (`GT4_PACING` paced): the produced SPU2 stream stays 800 frames per guest VBlank at 48 kHz; catch-up bursts
  produce audio faster for a moment; the 340 ms ring and the ±0.5% resampler absorb a debt of a few frames
  (High confidence; underrun/overflow counters in F8 are the check). Audio mode (`GT4_PACING=audio`) is not
  compatible with catch-up (Hypothesis).
- Alternative inside the game: `setAcceleratedTime(k)` + flip cycle k is GT4's own "simulate k steps per frame"
  path, but it keeps flare, smoke, particles and rigid bodies at one update per frame and changes the real-time
  countdown; render skip keeps every subsystem bit-identical. Prefer render skip.

---

## 3. Cost

### 3.1 Measured inputs (owner F8 logs, Confirmed values)

| Scene (owner sessions 2026-10-16/17, slice193-199) | EE thread CPU | translated EE (`ee_dispatch`) | VU1 worker busy | readback | critical path / 16.68 |
|---|---|---|---|---|---|
| Menus | 4.4-7.6 | 3.7-8.2 | 2.0-3.4 | 0 | 5.3-7.4 |
| Light race intervals (no readbacks; e.g. session 203401) | 6.5-7.0 | 1.4-1.7 | 5.0-5.3 | 0 | 9.9-10.6 |
| Night race 5 AI (slice198/199) | 8.1-8.8 | 2.3-2.5 | 3.4-4.1 | 0.7-1.7 | 11.8-14.2 |
| Heavy race intervals | 10.0-12.4 | 2.6-2.9 | 5.0-8.5 | 0.1-2.2 | 14.7-19.0 |

(EE thread CPU includes the translated game code, VIF/GIF parsing and the MTVU journal, GS stream writes,
kernel and IOP; `iop_run` is 0.13-0.5 ms.)

### 3.2 Render-only pass estimate (Hypothesis)

- Update share: translated update code + IOP ≈ 1-3 ms. Render pass ≈ the rest of the EE thread (VIF/GIF/stream work
  is all render) + the VU1 worker + readbacks: **7-15 ms critical path** in races; GPU raster in gt4-gs is small
  (GS thread about 1 ms).
- Budget per presented frame at rate R with one update per 16.67 ms: `update + k × pass ≤ 16.67`, k = R/60.

| Present rate | passes per guest frame | allowed pass cost | today (7-15 ms) |
|---|---|---|---|
| 60 | 1 | 14.7 | fits (as now) |
| 72-90 | 1.2-1.5 | 9.8-12.2 | light scenes only |
| 120 | 2 | 7.3 | no (needs ~2x cheaper pass) |
| 144 | 2.4 | 6.1 | no (needs ~2.5x) |

- CPU, not GPU, is the limit (RX 9060 XT at about 30 W in play, HIGH-FPS-STUDY 2). The Ryzen 7 5700X has free cores
  but the EE thread and the VU1 worker are serial per frame.
- **Work skippable on render-only passes** (to be measured in P0/P2): car env map (3 views + sphere mapper), back
  mirror, wet reflection, glare, smoke grab, flare readback and draw, `render_panel`/`render_other` (HUD can be the
  previous pass's GS output only if the GS side can compose it: not with the GS model; the HUD is cheap anyway).
  Expected saving 20-35% of a pass (Hypothesis).
- Other levers that cut every pass: VU1 AOT program cost, the EE-side stream copies (GS-LIVE 9.4), readback waits.

### 3.3 Below 60

Update-only frames cost about 2-4 ms (Hypothesis), so render skip can hold 100% game speed in the worst measured
interval (19 ms) with presents at about 40-50 FPS, and much lower presents if the GPU or GS side stalls.

---

## 4. Hook routes in our static recomp

### 4.1 Tools available

| Tool | What it can do | Limits |
|---|---|---|
| Dispatch hook (runtime capsule, like the license guard `scripts/license_guard_overlay.inc`) | inspect/modify registers and RAM at any `jalr` call (virtual, function pointer), keyed by return address | does not see direct `jal`; cannot safely call guest code re-entrantly (scheduler unwind discards host frames, SCENE-OBSERVATION) |
| Probes (`--probes`, whitelisted addresses) | observe exact instruction addresses, including direct-call frontiers | needs a separate generation profile and library (SCENE-OBSERVATION) |
| ELF patch + re-translation | change guest control flow (loops, calls), add code caves | producer slice, new `ps2game.lib`, new pins; decision 0055 needs superseding (as ULTRAWIDE P2) |
| VBlank RAM read/write (host) | read counters/state; write runtime-only fields | never save data; pointer 0x622F4C moves at boot (0x96E360 then 0xA0CC10, gotcha); race object found via manager lists, not fixed addresses |
| Runtime capsule (donor overlay) | kernel/syscall HLE, clock, events, MTVU, GS stream | donor is frozen; behaviour changes go through `prepare_perf_runtime.py` capsules |
| Native host (`native/`) | pacing, present, window, GS stream producer | none |

### 4.2 Hooks N1 needs

| Hook | Purpose | Tool | Phase |
|---|---|---|---|
| H0 observe update/render/idle/flip timing | P0 probe | dispatch hook on the functor `jalr`s in `func_00109580` (targets 0x100EE0, 0x100F50, 0x100FC0; return address inside 0x109580) + host clock; flip timing from the VBlank wait | P0 |
| H1 camera/car snapshot readout | P0 | dispatch hook at `RacePS2Base::render` entry (0x33D970 via vt+0x80) reads camera pose sets and car entries | P0 |
| H2 render purity diff | P0 | same hooks: copy RAM at render entry and at idle entry, diff outside packet buffers | P0 |
| H3 `t` injection | P2+ | dispatch hook at the `jalr` to `render_world` (vt+0x348) inside 0x33D970: set `$f12` | P2 |
| H4 render-only sub-loop and render skip | P1/P2 | ELF patch of `func_001016E8` (and a no-wait flip copy of `func_00107878`), code cave | P1 |
| H5 frame plan host call | P1/P2 | reserved syscall number in the patch, served by a kernel HLE capsule (returns: skip render / number of extra passes) | P1 |
| H6 guest clock freeze | P2 | runtime capsule (system.cpp events and budget) entered/left by H5 calls | P2 |
| H7 extra present | P2 | CoreGS present call; GS stream 1.3 `PRESENT` record + gt4-gs fork change | P2 |
| H8 present scheduler | P1/P2 | `native/pacing` (cap, VRR, unlocked; catch-up debt for render skip) | P1 |
| H9 cosmetic decay corrections | P3 | dispatch hook at `render_post_effect` (vt+0x350) adjusting the blend factor, or patch | P3 |

Never write save data; all writes are race-object runtime fields or host-owned code caves.

---

## 5. Phased plan

Each phase: own task branch from `main`, fresh `private/sliceNNN-*` dirs, new `docs/inputs/*.json` pins, single
writer, owner approval before merge. Switch: `GT4_NATIVE_FPS` (unset = today's behaviour, byte-identical).

### P0 — Frame-loop probe, no behaviour change (1 runtime slice, 3-5 days)

- Runtime capsule with H0-H2 behind `GT4_FPS_PROBE=1`; F8 section `[fps-probe]`: per guest frame update ms, render
  ms, idle ms, flip wait, render passes, `+0xE32C`, race clock F898, camera pose sets (+0x28.. and +0x94..), cached
  matrix +0x100, car entry positions; render-purity diff report (addresses written by render outside packet buffers,
  grouped by object).
- Acceptance: CTest fixture for the probe parser and the diff filter; Python test for the F8 section; headless
  4,200-frame menu schedule and the El Capitan race schedule with the probe off are byte-identical to slice211, with it
  on frames are identical too (observe only).
- Owner test: Time Trial Nordschleife Supra, F7/F8 one interval per scene (TT, night race, rally, showroom).
- Deliverables: the update/render split in ms per scene (replaces the 3.2 estimate), the `t` semantics of the camera
  (which set is previous, does `t = 1` equal the cached matrix), whether cars use `t`, the purity list.
- Kill: none (information). Feeds TASK-045 too (scene costs, page alternation in races).

### P1 — Render skip below 60 (1 producer slice + 1 runtime slice + native pacing, 1-2 weeks)

- H4 (patch: skip `forEach(B, render)` and idle when the plan says so), H5, H8 catch-up debt in `FramePacer`.
  `GT4_NATIVE_FPS=skip` first, later default once approved.
- Acceptance:
  - CTest: pacing fixture for catch-up (debt cap, resync, no drift over 10,000 frames); host-call fixture.
  - Headless determinism: race schedule with a forced skip pattern (every 2nd and every 3rd frame) vs baseline:
    race clock F898 and the DynamicsConductor/Automobile state identical at frames 9,000-16,000 (RAM diff of the
    DynamicsConductor object and car entries); lap time at finish identical.
  - Headless frame dumps on non-skipped frames identical to baseline where the scene is static (menus unaffected:
    skip disabled outside races).
  - F8: game speed 100% (VBlanks/s 60.0) with an artificial host load that drops presents to 30 and 20.
- Owner test: night race 5 AI and rally with an artificial load switch (`GT4_FPS_TEST_LOAD_MS`): lap times and
  audio unchanged, picture holds instead of slowing; then a normal session.
- Risks: update depends on render state in an unknown way (flare readback, DMAsafe, ghost copy); VU1 memory
  continuity across a skipped stream (PGL may assume microcode/constant uploads each frame).
- Kill: any simulation-state difference in the determinism test that cannot be traced to a render-only field.

### P2 — Camera-only render-only pass at 120 Hz in Time Trial (producer + runtime + native + gt4-gs, 3-4 weeks)

- H3, H4 extra passes, H6 clock freeze, H7 present (CoreGS first, then gt4-gs 1.3), skip env map/mirror/readback on
  extra passes when P0 shows it is safe. Cars and objects not interpolated (they repeat the main frame's pose).
  `GT4_NATIVE_FPS=camera120`.
- Acceptance:
  - Headless: frame distinctness (extra frames differ from both neighbours in camera motion; identical in a static
    camera); main frames byte-identical to the baseline run (extrapolation mode).
  - **Game speed: race clock F898 per guest frame identical to baseline over the whole race schedule; lap time at
    finish identical** (physics untouched).
  - IOP cycles and SPU2 frames per guest VBlank identical with and without extra passes (clock freeze check).
  - F8: extra pass cost (ms), presents/s, late frames.
- Owner test: Time Trial Nordschleife Supra 480p at 120 Hz (and 144 Hz monitor if VRR), compare smoothness of
  camera pans and the car (cars will judder slightly against the camera: expected in P2).
- Risks: `t` semantics not as hoped; clock freeze breaks a DMA wait; page/present mismatch in gt4-gs; cost.
- **Kill / pivot:** render-only pass critical path > 7.3 ms in Time Trial after the skips → 120 Hz not reachable;
  keep P1, ship 72-90 Hz only if the owner wants it, and make TASK-045 the 120/144 route. Camera discontinuity
  between extra and main frames that `t` cannot fix → host-side matrix override at `applyModelview` instead.

### P3 — Cars and the whole scene (2-4 weeks)

- Car pose via `t` (if P0 shows support) or host snapshots per step injected at the car pose call; other cars;
  shadows follow; particles/smoke by node identity or left at 60 Hz; post-effect decay `k^(60/Hz)` (H9); HUD
  unchanged.
- Acceptance: as P2 plus per-object continuity checks (car screen position monotonic in a straight); collisions
  frame-by-frame review in extrapolation mode.
- Kill: visible car/camera disagreement that cannot be removed without the native renderer's per-draw snapshots.

### P4 — Present pacing (1-2 weeks, can overlap P1)

- Cap (30, 45, 60, 90, 120, 144), VRR (cap a few FPS below refresh, the BLACK idea), unlocked; FIFO/mailbox/immediate
  per mode; frame-time histogram in F8; launcher setting.
- Acceptance: pacing fixture (presents vs guest grid), owner VRR test.

### P5 — Below-60 plus above-60 together (1 week)

- One scheduler: per guest frame choose skip, normal, or normal + k extra passes from a cost predictor (EWMA of P0
  timings).
- Acceptance: owner sessions with frame drops to 20 and peaks to 144: game speed 100%, lap times identical to 60 Hz
  runs on the same input script (headless).

---

## 6. N2 analysis (variable dt, BLACK style)

| System | Time base today | Could take variable dt? | Note |
|---|---|---|---|
| DynamicsConductor, Automobile, tyres, suspension, AI | 1/60 step | **No** | physics timing; lap clock is integer steps; replays/ghosts re-simulate inputs |
| RigidBodyManager (cones, debris) | 1/60 per frame | No (interacts with cars and license-test penalties: Hypothesis) | |
| Lap/race clock, countdowns, racePreUpdate timers | steps | No | lap times must match PS2 |
| Replays, ghosts | frame-indexed inputs | No | |
| Race camera | updated per step, already interpolable by `t` | not needed (N1 `t`) | |
| Post-effect feedback (blur/cross-fade) | per render pass | **Yes** (`k^(60/Hz)`) | needed even in N1 |
| Smoke, sparks, particles, flares | dt per frame | Yes, cosmetic | or interpolate in N1 |
| Course VM animation (flags, animated objects) | 1/60 per frame | Yes, cosmetic | |
| HUD tweens, menu actors (1/60 constants) | per frame | Yes, cosmetic | menus stay 60 Hz 480i; low value |
| Sound | steps / IOP | No | IOP and SPU2 stay on the 60 Hz guest VBlank |

**Verdict:** N2 for vehicle physics **breaks the out-of-scope rule** ("changes to the game's physics timing"). An owner
decision to allow it would have to accept: driving behaviour that differs between frame rates and from the PS2;
non-deterministic results; lap times not comparable with the PS2 or between rates; recorded replays and ghosts that
desync; license-test fairness changes; and a long reverse-engineering effort on the solver (the BLACK
`ScaleBodies`/dt² rescaling equivalent for GT4's tyre and suspension model), with no external reference for
correctness. Not recommended. N2 for cosmetics only is a small, safe complement to N1.

---

## 7. Comparison with TASK-045 and the native renderer

| | N1 (this study) | TASK-045 GS-stream interpolation | Native renderer P5 (TASK-065..073) |
|---|---|---|---|
| Frames | real game renders at `t` | lerped GS primitives | native draws from per-tick snapshots |
| Latency | 0 (extrapolation) or +1 tick | +1 tick (interpolation) | +1 tick |
| CPU cost per extra frame | full EE render + VU1 pass (7-15 ms today) | near zero on the guest; lerp on a worker | small (VU1 skipped for native draws) |
| GPU cost | one GS frame | one GS raster | native raster |
| 120/144 Hz on the owner PC | not today (needs ~2x cheaper passes) | plausible (oracle not yet run on a race capture) | yes, in 18-30 months |
| Below 60 at full speed | **yes (P1 render skip), soon** | no (it only adds frames) | yes |
| Quality risks | `t` semantics, render purity, clock freeze | primitive matching, disocclusion, HUD, page alternation | effects parity, draw identity |
| Touches physics | no | no | no |
| Effort to first owner test | P0+P1: 2-3 weeks | oracle 2-3 sessions + in-engine 6-10 sessions | months |
| Licence/clean room | our MIT code; gt4-gs PRESENT record in the GPL fork | MIT (tools), runs on any renderer | clean-room agents only |

Honest ordering: **P0 first** (it serves both N1 and TASK-045). Then **P1 render skip**: it delivers the owner's
"drops never slow the game" with real frames and small risk, which TASK-045 cannot. For **above 60**, TASK-045's race
oracle should run in parallel; it is more likely to reach 120/144 on this PC soon. N1 P2 (camera at 120 Hz) is worth
doing as a proof once P0 shows a render-only pass near 7 ms after skips; if not, N1 stays at "≤ 60 plus render skip"
until VU1/stream costs fall or the native renderer arrives.

---

## 8. Open unknowns

1. Camera `t` semantics: which pose set is previous/current; `t = 1` vs the cached matrix +0x100; behaviour for
   `t > 1`.
2. Whether car poses use `t` (`RaceCarModel::virtual_34` → `func_004596A8` and the VehicleModel getter).
3. The full render-purity list (P0 diff), including random numbers and readers of `+0xE32C`.
4. The update/render split in ms per scene (P0).
5. Whether the VBlank handler 0x4AB430 is the only writer of 0x70000050 and of the five display registers.
6. Whether DMA/VIF/GIF completion or MTVU waits depend on scheduled guest events (clock-freeze design).
7. VU1 memory continuity when a whole frame's stream is skipped (render skip).
8. gt4-gs behaviour with presents that are not VSYNCs (fork work).
9. Whether the media player really uses flip cycle 2 (movies at 30 Hz) and how it behaves with render skip
   (disable skip outside races).
10. Split screen (`render_world_split`) and photo mode paths.
11. Skid marks location (no symbol).

## 9. Owner decisions needed

1. Accept N1 as the native high-FPS track and **reject N2 for vehicle physics** (keeps the scope rule as is).
2. Order: P0 → P1 render skip (below 60) before TASK-045's in-engine work, with the TASK-045 race oracle in parallel?
3. Default timeline for extra frames: extrapolation (zero latency, small errors at collisions) or interpolation
   (+16.7 ms, exact).
4. Allow cosmetic dt corrections (post-effect decay, particles, course animations) — N2 on visuals only.
5. Allow a translated game patch (ELF patch of the main loop and a host-call syscall, re-translation, new
   `ps2game.lib`): supersedes decision 0055's "no translated game patch" for this feature.
6. GS stream protocol 1.3 `PRESENT` record and the matching gt4-gs fork change.
7. Menus stay 60 Hz (480i); high FPS only in races, replays and showroom.
8. Rates offered in the launcher: 30/45/60 (any time), 72/90 (when measured to fit), 120/144/unlocked only after P2
   passes its cost gate or via TASK-045.

## 10. Notes for reviewers

- No repository file was changed. Headless runs used only existing binaries and a copy of a seed card in
  `private/fps-study-01`; the owner's cards and `%LOCALAPPDATA%\GT4Recomp` were not touched; no window was opened.
- `PS2_PEEK` cannot read the scratchpad (0x7000xxxx reads RAM), so the VBlank counter at 0x70000050 was not
  observed; its role comes from the decomp of `func_004A22C0`/`func_004A21F0`.
- The BLACK-XBERecomp ideas used (FrameShare-style decay correction for cosmetics, a 60 Hz virtual VBlank for
  platform services, fractional step accumulators, present caps with a VRR margin) are general techniques taken
  from the owner's prose notes; no code or text was copied. GT4 already contains its own fractional step accumulator
  (`RaceBase::updatePlay`).
