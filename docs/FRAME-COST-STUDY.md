# Frame cost study: where GT4Recomp spends each frame, and how to cut it

2026-10-21. Research and measurement only (no code, pin or slice change). This is the study for TASK-084, planned after
1.0.0 (owner, 2026-10-21). Agent status: **not
PCSX2-tainted** (no PCSX2 source, no `gt4recomp-gs` fork, no `private/tooling/pcsx2-*`, no `slice189-pcsx2-ref`
was opened). Labels: **Confirmed** (measured or read in code), **High confidence**, **Hypothesis**, **Unknown**.

**Implementation status (2026-10-22):** section 6 and items 2-4 are implemented on `task/084-frame-cost-122`
(build `slice262`); item 1 is out by owner decision. Build, switches, before/after numbers and the owner F7/F8
checklist: `docs/BUILD.md`, "Slice 260/262".

Build under test: slice211 `private/slice211-build/build/gt4vulkan.exe` (97,186,816 bytes, sha256 `2fecda70...`,
pin `docs/inputs/slice211-pcsx2.json`), runtime capsule `private/slice191-gs-stream-runtime`, port
`private/slice200-port`, gt4-gs 1.0.1 (`private/slice203-gt4gs-release-pkg/gt4-gs.exe`, sha256 `bda98d49...`,
byte-identical to `%LOCALAPPDATA%\GT4Recomp\gs\1.0.1\gt4-gs.exe`; the pinned copy was used so nothing in the owner's
folder was touched). Raw runs, scripts and logs: `private/perf-study-01/` (`scripts/run.sh`, `batch.sh`,
`f8.py`, `summary.py`, `runs/<name>/{conditions.txt,stderr.log,stdout.log}`).

## 0. Summary

1. **The EE thread is not slow because of game code.** In a headless day race (1x, idle machine) translated game
   code plus everything nested in it (`ee_dispatch`) is 2.9 ms of an 8.2 ms frame. The rest is emulation plumbing:
   VIF1 packet posting 1.2, MTVU journal/GS front end 0.5, GS stream writes 0.8, IOP 0.5, run loop 0.8, and
   **1.4 ms of waits for the VU1 worker** (Confirmed).
2. **The flare readback is the night race's real cost, and it is not the GS round trip.** Clubman Stage Route 5 at
   night issues exactly one 2,048-byte readback per frame (64x8 RGBA, the lens/headlight flare visibility buffer,
   `func_003B8848 -> func_00497EF8 -> func_004A5178`). Each one costs **3.3 ms of EE wait for the VU1 worker**
   (the MTVU `download` sync drains every queued VU1 job) **plus 0.9 ms** of GS round trip at 1x (2.2-2.8 ms at
   3x). It serializes the EE and the VU1 worker, so MTVU is **slower than no MTVU** in that scene (9.0 vs 9.9 ms
   per frame headless). `GT4_GS_READBACK=unsynchronized` removes only the 0.9 ms part (Confirmed).
3. **MTVU per-job overhead is large.** The VIF1 DMA chain runs once per frame but is posted to the worker as about
   5,200 jobs per frame (one per DMA tag plus one per transferred tag). In 3D menus MTVU makes the frame 1.3 ms
   *slower* (7.0 vs 5.6 ms) because there is almost no VU1 work to overlap. The shared doorbell wakes the waiting
   thread on every push and pop; about 0.7-1.0 ms of the "wait" time is CPU, so **the F8 summary line
   "EE critical path = EE CPU + waits" double-counts** by about 1 ms (Confirmed by occupancy; mechanism High
   confidence).
4. **The paced owner F8 `ee_dispatch` is not "game code" either:** the pacing sleep (`release_frame`) runs inside
   whatever scope is open when the VBlank event fires, which is `ee_dispatch` in menus and loading and
   `unattributed` in races (Confirmed in code). Owner menus showing `ee_dispatch` 9-11 ms are mostly sleep.
5. **Ranked plan (section 4):** (1) deferred, journal-ordered flare readback, about -4 ms in night races;
   (2) VIF1 job coalescing per DMA chain plus targeted wake-ups, -1.0 to -2.0 ms everywhere 3D; (3) VBlank as a
   journal entry instead of a blocking drain (pcsx2 mode), -1 to -3.5 ms on frames where the worker tail is
   late; (4) drop the redundant GIF walk and the extra copy of worker output, -0.4 to -0.8 ms. Together they move a
   heavy day race from about 8.2 to about 5.5-6 ms (EE and VU1 worker balanced) and the night race from about
   9.9 to about 5.5-6.5 ms headless. **For TASK-082**, a render-only pass then costs about max(EE render part,
   VU1 worker) = 4-6 ms in these scenes, inside the 7.3 ms budget for 120 Hz; heavy scenes (owner VU1 worker
   7-8.5 ms) still need VU1 worker or skip-on-render-pass work.

## 1. Method, conditions, limits

