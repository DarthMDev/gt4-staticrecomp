# Plan: Speed, Rendering, and Selective Migration to Native Code

2026-10-13, following slice 114 / commit e431fb3. Planning requested by owner;
no implementation, performance profiling, or new race run in this revision.

## Recommendation

Maintain the hybrid base as a functional baseline and modify it incrementally. Eliminating
all emulation is not a prerequisite for decoupling game speed and FPS. Recompiling
VU/IOP and decoupling update/render address distinct problems.
Prioritize build reproducibility, reliable measurements when authorized,
time management, and presentation decoupling. Next, investigate
the game loop required to generate additional frames. Migrate subsystems
only when there is demonstrated overhead or a concrete dependency on the new architecture.

## Current Evidence: Confirmed

Frozen donor code at `private/references/ps2-web-recomp`, commit
1b4808793a12c45125262fd2d5f50a9ec9f7c020; local source code at commit e431fb3.
Method: static inspection of source files and slice 114 manifests, without running the game.

- `runtime/src/system.cpp:85` computes VBlank budget as EE clock
  divided by 60 NTSC / 50 PAL. This is current runtime policy, not proof of
  exact hardware cadence or internal GT4 physics rate.
- `system.cpp:127–141` invokes the host callback inside the VBlank event,
  prior to signaling hardware/IOP/kernel and scheduling the next occurrence.
- `native/window.cpp:80–106` gathers input and calls `gs::present()` inside that callback.
  `window.cpp:173–174` explicitly uses a single thread and FIFO presentation.
- `native/native_vulkan.cpp:693` selects FIFO. Lines 858–865 wait for actual
  presentation completion; lines 509–513 also wait for GPU submissions. These locations
  block execution; we have not measured their share of overhead during races.
- `system.cpp:329–336` advances IOP based on guest time, and lines 366–384 execute
  EE/events. SPU2 audio originates from IOP jobs; `native/host_io.cpp:219–273`
  can wait on Windows device buffers. Audio is another source of stalls.
- VU is interpreted in this configuration; dispatch to compiled microprograms exists
  in `runtime/src/vu_micro.cpp:788–812`, with content-addressed registration/caching
  at lines 817–854. `vu_jit_web.cpp:18–30` disables JIT outside Emscripten.
  The examined compiled path is VU1; equivalent support for VU0 requires an audit.
- IOP interprets instructions and already replaces imports with HLE services.
  See `ARCHITECTURE.md` for limits and additional references.
- The donor already provides a GS queue and consumer in `runtime/src/gs/gs.cpp:963–1025`,
  used by the original host in `host/window.cpp:310–341`, but unactivated in the
  current Win32 adapter. `post_present()` limits pending presentations to two;
  literal reuse may still block the producer. `start_thread()`
  initializes the queue; the host creates the thread. Evaluate/reuse before building
  new infrastructure, preserving GIF/reset/download/checkpoint in order.

## Hypotheses and Unknowns

**Hypothesis:** Interpreting VU may incur significant overhead in 3D scenes.
The reported framerate drop when cars/tracks appear is also consistent with
GS/compute workload, synchronization stalls, or EE code. The report does not isolate the cause.
**Unknown:** Breakdown of EE/VU/IOP/GS/audio overhead, blocked/stall time, true physics cadence,
routines that update state and assemble scenes, dependencies between draw calls and
gameplay, temporal fidelity. Do not label VU as a confirmed bottleneck.

Recompiling VU/IOP generates x86-64 code while preserving PS2 semantics. This can
accelerate execution without eliminating hardware models or changing the linkage to FPS.
Replacing services with PC APIs requires separate contracts for DMA/IRQ/RPC,
audio, and memory card; the fact that they are software models does not mean they need to be removed.

## Desired Outcome

Three cadences must be made explicit: simulation time, scene generation, and
display presentation. The host monotonic clock governs target speed;
guest events preserve order and timing costs, while physics maintains qualified
original timesteps. The monitor refresh rate does not determine how many physics steps advance.

| Target Scenario | Behavior |
|---|---|
| 30 FPS Presentation | Simulation continues at original cadence; presents fewer frames |
| 60 FPS Presentation | Original simulation, matched presentation |
| 144 FPS Presentation | Original simulation; new frames use interpolated visual states |
| 144 Hz Monitor with repeated frames | May preserve game speed, but does not satisfy 144 distinct frames per second |
| CPU unable to simulate in real time | No guarantee of 100% speed; requires optimization or reducing simulation-independent work |

Do not raise VBlank/clock rate to 144 or indiscriminately alter the physics
delta time. Examples use 60 solely for illustration; qualify the original cadence
before freezing an architectural contract. Measure 100% game speed as game time / wall-clock time,
alongside step and event counts, rather than relying on an FPS overlay readout.

## Proposed Phases and Completion Criteria

### 1. Make Game Code End-to-End Recompilable

Document and automate EE code generation and library registration from pinned
inputs, compilation, and integration into a clean directory. Currently, the build reuses
a private `ps2game.lib` with a fixed hash. Modifying the GT4 loop cannot be achieved merely
by editing the adapter. This does not require decompiling the entire game: readable
translated code and surgical hooks are sufficient initially.

Complete when a new producer reproduces behavior across existing boot,
menu, and race controls, identifies all tools and inputs, and does not depend
on a binary library lacking a generation recipe. Do not require byte-identical binaries if
build metadata changes; document differences and verify behavioral equivalence.

### 2. Establish Functional and Timing Baseline, When Authorized

