# OpenCode Task: Investigate GT4Recomp Performance with Owner-Controlled Gameplay

## Assignment and scope

Work in `C:\Antigravity\gt4-staticrecomp` on Windows. Investigate why genuine
3D gameplay and the car showroom perform poorly. Measure competing explanations,
test small changes, build them, and leave the game open for the owner to navigate
and evaluate. We will iterate using the owner's feedback.

This is explicit owner authorization for **OpenCode** to profile and test
performance changes, superseding older "record only" performance notes. Codex
prepared this handoff only: it has not performed the performance investigation.
Do not expand this task into a full runtime rewrite, physics/FPS decoupling,
internal resolution upgrades, deinterlacing, or keyboard double-trigger fixes.

**Never use Computer Use, GUI automation, browser automation, screen-control
tools, SendKeys, simulated controller input, or scripted menu/race navigation.**
The owner controls the game from now on. Native fixture tests are permitted;
they do not authorize navigating gameplay. Launching the real game through its
CLI/launcher is authorized. Do not manipulate the owner's window after launch.

Keep documentation, reports, prompts, and new code comments in **en-US**.
Owner-facing conversation may remain Portuguese. Read the current `AGENTS.md`.

## Read first and establish the baseline

Read `docs/STATUS.md`, `README.md`, `docs/ARCHITECTURE.md`, `docs/BUILD.md`,
`docs/ROADMAP.md`, and `docs/reverse-engineering/slice114-native-base-cleanup.md`.
Start with `git status --short` and record HEAD. Handoff source baseline is
`cbccf80` (documentation/launcher translation); recheck before modifying anything.
Do not overwrite unrelated owner changes. Create a short-lived `codex/` branch.

Current execution is hybrid: EE game code is AOT C++/x86-64; IOP and VU are
interpreted; PS2 services/hardware remain software models. Vulkan compute handles
GS graphics, with Win32 presentation, WinMM stereo audio, keyboard, and XInput.
There is no browser, WebAssembly, or embedded PCSX2.

Owner observations, not independently calibrated measurements:

- Approximately 19 FPS in Nürburgring gameplay, 11 FPS in the 3D car showroom.
- Approximately 18 FPS / 72.5 ms in a 1080i screenshot; unstable frametime.
- Performance becomes poor when 3D content appears, including outside races.
- 480i/480p/1080i worked manually at 16:9; 480p felt most stable.
- Audio continuity is imperfect. Keyboard arrows reportedly double-trigger.
  Physical XInput has not been tested. These are separate observations, not
  established causes of the performance drop.

Do not conclude "VU is the bottleneck" or "Vulkan is slow" from these observations.
Low aggregate CPU/GPU utilization does not identify the expensive code or waits.

## Verified project paths and build constraints

- Active code: `native/`; root `CMakeLists.txt` and `native/CMakeLists.txt`.
- Frozen MIT donor: `private/references/ps2-web-recomp`, commit
  `1b4808793a12c45125262fd2d5f50a9ec9f7c020`. Keep this checkout unchanged.
- AOT game library: `private/reference-acceleration-study/runtime-build/ps2game.lib`;
  SHA256 `c837d13dae027f377aecdb0c39834ede2e9463eabf7f767fda6a3c5a0762de1f`.
  Generated EE source: `private/reference-acceleration-study/generated`.
- Adapter generator: `scripts/prepare_external_vulkan.py`; patches/snippets in
  `native/gpu_helpers.inc`, `gpu_init.inc`, and `gpu_tail.inc`.
- Qualified executable: `private/slice114-native-base/build/gt4vulkan.exe`;
  SHA256 `c634a3d32c9b3c32d2b0ab9d769d0682572a2f35761e397669e044117e2dc033`.
  Accepted manifest: `docs/inputs/slice114-native-base.json`.
- Private inputs: root `Gran Turismo 4 (USA) (v2.00).iso`,
  `private/fingerprint-check/CORE.GT4`, `private/reconstructed/SCUS_973.28.elf`.
  Verify using the existing manifests/launcher; never alter input pins.