### 1.1 Runs

- Headless only (no `--window`, gt4-gs `Surfaceless = true`), unthrottled, one run at a time
  (`scripts/batch.sh`). The executable was started directly (not through `external_runtime_probe.py`) because the
  probe always adds `--trace-syscalls`, which writes about 1,000 lines per frame and inflates `ee_dispatch` (the
  slice211 title-screen runs show `ee_dispatch` 6.1 ms with tracing vs 0.5 ms here). Environment as the probe sets
  it: `GT4_PERF=1 PS2_SCALE=1 PS2_GS=gpu GT4_FORCE_WIDESCREEN=1 GT4_MTVU_REPORT_EVERY=500`; pcsx2 runs add
  `GT4_RENDERER=pcsx2 GT4_GS_PROCESS=<pinned gt4-gs.exe> GT4_GS_CONFIG=<run>/gt4-gs.ini` (the probe's own ini text,
  generated with `gs_config_text`, plus `Surfaceless`, `DumpEvery = 1000`). Frame dumps every 1,000 frames
  (scene check only). Card: copies of the pinned seeds in each run folder (`slice113-video/widescreen-seed` for the
  title schedule, `slice110-menu-pilot/name-control4` for races). Disc: `Gran Turismo 4 (USA) (v2.00).iso`.
- Schedules:
  - **title**: the slice157/208 12-press schedule (4,200 frames); it never leaves the title/intro (2D).
  - **day race**: the TASK-041 El Capitan schedule (Arcade, Single Race, Original Circuits, El Capitan, Ford Ka,
    5 AI, cross held from 8,800), 16,000 frames. Report index i covers VBlanks 500i..500(i+1): 3D mode menu
    i=2-5, car showroom i=12-13, race i=19-31.
  - **night race** (new): City Courses, **Clubman Stage Route 5** (night), Ford Ka, 5 AI:
    `600:start:30 900:cross:30 1500:cross:30 2300:down:10 2450:cross:10 3500:right:10 3700:right:10
    3900:cross:10 4100:cross:10 6500:down:8 6650:cross:10 7400:cross:10 7600:cross:10 7800:cross:10
    8700:cross:10 9200:cross:7000` (frame dumps confirm the night track and the race). Flare readback stretch
    i=18-25 (1.0 readback per frame); a stretch without readbacks i=26-29.
- Renderers: PCSX2 GS 1x (two runs, a/b), PCSX2 GS 3x, `unsynchronized` readback, CoreGS 1.1 built in
  (`donor`, two runs), and `GT4_MTVU=0` (VU1 on the EE thread) as a control.
- **Measurement hygiene (Confirmed):** before every run `batch.sh` waited while any `ninja`, `clang-cl`, `cl`,
  `link`, `lld-link`, `cargo`, `rustc`, `msbuild` or `makensis` ran; each `conditions.txt` records the tool check
  and CPU load before and after. Builds by the other agents ran between batches (02:15-02:19, 02:27); every number
  below comes from a run with no build tool before or after it, except `night-pcsx2-3x` (makensis started during
  it), which is marked noisy and was repeated (section 2.2). CPU load before runs: 5-19%.

### 1.2 What the counters mean (read in code, Confirmed)

- `frame` here is **wall ms per VBlank** from the F8 header (`elapsed / VBlank samples`). Headless runs are not
  paced, so this is the critical path of the frame on this PC.
- `[perf] exclusive wall` categories are wall time of scopes on the EE thread: `ee_dispatch` wraps only
  `dispatch()` in `system.cpp run()` and therefore also contains everything the translated code calls without its
  own scope: syscalls and thread switches (`kernel.cpp`), `preempt()` -> `run_due_events()` (events, DMA,
  `mtvu::pump`, `sif::pump`), PATH3 GIF transfers and their GIF walk. `unattributed` is the run loop outside
  `dispatch`: `run_due_events`, `kernel::service` (interrupt handlers run through `run_guest`), `idle()`.
- **Pacing sleep is not a category.** `window.cpp on_vblank` calls `release_frame()` (waitable timer plus 0.5 ms
  spin) *before* its `host_vblank` scope, so the sleep lands in `ee_dispatch` when the VBlank event fires inside
  `preempt()` (busy guest: menus, loading) and in `unattributed` when it fires from `idle()` (races). Owner F8
  `ee_dispatch` 9-11 ms in menus is mostly sleep; in races `unattributed` 3-10 ms is mostly sleep.
- `measured_thread_cpu` uses `GetThreadTimes`, which is tick-sampled: single intervals can be wrong (one showroom
  interval reports 15.6 ms CPU for a 6.2 ms frame, occupancy 129%). Use it only as an average.
- MTVU waits (`[mtvu] EE wait`) use `std::atomic::wait` on one shared doorbell (`mtvu_ring.hpp Doorbell`), rung with
  `notify_all` on **every** ring push and pop by both threads. The wait loop also runs `pump_output()` (pops worker
  records into new `std::vector`s). So part of every wait is CPU work and wake-ups.