Use identical inputs, car, track, controls, cloned memory card, and observation windows.
Record guest vs. wall-clock speed, game steps, input, audio samples, and EE/VU/IOP/GS cost breakdown;
GPU timestamps and presentation/readback/audio wait times. Use frametime percentiles
and coherent profiling traces, not aggregate CPU/GPU utilization percentages.
Headless/no-presentation test controls must maintain execution and required GS operations;
do not arbitrarily disable the GPU and assume equivalence.

Complete with attribution sufficient to select the first optimization target,
repeatable controls, and explicit bounds. The prior instruction to only
record issues remains in effect: this phase has not yet been executed.

### 3. Decouple Presentation from Guest Timestep Progression

Separate the VBlank/input/event callback from display presentation, preserve
current guest event ordering, and pace progression using a monotonic host clock. Implement the
minimal safe decoupling first; add a dedicated thread only if necessary. Use frame images with
explicit ownership and lifetimes alongside a bounded queue; preserve GS work upon which
the game depends. When the display is slow, drop outdated presentation frames—never
physics/DMA/IRQ events or audio samples.

Merely removing fences is insufficient: current buffers and semaphores rely on them. Do not move
`gs::present()` to another thread while accessing shared global state. Handle
input by timestamp/event ordering and poll it on the guest step; pace audio by clock
with a bounded queue; handle pause, focus loss, resize, and window close without simulation jumps.
Designate a single authority for pacing speed to avoid conflicts between host pacing
and audio backpressure; catch-up logic must not accumulate unbounded simulation work.

Complete with scheduling tests using a controlled clock and presentation caps of
30/60/120/144 FPS; identical timed inputs must produce identical state at the
same simulation steps. Verify race times, collision events, audio, and save states,
in addition to rendered frames. Real-hardware pacing requires adequate CPU/GPU
processing headroom. This can maintain 100% game speed without yet generating 144 distinct new frames.

### 4. Migrate Confirmed Hotspots Without Full Rewrites

If VU dominates, first leverage donor infrastructure to register GT4 VU1
microprograms AOT: collect program contents/variants across qualified scenarios, translate,
recompile, and compare registers, flags, memory, and GIF packets against the interpreter.
Account for self-modifying/dynamic microcode and preserve content-based identity; do not index solely by program counter (PC).
Unrecognized microprograms retain an explicit, observable interpreted fallback path.
VU0 requires its own interface contract and support. A Windows JIT is an alternative if dynamic
variations make AOT impractical, but carries a larger implementation surface.

If synchronization/GS dominates, prioritize that path before VU. If IOP dominates,
evaluate AOT for active modules or HLE replacement of targeted services,
preserving imports, DMA/IRQ/RPC, audio, and memory card contracts. If EE dominates, analyze
translation generation and dispatch overhead. Each migration must preserve guest timing semantics
and be verified against baseline execution; internal consistency alone does not prove PS2 fidelity.

Complete each modification with measured performance gains in the identical test scenario and zero
regressions in functional contracts. Full native IOP migration and wholesale elimination of
hardware models remain deferred until backed by concrete technical necessity.

### 5. Generate 144 Distinct Frames While Preserving Original Physics

Map in GT4 code the functions responsible for updating physics, AI, collisions, lap timers,
and input, as well as scene construction, camera, animation, and video sync waits. Formally determine
which functions can be called to generate a frame without advancing gameplay state or causing side effects.
Do not assume the game engine natively provides decoupled `update()` and `render()` entry points.

Capture previous and current visual transform states and interpolate camera, vehicle/mesh poses,
and dynamic objects for each presentation frame. Assemble and render a new scene from these interpolated states;
repeating the framebuffer or replaying a GS packet containing baked coordinates does not achieve this.
Decouple HUD/timers, particle effects, cutscenes, and camera transitions. Never write
interpolated visual transforms back into authoritative physics state. Interpolation requires defining
and measuring presentation latency, as well as resetting historical state on teleports, pauses, and scene transitions.

Begin with a single race/camera configuration, then expand. Complete with distinct frames
rendering at 144 FPS, bit-exact physics outcomes given identical inputs/timestamps, accurate audio playback,
and uncorrupted HUD rendering. This milestone requires localized reverse-engineering of the game binary
and a suitable rendering interface; it does not require a prior total rewrite of IOP or VU.

### 6. Progressive Scan and Internal Resolution, Followed by Visual Enhancements

Qualify 480p as the baseline, modify video mode selection compatibly across menus
and races, and implement true internal resolution scaling. Cover viewport,
render targets, depth buffers, textures, post-processing passes, and HUD elements.
Resizing the window viewport is insufficient. Provide launcher configuration persistence without risking
memory card saves; subsequently address ultrawide aspect ratios, camera adjustments, HUD scaling/repositioning,
high-resolution textures, and modern visual effects. This work can proceed in parallel with
decoupled rendering once interface boundaries are formalized.

## Scope and Commencement Decision

Recommended first milestone: reproducible EE code generation recipe and automated build pipeline.
Follow with a measured timing baseline and the first slice of presentation/timing decoupling;
select optimization targets based on profiled data. Do not embark on an upfront wholesale native rewrite.
The project owner requested architectural planning; all executable stages above are proposed roadmap phases,
neither implemented nor qualified in this revision.

## Primary External References

- [Glenn Fiedler, Fix Your Timestep!](https://gafferongames.com/post/fix_your_timestep/):
  Fixed timestep, time accumulation/interpolation, and processing headroom requirements.
- [Khronos, Swap chain](https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/01_Presentation/01_Swap_chain.html):
  Presentation, swapchains, and monitor refresh cadences. Does not identify our specific bottleneck.

External references provide general architectural context; they do not demonstrate GT4 engine hooks or game fidelity.