- Owner save: `private/manual-play/card.ps2`; session logs/backups alongside it.
- Private Python: `private/tooling-venv/Scripts/python.exe`.
- Qualified toolchain: clang-cl 23.1.2, VS2022 x64 SDK/ABI, C++20, Ninja,
  CMake 3.24+, Rust/Cargo with locked Naga 30.0.0. Build in Release.
- Qualified GPU: RTX 3060 Laptop. Backend requires Vulkan 1.2 and swapchain
  maintenance1. Verify the actual selected device/driver during the investigation.

**Do not blindly run `native/build.cmd`: it currently writes the frozen slice114
adapter, shader, and binary directories.** Use it as the verified recipe, then
make the smallest necessary build-path change for fresh experiment outputs, e.g.
`private/slice115-perf-investigation/<variant>/`. Recheck that this location is
unused. Root/native CMake also hardcode slice114 adapter/shader paths: changing
only `-B` does not fully isolate a new producer. Preserve previous artifacts.

The recipe enters `VsDevCmd.bat -arch=amd64 -host_arch=amd64`, sets `chcp 65001`,
derives the adapter, builds the locked shader compiler, emits SPIR-V, configures
root CMake with Ninja/Release/clang-cl, then builds. Follow `BUILD.md` and record
the actual commands and versions. Preserve warning-free builds.

Use the existing AOT library for runtime/host diagnostics; completing the entire
ISO-to-AOT pipeline is not a prerequisite for investigating these costs. If EE
changes become necessary, qualify a separately generated library and provenance.
For donor instrumentation, derive a private copy/patch with recorded hashes;
do not silently modify the frozen checkout. Persist reproducible changes in
tracked readable tooling/source, never by editing only generated output.

## Where to investigate: hypotheses to measure, not conclusions

1. **CPU and waits:** sample the running process to separate EE translated work,
   VU interpretation, IOP execution, GS batching, audio, and synchronization.
   Aggregate counters/timings with bounded overhead; avoid per-instruction logging.
2. **GPU submissions and presentation:** inspect `native/native_vulkan.cpp`,
   especially `Context::finish_commands`, `execute`, `read`, `write`, `wait_idle`,
   and `present`. Submissions wait on a fence, presentation uses FIFO and waits
   for a present fence. Measure CPU blocked time separately from GPU execution
   using supported GPU timestamps or a suitable profiler. Account for uploads,
   downloads, batch sizes, submission counts, display extraction, and CPU copies.
   Nested measurements must not double-count time. Do not remove fences without
   replacing buffer/command/semaphore lifetime and ownership guarantees.
3. **Host callback coupling:** `native/window.cpp::on_vblank` polls controls and
   calls `gs::present` on the execution thread. Donor `runtime/src/system.cpp`
   calls it inside VBlank before delivering guest events. Its current video model
   uses 60 Hz NTSC / 50 Hz PAL; this does not prove GT4's physics cadence.
4. **VU/IOP costs:** inspect donor `runtime/src/vu_micro.cpp`, `vu_jit_web.cpp`,
   `iop/cpu.cpp`, and `iop/kernel.cpp`. Windows VU JIT is not active; current VU
   code is interpreted. Existing compiled VU1 dispatch and GS queue mechanisms
   are candidates for later reuse, not proof they are the first optimization.
5. **Audio/backpressure:** `native/host_io.cpp::AudioOutput` waits for WinMM
   buffers; SPU2 follows IOP time. Distinguish legitimate pacing, underruns,
   production cost, and long waits. An audio-disabled diagnostic is not a complete
   functional improvement and may change the pacing of the whole process.
6. **Diagnostic overhead:** existing `PS2_TIME` adds GS synchronization,
   `PS2_VU_COUNT` prints statistics, and capture/trace options change workload.
   Inspect their implementation before using them. Performance runs should not
   write PPM/PCM dumps or verbose syscall traces by default. Sampling a different
   driver/tool is optional only if useful; do not turn this into a tooling project.

## Measurement and iteration protocol

First prepare **one instrumented baseline**, build/test it, and launch it for
the owner. Do not require automatic menu navigation to establish a baseline.
Ask the owner to reproduce a simple menu, car showroom, and a chosen race segment;
they will provide feedback and measurements. Use 16:9 and preferably 480p for
the initial race comparison, selected by the owner. Record car/track/camera,
scan mode, window dimensions, warm-up and timed interval. Separate loading from
steady gameplay. Note power/thermal conditions if comparing laptop runs.