- `[gs-ipc] summary: EE critical path = EE CPU + VU1 waits + readbacks + back-pressure` therefore double-counts
  the CPU part of the waits. Headless day race: summary 9.35 ms vs measured wall 8.20 ms; night 11.2 vs 9.9.

## 2. Per-stage cost (headless, ms per frame, idle machine)

### 2.1 EE thread (wall, exclusive scopes) and the other threads

pcsx2 = PCSX2 GS 1x, mean of runs a and b; donor = CoreGS 1.1 built in, mean of a and b; MTVU off = `GT4_MTVU=0`
(VU1 then runs on the EE thread). Worker = VU1 worker thread busy (VU1 programs + PATH1 output). GS thr = gt4-gs GS
thread (its own log, same VBlanks). Jobs = MTVU jobs posted per frame.

| Scene, renderer | frame | EE CPU | ee_dispatch | IOP | VIF/GIF post | journal GS front end | stream write | run loop (unattr.) | VU1 on EE | wait vblank | wait download | readback wait | worker busy | GS thr | stream MB | jobs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Title/intro 2D, pcsx2 | 1.38 | 1.29 | 0.72 | 0.14 | 0.18 | 0.03 | 0.08 | 0.07 | - | 0 | 0 | 0 | 0.03 | 0.39 | 0.64 | 631 |
| Title/intro 2D, donor | 1.50 | 1.35 | 0.66 | 0.14 | 0.18 | 0.07 (+0.11 copy/exec) | - | 0.12 | - | 0 | 0 | 0 | 0.03 | - | - | 631 |
| 3D mode menu, pcsx2 | 6.95 | 6.90 | 4.63 | 0.29 | 1.39 | 0.18 | 0.27 | 0.09 | - | 0 | 0 | 0 | 0.24 | 0.85 | 1.65 | 4,307 |
| 3D mode menu, MTVU off | **5.61** | 5.59 | 4.58 | 0.28 | 0.32 | - | 0.25 | 0.06 | 0.00 | - | - | - | - | 0.84 | 1.65 | 0 |
| 3D mode menu, donor | 8.19 | 8.14 | 5.32 | 0.28 | 1.38 | 0.76 (+0.22 copy/exec) | - | 0.13 | - | 0 | 0 | 0 | 0.22 | - | - | 4,307 |
| Car showroom, pcsx2 | 7.61 | ~7.4 | 1.73 | 0.27 | 1.02 | 0.36 | 0.43 | 0.40 | - | 3.10 | 0 | 0 | 5.09 | 4.40 | 3.71 | 4,911 |
| Car showroom, MTVU off | 7.50 | 7.48 | 1.40 | 0.26 | 0.76 | - | 0.45 | 0.23 | 4.77 | - | - | - | - | 4.10 | 3.71 | 0 |
| Car showroom, donor | 7.46 | 7.37 | 1.60 | 0.24 | 0.97 | 3.53 (+0.41 copy/exec) | - | 0.40 | - | 0.08 | 0 | 0 | 4.39 | - | - | 4,837 |
| Day race El Capitan, pcsx2 | **8.20** | 7.76 | 2.88 | 0.47 | 1.22 | 0.53 | 0.80 | 0.77 | - | 1.08 | 0.31 | 0.07 | 5.52 | 3.50 | 6.62 | 5,256 |
| Day race, pcsx2 3x | 10.30 | 9.51 | 3.66 | 0.60 | 1.48 | 0.66 | 0.88 | 0.92 | - | 1.64 | 0.25 | 0.06 | 7.13 | 4.79 | 6.62 | 5,300 |
| Day race, MTVU off | 9.59 | 9.55 | 2.32 | 0.44 | 1.01 | - | 0.74 | 0.29 | 4.84 | - | - | 0.03 | - | 3.32 | 6.60 | 0 |
| Day race, donor | 9.44 | 9.23 | 2.70 | 0.45 | 1.20 | 3.60 (+0.41 copy/exec) | - | 0.79 | - | 0.05 | 0 | 0 | 5.25 | - | - | 5,251 |
| Night race CSR5, flare readback 1/frame, pcsx2 | **9.89** | 8.78 | 2.54 | 0.44 | 1.12 | 0.36 | 0.51 | 0.58 | - | 0 | **3.34** | **0.90** | 3.65 | 3.62 | 3.85 | 5,321 |
| Night, pcsx2 `unsynchronized` | 9.49 | 8.55 | 2.78 | 0.47 | 1.29 | 0.34 | 0.61 | 0.62 | - | 0 | 3.17 | 0.07 | 3.70 | 3.79 | 3.77 | 5,141 |
| Night, MTVU off | **9.00** | 7.41 | 2.57 | 0.45 | 0.77 | - | 0.52 | 0.30 | 2.96 | - | - | 1.42 | - | 3.67 | 3.84 | 0 |
| Night, donor | 13.70 | 9.67 | 2.37 | 0.44 | 1.11 | 2.57 (+0.65 copy/exec) | - | 0.62 | - | 0.01 | 0 | GPU fence 3.87, privileged 2.05 | 3.43 | - | - | 5,145 |
| Night stretch without readbacks, pcsx2 | 5.03 | 4.90 | 2.49 | 0.45 | 0.71 | 0.26 | 0.42 | 0.50 | - | 0.08 | 0 | 0 | 2.33 | 2.79 | 3.26 | 3,342 |

Run-to-run spread (a vs b): day race frame 8.31/8.10, night 10.04/9.74, donor race 9.41/9.47 (Confirmed, about
+-2-3%). VU0 micro (`vu0_aot`) is 0.09-0.31 ms in every scene. Stream records: 2,770 per frame in the day race,
2,350 in the night race; GS process wake-ups 12 and 4 per frame.

### 2.2 3x and readback round trip

Night race flare stretch (i=18-25), PCSX2 GS at the owner's 3x (headless, surfaceless):

| Run | frame | wait download | readback wait | per readback | worker busy | GS thr | note |
|---|---|---|---|---|---|---|---|
| 1x (a/b mean) | 9.89 | 3.34 | 0.90 | 0.86 ms | 3.65 | 3.62 | clean |
| 3x, run b | **12.12** | 3.75 | 1.58 | 2.01 ms (max 6.4) | 4.44 | 4.71 | clean (load 4% before) |
| 3x, run a | 14.80 | 4.73 | 2.80 | 2.21 ms (max 76.7) | 4.20 | 4.67 | **noisy**: makensis ran during the run; first 3x pass on this track filled the pipeline cache |
| 3x `unsynchronized` | 11.76 | 4.06 | 0.08 | 0.09 ms | 4.81 | 5.66 | CPU load 41% before start (other user work): indicative |
| 3x MTVU off | 11.99 | - | 1.61 | 1.15 ms | - (VU1 on EE) | 4.94 | CPU load 24% before start: indicative |

At 3x the GS round trip doubles (the GS thread has more to finish before it can answer) and the EE-side categories
grow by about 15-25% (ee_dispatch 2.54 -> 3.04, VIF post 1.12 -> 1.38): the GS thread and driver threads compete with
the game threads for cores and clock (Hypothesis). The day race shows the same (8.2 -> 10.3 ms, worker 5.5 -> 7.1).
The download (worker drain) part stays the largest at 3x too: 3.75 of the 5.3 ms per readback.

### 2.3 Critical-path picture (threads and order inside one frame)

```
EE thread (game)   |update ~1.5|render: build lists, kick VIF1 chain (5,000 jobs) |..flare FINISH+readback..|idle|VBlank: drain|pace
                                     \ jobs                                         ^ waits for ALL queued VU1
VU1 worker                            [VU1 programs + PATH1 output .................]                ^ (vblank wait if late)
                                                     \ GIF records via the EE journal
EE journal drain -> GS stream ring  ->  gt4-gs CPU thread (0.5 ms) -> GS thread (3.5 ms at 1x, 4.8 at 3x) -> GPU
                                         READ_FIFO: waits until the GS thread executed every earlier record + GPU sync
```

- Without readbacks the frame is `EE busy (6.8 ms) + the worker's tail after the EE finished (1.1-1.4 ms)` in the
  day race; the GS process runs up to 2 frames behind and costs the EE nothing (back-pressure 0).
- With the flare readback (night) the EE stops mid-frame, waits for the whole frame's VU1 work (3.3 ms), then for
  the GS process to catch up and sync its GPU (0.9 ms at 1x), so EE and worker run **in series**: 9.9 ms. MTVU off
  is faster (9.0 ms) because it avoids the per-job overhead and gains nothing from a worker that cannot overlap.
- Paced owner runs add the pacing sleep after the VBlank; the owner's heavy race intervals (EE CPU 10-12.4 ms,
  F8 critical path 14.7-19.0) are overstated by the double-counted wait CPU (section 1.2); the true busy part is
  about 1-2 ms lower (High confidence).
- CoreGS (donor) runs its GS front end on the EE (3.5 ms in races, `gs_drain`) and blocks on its own GPU for each
  readback (3.9 ms) and for privileged writes (2.1 ms): it is the slower renderer on the EE side in every 3D scene.

## 3. EE overhead breakdown (day race, pcsx2 1x, 8.20 ms)