Provide a lightweight way to delimit measurement intervals during manual play,
such as a documented diagnostic hotkey that does not send game input. Keep
instrumentation bounded and compare enabled/disabled overhead. Record actual
frame intervals (median/p95/p99), guest progress versus wall-clock time, distinct
frames/presentations, CPU work/waits, GPU timings, submission/dispatch counts,
and audio queue health where available. Mark unavailable data as unknown.
More FPS caused by faster game speed, repeated frames, removed graphics, or
disabled sound is not a successful optimization.

After feedback, rank the hypotheses using the measured baseline. Test the most
promising explanations one at a time. Preserve the baseline and each experiment;
use comparable owner-driven scenes and repeat comparisons when noise warrants it.
Diagnostic variants may help isolate costs but must be labeled and must not become
the normal player build if they skip required work. Start with a small reversible
change; do not batch speculative VU/IOP/render rewrites. Each iteration ends with
a new tested build available for manual play and a clear request for feedback.

Do not alter guest clocks/VBlank, physics, scheduling contracts, DMA/IRQ order,
or saves to manufacture a performance gain. Retain real guest rendering and
required GS/readback semantics. Existing CPU/GPU discrepancies and CFC2(28)
are known limits, not permission to hide new failures or disable stop checks.

## Build, tests, and safe manual handoff

Build serially in the new directory, then run:

```powershell
ctest --test-dir <new-build-directory> --output-on-failure
private/tooling-venv/Scripts/python.exe -m unittest discover -s tests/python
```

Current suite contains 3 CTest entries, including 32 Python tests. Extend existing
fixtures for nontrivial changes and do not weaken assertions. The old 54 C++
tests belong to the archived engine. Record results of this build, not inherited
green results. Do not rebuild/link over a running executable.

Create a **new** runtime manifest under `docs/inputs` with exact size/hash pins
for the candidate exe/shaders and appropriate unchanged input/card references.
Label it experimental; do not repin slice114 or bypass verification to launch.
`external_runtime_probe.py` accepts `--runtime-manifest`, `--window`, and
`--interactive`, but the current `play_external_runtime.py` hardcodes slice114
and only accepts `--aspect`. Add the smallest tested optional manifest selection
to the persistent launcher, keeping the default baseline and all guards intact,
or provide an equally safe thin entry point reusing its protections.

The owner's game must use `--frames 0`, interactive window mode, **no `--press`**,
no automated captures, and a sufficiently long manual timeout. Preserve the
existing persistent card, byte lock, native exclusive lock, and per-session
backup. Never reseed/overwrite the existing card. Do not attach an experiment
to a pinned seed as a mutable save. If the owner already has a game running,
ask them to close it when ready; never kill processes or interfere with gameplay.

Launch through a background console helper with a hidden console window and
leave the actual game window visible. Record the process and session log path;
do not wait for the owner to finish playing before returning your handoff.
Do not launch multiple instances. A visible boot window is not proof that a
race is stable or faster: the owner will navigate and report that.

## Deliverables for each iteration

- Hypothesis, evidence, variant settings, exact exe/shader/source identity,
  measurement method, limitations, and comparison with baseline.
- Small readable code/tooling changes, passing relevant tests, and a documented
  rollback to the baseline. Persist raw traces/captures/binaries only in ignored
  private directories; metadata/reports may be tracked.
- Update STATUS and append the journal in en-US. Keep `main` green; commit/push
  verified changes only to `origin`. Experimental branches may remain separate
  pending owner gameplay feedback. Never modify upstream or rewrite history.
- Leave the tested candidate game open and tell the owner: what changed, exact
  build/variant, which scene to test, which numbers/visual/audio observations
  to report, any diagnostic mode, where logs are, and how to return to baseline.

First-turn completion is an instrumented, tested, manually launched baseline
plus a hypothesis/measurement plan. Return control to the owner and wait for
feedback; do not claim the performance problem is solved before comparison.