| Item | ms/frame | Evidence | What it is |
|---|---|---|---|
| Translated game code + everything nested without a scope | 2.88 | Confirmed | Game update and render list building; also syscalls (about 950 per frame on the title screen: GetThreadId 620, ChangeThreadPriority 160, WaitSema 63, SignalSema 54; race count Unknown), thread switches (`kernel.cpp switch_to`, `std::map` scheduler), `preempt()` event pumps, PATH3 GIF scan and walk. MTVU off: 2.32, so about 0.55 ms of it is MTVU pumping (High confidence). |
| VIF1 packet posting (`vif_gif`) | 1.22 | Confirmed | EE shadow VIF1 walk (dry unpacks, needed for stalls) + copy of every packet into the MTVU input ring + one journal placeholder + doorbell ring per job (5,256 jobs/frame from **one** DMA chain per frame: `[dma] starts ch1` = 16,581 per 16,000 frames). MTVU off: 1.01; 3D menu: 1.39 vs 0.32, i.e. **about 0.25 us per job**. |
| MTVU journal GS front end (`gs_drain`) | 0.53 | Confirmed | Worker GIF output popped into a fresh `std::vector` per record (copy 2 of 3) and passed to `gif::render`, which copies it into the GS ring and then **walks every GIF qword again** (`StreamSink`) looking for SIGNAL writes that MTVU already pre-applied. |
| GS stream writes (`gs_ipc_write`) | 0.80 | Confirmed | 6.6 MB/frame memcpy into the shared ring (about 8 GB/s effective), 2,770 records, 12 SetEvent wake-ups per frame (TASK-041 batching works). |
| IOP (`iop_run`) | 0.47 | Confirmed | R3000A interpreter for the 23 game IRX modules, run in many small slices from `run_due_events` (each `preempt` of at most 20,000 EE cycles syncs the IOP). |
| Run loop (`unattributed`) | 0.77 | Confirmed | `run_due_events` (vector scan of events, `std::function` calls), `kernel::service` (interrupt handlers through `run_guest`), `idle()` steps of 0.5 ms guest time. MTVU off: 0.29, so about 0.5 ms is MTVU pumping in the idle loop (High confidence). |
| VU0 micro AOT | 0.09 | Confirmed | small |
| Waits for the VU1 worker | 1.08 vblank + 0.31 download | Confirmed | The VBlank drain (`mtvu::vblank_sync` -> `sync("vblank")`, blocking) waits for the worker's tail; download = the occasional sun-flare readback. About 0.7-1.0 ms of these waits is CPU (wake-ups on every doorbell ring, `pump_output` inside the wait): occupancy 0.94 while CPU + waits exceed the wall time by 1.15 ms. |
| GS round trip (readback wait) | 0.07 | Confirmed | Rare in the day race (0.04 readbacks/frame, sun flare); 0.8-1.5 ms each. |
| Back-pressure (GS ring or frames in flight) | 0.00 | Confirmed | gt4-gs keeps up headless. |
| Kernel/syscalls, separately | Unknown | - | Inside `ee_dispatch`; no counter without the per-call trace. Estimate 0.2-0.6 ms (Hypothesis: 1,000 calls at 0.2-0.6 us). |

So of 6.8 ms of EE busy time, about 2.9 ms is the game (plus kernel and nested work) and **about 3.9 ms is
emulation plumbing**, of which **about 1.9 ms exists only because of MTVU** (MTVU off: EE busy 4.9 ms without VU1).
MTVU still wins in the day race (8.2 vs 9.6 ms) because it moves 4.8 ms of VU1 work off the EE, but it gives back
40% of that gain in overhead, and loses outright where it cannot overlap (night readback, 3D menus).

Missing counters to separate the remaining items: section 5.

## 4. Readbacks

### 4.1 Which effects, how many (Confirmed unless noted)

| Effect | Code path (gt4-decomp names) | Size | Per frame | Scenes |
|---|---|---|---|---|
| Lens flare / headlight flare visibility | `RacePS2Base::render_world_split` -> `func_003B8848` (flush, then only when flare sources exist: fields +0x8, +0xA34, +0x1848) -> `func_00497EF8` -> `func_004A5178(ctx, 0, 0x40, 0, 0, 0x40, ...)` | 2,048 bytes (64x8 RGBA) | **1.00** at night (CSR5 race), 0.0-0.53 by day (sun in view: El Capitan 121-125 per 6,500 frames) | every race with light sources; also `func_0033EAB8` (split screen / second view) |
| Photo mode capture | `RacePhotoMode func_003C7AE0` -> `func_004A5178(.., 0x30, .., 8, 4, ..)` | Unknown | 0 in play | photo mode |
| Race grab (8x4 blocks, PSM 0x30) | `func_003308E8` | Unknown | 0 in these runs | Unknown (smoke/screen grab, Hypothesis) |
| Screen grab of a window | `func_001059F8` (startup, `GSBuffer`) | Unknown | 0 in these runs | menus/transitions (Hypothesis) |

Each readback is a GS->host transfer: BITBLTBUF/TRXPOS/TRXREG, TRXDIR=1, then the EE reads the FIFO through DMA
channel 1 reverse (`dmac.cpp:128 sync("download")` -> `gs::read_image` -> `gs_stream::read_fifo`). Per readback the
game also writes CSR (FINISH clear) and BUSDIR twice (8 privileged writes per frame at night, 5 by day).

### 4.2 Why it blocks, and what each part costs (night race, 1x)

1. **MTVU download sync: 3.34 ms/frame** (Confirmed). `sync("download")` drains the whole journal: every VU1 job
   posted so far must finish and its GIF output must be in the stream, because READ_FIFO must follow that output.
   The game requests the flare readback after it kicked the frame's main VU1 work, so the EE waits for about all
   of the worker's frame (worker busy 3.65 ms). This is the dominant cost; `GT4_MTVU_PRIV_ASYNC` (TASK-041) only moved
   the same wait from the CSR write to here.
2. **GS round trip: 0.90 ms/frame at 1x** (Confirmed; 2.2-2.8 ms at 3x, section 2.2). The GS process must execute
   every earlier record of the frame on its GS thread, then PCSX2's accurate download syncs the GPU. gt4-gs reports
   the same service time on its side (mean 1.0-1.5 ms). The first readbacks on a track with a cold pipeline cache
   take 8-150 ms (TASK-046).
3. Privileged writes: 0 wait (queued in the journal, TASK-041).

`unsynchronized` (answer from gt4-gs local memory, no GPU sync) only removes part 2 (night 9.49 vs 9.89 ms) and
loses the headlight flares (owner, slice195). MTVU off removes part 1's overhead but not the serialization
(9.00 ms).

### 4.3 Options

| Option | What changes | Saves (night, 1x headless) | Fidelity | Verdict |
|---|---|---|---|---|
| **A. Deferred, journal-ordered flare readback** | For the flare call site only (allow-list by the EE return address in `func_00497EF8`/`func_004A5178`, or by the exact 64x8 TRX rectangle): post READ_FIFO **in stream order through the MTVU journal** (like `append_privileged`) without draining, return the **previous** request's 2,048 bytes immediately, and collect this request's reply at the next request (or at the next VBlank). Producer-only change: the protocol already has `readback_request/done` ids; the response area holds one reply, which is enough if the producer collects before posting the next READ_FIFO. | **-4.2 ms** (download 3.34 + round trip 0.90); frame about 5.5-6.5 ms (High confidence: the no-readback stretch of the same track runs at 5.0 ms). | The flare intensity uses data one frame older (the game already uses it in the next update, so the visible lag goes from 1 to 2 frames, 33 ms). Deterministic: request N always gets reply N-1, independent of host timing. No gameplay effect if the buffer is read only by `FlareClass::updateFlare` (High confidence from the TASK-082 study; to be proven with a reader scan of the buffer, P0). | **Do first.** |
| B. CPU emulation of the occlusion query | Compute flare visibility on the CPU | - | needs the depth buffer the GPU drew; not exact | Rejected |
| C. Cache | Reuse the last answer when the same rectangle is read twice in a frame | 0 (one read per frame) | - | Not useful for the flare; the donor already caches downloads in its async mode |
| D. Answer without a GPU sync when only a visibility count is needed | gt4-gs computes the 64x8 read on its GS thread without the GPU (`unsynchronized`-like) | 0.9 ms only | flares missing today (stale data) | Superseded by A |
| E. Skip on render-only passes (TASK-082) | Extra passes reuse the last flare result; no READ_FIFO | 4.2 ms per extra pass | none extra (the update reads it once per update) | Required for TASK-082 P2 on top of A |
| F. Prewarm pipelines (TASK-046) | - | removes the 8-150 ms first readbacks | none | Keep |

## 5. Ranked optimisation plan

Gains are headless 1x on this PC (Ryzen 7 5700X); "frame" is the critical path. Checks named in every item:
**stream identity** = `GT4_GS_STREAM_HASH=1` session hash identical on the day race and the night race schedules;
**frames** = slice157 title 42/42, day-race and night-race frame dumps every 100 frames identical; CTest with new
fixtures; owner F8 in the night race and a heavy day race. One variable per slice; every change keeps an A/B switch.

| # | Change | Expected saving | Effort | Risk | Fidelity | Checks | TASK-082 / weak PCs |
|---|---|---|---|---|---|---|---|
| 1 | **Deferred flare readback (4.3 A)**: journal-ordered READ_FIFO, previous result returned, allow-list of one call site, switch `GT4_GS_READBACK_DEFER=0/1` | night -4.2 ms at 1x (9.9 -> about 5.7), -5.3 ms at 3x (12.1 -> about 6.8), day race -0.3 ms average, removes 8-150 ms cold-cache stalls from the EE path | 1-2 slices (journal entry + client collect + fixtures); game side only, no gt4-gs change; CoreGS needs the same in `gs.cpp read_image` (its async download cache) | medium (ordering, death of the GS process mid-request, first request has no previous reply: return zeros = "not visible", as the unsynchronized path does) | flare 1 frame later; no gameplay (prove the reader set first) | stream identity **except** READ_FIFO timing (records identical, reply bytes shifted by one); frames differ only in flare intensity (night); a reader scan of the 2 KiB buffer; owner night race | the biggest single gain for weak PCs (removes the EE/worker serialization); for 120 Hz render-only passes skip it (option E) |
| 2 | **Coalesce VIF1 jobs per DMA chain** (post one job at chain end, pause, or before any other journal/sync operation) **and targeted wake-ups** (separate doorbells per direction; notify only when the peer sleeps; the EE waits for "job N done" instead of every ring) | -1.0 to -2.0 ms EE in every 3D scene (VIF post 1.2 -> about 0.4, wait CPU -0.7, pumping -0.3); worker -0.3 to -0.6 ms (one receive per chain instead of 5,000) | 1 slice | low: the worker runs the same bytes in the same order | none (byte-identical stream) | stream identity, frames, `GT4_MTVU_VERIFY=1` 0 mismatches, journal random-script fixtures with coalesced jobs, `--cpu-cores 2` check | makes MTVU a net win in menus and on 4-core PCs; every render pass gets cheaper |
| 3 | **VBlank as a journal entry (pcsx2 mode)**: `append_vblank_flush` (exists, unused) posts VSYNC after the frame's GIF output; the EE does not drain at VBlank. CoreGS keeps the blocking drain (its present needs the output). | -1.1 ms day race, -3.1 ms showroom (headless); in paced play it only matters when EE + worker tail exceed the budget (owner heavy races); the frame becomes max(EE, worker) instead of EE + tail | 1 slice | medium: frame dumps and headless reports read worker totals at VBlank (move them to the drain), the probe's worker totals become one frame late; GS frames in flight already bounded at 2 | none (stream byte-identical; guest never reads worker timing: signals are pre-applied, CSR/IMR answered from the EE copy) | stream identity, frames, MTVU verify, owner F8 heavy race | needed for 120 Hz: lets render pass N's VU1 work overlap pass N+1's EE work |
| 4 | **Remove the redundant GIF walk and one copy** (pcsx2 mode with MTVU): skip `StreamSink` walk when all SIGNAL/FINISH/LABEL of the packet were pre-applied (PATH1 has none by invariant; PATH2/3 scanned at accept); forward worker output from the MTVU output ring straight into the GS ring (no `std::vector` per record) | -0.4 to -0.8 ms EE (most of `gs_drain` 0.53 plus part of PATH3 work in `ee_dispatch`) | 1 slice | low | none | stream identity, GIF tag statistics moved to a debug switch | small but on every pass |
| 5 | **EE build flags**: `GT4_X86_64_V3` (with `fp-contract off`), then ThinLTO, then PGO from a headless day+night race profile (TASK-010 left them unmeasured) | Hypothesis: 5-15% of `ee_dispatch` + runtime = -0.3 to -0.8 ms | 1-2 slices (long builds) | low with fp-contract off; v3 needs the AVX2 check that TASK-081 adds | none (no rounding change) | frames, stream identity, owner F8 | helps every pass; v3 raises the minimum CPU (AVX2) |
| 6 | **Kernel and run-loop fast paths** after counters (section 6): flat thread table instead of `std::map`, no reschedule when priority order does not change, event queue as a small sorted array, fewer IOP slices per frame (sync the IOP at most every N EE cycles unless an IOP-visible event is due) | Hypothesis: -0.3 to -0.8 ms | 1-2 slices | medium (IOP timing affects audio/SIF; must stay deterministic) | none | frames, audio capture identity, card saves | small |
| 7 | **gt4-gs GS thread at 3x** (fork work, tainted agents): GS thread 4.8 ms at 3x vs 3.5 at 1x, and the whole EE side slows by about 25% at 3x (shared cores/clock; Hypothesis) | Unknown | fork | - | none | owner F8 | weak PCs with few cores |
| 8 | **VU1 worker program speed** (VU1 AOT output quality: flag/state elimination per program, PATH1 output batching) | after 1-4 the worker (3.7-5.5 ms headless, owner 5-8.5 ms) is the critical path | separate study | - | none (differential tests) | VU1 corpus differential, frames | **the gate for 120 Hz in heavy scenes** |
| 9 | Move the stream producer to its own thread (journal drain + GS ring writes off the EE) | -0.5 to -1.0 ms EE after 2 and 4 | 2 slices | medium (readback and privileged ordering) | none | as 2 | only if the EE stays critical after 1-5 |
| 10 | IOP AOT (23 IRX) | -0.2 to -0.3 ms | large | medium | none | audio identity | low priority |

**Combined estimate (Hypothesis, headless 1x):** day race 8.2 -> about 5.5-6 ms (EE about 4.5-5, worker about 5);
night race 9.9 -> about 5.5-6.5 ms; 3D menus 7.0 -> about 5 ms. Paced at 60 Hz this is 6-11 ms of headroom.

**For TASK-082 (120 Hz needs a render-only pass <= 7.3 ms):** today a pass would cost about 6-8 ms by day and
about 10 ms at night (EE render part + waits). After 1-4 a pass is about max(EE render part 3-4 ms, worker
3.5-5.5 ms) by day and night at 1x headless: inside 7.3 ms, without margin for the owner's heavy intervals (worker
7-8.5 ms paced). Item 8 (worker) and skipping env map/mirror/flare on render-only passes (TASK-082 3.2) close the
rest. TASK-045 (GS interpolation) does not need these items but benefits from 1 (no mid-frame stall).

**For weaker PCs:** item 1 and 3 turn the serial EE + worker path into a parallel one (the night race on a CPU
40% slower: today about 14 ms headless-equivalent, after about 8-9 ms); item 2 removes thousands of thread
wake-ups per frame, which matter more on 4-core/8-thread CPUs where the EE, the worker, gt4-gs and the GPU driver
share cores.

## 6. Missing instrumentation (add first, one small slice, no behavior change)

1. **Pacing sleep scope** (`release_frame` -> new `pacing_wait` category) so owner F8 `ee_dispatch` and
   `unattributed` mean work, not sleep.
2. **Fix the F8 summary formula**: critical path = wall time of the EE frame minus pacing sleep (or the
   sum of exclusive non-sleep categories), not CPU + waits.
3. **CPU time by `QueryThreadCycleTime`** (or `GetThreadTimes` deltas only per many-frame interval, already the
   case) plus CPU spent inside MTVU waits (cycle counter at wait entry/exit), and **wake-ups per wait**
   (`atomic::wait` returns) and **doorbell rings per frame**.
4. **Run-loop scopes**: `run_due_events`, `kernel::service`, `idle`, `preempt` count per frame, `run_guest`
   (interrupt handlers) time.
5. **Syscall counter** by number with time, cheap (array of counters, no text), reported per F8 interval; thread
   switches per frame.
6. **GIF split on the EE**: PATH1 (worker), PATH2, PATH3 bytes and records per frame; time of the `StreamSink`
   walk; journal `pump_output` copy bytes.
7. **Per-readback record**: EE return address (call site), BITBLTBUF/TRXREG (rectangle, PSM), download-sync wait
   before it, round trip after it, and gt4-gs service time; per interval and in the session line.
8. **MTVU job shape**: jobs per DMA chain, bytes per job histogram.
9. **GS process GPU time** (`GPU 0.000 ms` today: surfaceless has no timestamps; Unknown).

Without a sampling profiler these are the only way to split `ee_dispatch` (game code vs syscalls vs nested event
work). **Recommendation:** Windows Performance Toolkit (`xperf`/WPA, from the Windows ADK) for CPU sampling of a
headless run. `wpr.exe` exists on this machine but recording kernel sampling needs an elevated shell, and WPA is
not installed; nothing was installed for this study.

## 7. Open unknowns

- Split of `ee_dispatch` (2.9 ms day race) between translated game code, kernel HLE and nested event/DMA work
  (needs section 6 items 4-5 or a profiler).
- Whether the MTVU wait CPU is spin inside `WaitOnAddress` or only wake-ups plus `pump_output` (Hypothesis: mostly
  wake-ups, one per doorbell ring while the EE sleeps).
- The paced owner numbers at 3x in the night race after item 1 (headless 3x adds 1.3-1.9 ms of round trip per
  readback that item 1 removes).
- Why the whole EE side is about 25% slower with gt4-gs at 3x (CPU clock/SMT sharing with the GS thread, memory
  bandwidth, or driver threads; Hypothesis).
- Readers of the flare buffer other than `FlareClass::updateFlare` (gate for item 1; scan of loads from the
  buffer address in the translated units).
- The purpose and frequency of the `func_003308E8` 8x4 readback and the `func_001059F8` grab (0 in these runs).
- Whether coalescing changes VIF1 stall/MARK timing seen by the EE (High confidence it does not: the EE shadow
  still runs per transfer; only the worker post is batched).

## 8. Ready owner command (paced, windowed, for confirmation)

From the repo root (PowerShell), with the game's default renderer:

```
private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode pcsx2
```

Arcade -> Single Race -> City Courses -> Clubman Stage Route 5 (night) -> any car, 5 AI. Press **F7** when the race
starts, **F8** after about 30 s; then the same in a busy day race (El Capitan). A/B in the same scene:
`--gs-readback unsynchronized` (expect only about -1 ms of readback wait, flares missing) and `$env:GT4_MTVU="0"`
(expect the night race to be no slower than with MTVU). The report lands in
`private/manual-play/session-*/probe.log`; the lines to compare are `[mtvu] EE wait` (download), `[gs-ipc]
interval readback waits` and `[perf] frame ms`.
