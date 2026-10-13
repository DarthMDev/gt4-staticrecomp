# Slice 115 — Owner-controlled performance investigation: instrumented baseline

2026-10-13, branch `codex/perf-investigation-115`, source baseline `88c0e96`
(`88c0e96` = `cbccf80` documentation/launcher translation plus the OpenCode handoff
commit). Authority: [OPENCODE-PERFORMANCE-HANDOFF.md](../OPENCODE-PERFORMANCE-HANDOFF.md).
No guest clock, VBlank, physics, resolution, deinterlacing or input change was made.

## What was built

One instrumented baseline plus the tooling needed to build more experiments:

| Item | Location |
|---|---|
| Host diagnostics | `native/perf_probe.hpp` (new) |
| Measurement points | `native/native_vulkan.cpp`, `native/host_io.cpp`, `native/window.cpp` |
| Automated control | `native/perf_probe_fixture.cpp` (new), CTest `native_performance_probe` |
| Experiment build | `native/build_experiment.cmd` (new), `GT4_PORT_DIR`/`GT4_SHADER_DIR` cache variables in `native/CMakeLists.txt` |
| Candidate build | `private/slice115-perf-investigation/baseline/` (ignored) |
| Candidate manifest | `docs/inputs/slice115-perf-baseline.json` (experimental, not a release) |
| Launcher selection | `scripts/play_external_runtime.py --runtime <manifest name>` |

The default build path is unchanged: without `--runtime`, `Jogar-GT4.cmd` still
selects `docs/inputs/slice114-native-base.json` and the frozen slice114 producer.
`native/build.cmd` is untouched.

## Instrumentation model

Every guest VBlank cycle is accounted on the single emulation thread:

- `frame period` — wall time between consecutive `on_vblank` calls (the guest
  frame boundary) and the presentation cadence.
- `guest_between` — frame period minus the host callback: guest execution, GS
  batching and whatever wait that work performs on this thread.
- `host_vblank` — the host callback: window pump, input poll, `gs::present()`.
- `gs_upload`, `gs_execute`, `present`, `audio_push` — wall time of those stages.
- `gs_submit_wait`, `acquire_wait`, `present_wait`, `audio_wait` — blocked time
  inside a specific OS/driver wait. `blocked` is the sum of these waits, so
  `frame period = guest_between + host_vblank` and
  `period = thread work + blocked`.

Counters per frame: GS executes, dispatches, workgroups, presentations,
upload bytes, guest primitives, interpreted VU1 calls, audio frames produced
and device-completed. Guest progress is reported as guest cycles per wall
second against the 294.912 MHz EE clock, so 100% means the guest advances at
hardware rate rather than the FPS overlay being read.

Recording: a report is printed every 5 seconds and restarts the interval.
`F7` restarts the interval, `F8` prints it on demand, `F9` toggles the probe.
These keys are not in the pad binding table, so they cannot reach the game.

Overhead control: the probe is disabled with `GT4_PERF=0`.

## Verified build and controls

Build: `cmd /c native\build_experiment.cmd baseline` (VsDevCmd x64, chcp 65001,
clang-cl 23.1.2, C++20, Ninja, CMake, cargo 1.95.0 locked Naga 30.0.0,
VS2022 SDK 10.0.26100.0). Zero warnings.

- CTest 4/4 passed in 23.8s: `native_vulkan_pixels`, `native_audio_input`,
  `native_performance_probe`, `python_native_contracts`.
- Python 34/34, no skips.
- Candidate executable `private/slice115-perf-investigation/baseline/build/gt4vulkan.exe`
  SHA256 `4f31c723a5cf8093beabc12148039ee54d94d6685311212a5a25628a997743c2`,
  54,468,096 bytes, pinned in `docs/inputs/slice115-perf-baseline.json`.
- All nine SPIR-V modules byte-identical to slice114 (independent control that
  the shader compiler output did not change).
- 120-frame headless smoke with the candidate: `completed_requested_frames`,
  `reported_budget` 589983844 (identical to the slice114 smoke) and
  `frame_00100.ppm` SHA256 `0bb0b9d65987fdbbeb0d49f809325b1eb8c632b61348a43ddc7bc806af038bc4`,
  byte-identical to the preserved slice113 capture. The instrumentation does not
  change rendered output or guest progress.
- Headless overhead A/B (3 runs each, 120 frames, alternating): median 2.622s
  for the qualified slice114 executable and 2.621s for the instrumented one,
  i.e. below measurement noise.
- GPU reported by the runtime: `NVIDIA GeForce RTX 3060 Laptop GPU`,
  Vulkan API 1.4.351.

Build reproducibility limit: two rebuilds of identical sources produced
executables differing in exactly one byte, offset 128, the PE COFF
`TimeDateStamp` (values 1791875850 and 1791875907, matching the two build
times). The binary is therefore deterministic apart from the linker timestamp,
but it is not byte-reproducible across time; each experiment manifest must pin
the exact binary that was tested.

## Instrumentation defect found in the first manual run, and correction

The first candidate ran while the owner played menus and 3D content. Its report
had four defects, all inside the probe, none in the runtime:

1. `frame period ms: median/p95/p99/max` was always `0.00`. Samples were indexed
   into a fixed 4096-entry ring while an interval only writes a few hundred
   indices, so the percentile window read never-written zeros. The period
   history is now a bounded vector of the most recent samples.
2. `guest cycles`, `primitives` and `vu1_interpreted` were accumulated as
   absolute runtime counters instead of deltas, so their per-frame values were
   meaningless. The interval now stores differences against the previous sample
   and tolerates a counter reset after a state load.
3. The first line now also reports guest frames/s against the 60 Hz guest
   VBlank rate, and a separate line reports guest progress against the
   294.912 MHz EE clock.

Corrected candidate: `private/slice115-perf-investigation/baseline-fix/build/gt4vulkan.exe`,
SHA256 `e0887e6b366a030745282b1ce5e94a9b7c3eaa94e16b73f43b742ac4002b11aa`,
pinned in `docs/inputs/slice115-perf-fix.json`. The first candidate
(`docs/inputs/slice115-perf-baseline.json`,
`5c571014348ef944a2ecff04881f5604646479c630895009bd6d0b9b7e14c776`) is preserved,
including the binary that produced the first session log. The automated fixture
now asserts the delta accounting and the corrected report lines.

## Preliminary live observations (owner-driven scenes, not controlled runs)

From the first manual session, where the owner navigated menus and 3D content.
These are single-scene observations, not a controlled comparison:

- Menu/boot phases: about 42 guest frames/s, with `audio_wait` of roughly
  16-20 ms per guest frame and thread work of about 2 ms. The emulation thread
  is paced by the WinMM queue, not by the guest or the GPU, in these scenes.
  The 4 x 512-frame buffer quantum (10.67 ms) multiplied by the ~1.6 buffers
  needed per 800-frame guest audio frame is consistent with the observed rate.
- A heavy 3D scene reached about 15 guest frames/s (about 66 ms per frame):
  54.7 ms outside the host callback, of which 10.8 ms is GS submission with
  10.1 ms blocked on the batch fence, and 56.3 ms of thread work against
  10.1 ms blocked. In that scene the emulation thread was CPU-bound and the
  GPU fence wait was about 15% of the frame.
- `acquire_wait` and `present_wait` stayed near 0.01-0.07 ms per frame in all
  observed scenes so far, so the presentation wait itself is not the limit in
  what has been measured.

These numbers come from the first candidate, whose percentiles and guest-side
counters were defective; the frame rate, the stage attribution and the wait
attribution are valid, and the corrected candidate reproduces the same
measurement points.

## Hypotheses, in the order the baseline data can decide them

These are hypotheses to measure, not conclusions. The baseline report
separates guest work, host presentation waits and audio backpressure, which is
enough to rank them:

1. **Presentation/synchronization stalls dominate.** `Context::present` waits
   for the present fence after every `vkQueuePresentKHR`, and
   `vkAcquireNextImageKHR` is called with `UINT64_MAX`. If `present_wait` is a
   large fraction of the frame period, the cost is the FIFO/refresh
   quantization of one presentation, not the guest.
2. **GPU execution dominates.** `Context::execute` submits and then blocks on
   the submission fence, so host blocked time there also measures GPU
   execution of the batch. If `gs_submit_wait` is large while guest work is
   small, the GS compute path is the limit.
3. **Guest CPU work dominates.** If `guest_between` is large and both waits are
   small, the limit is EE/VU/IOP/GS batching on the host CPU.
4. **Audio backpressure shapes the frame cadence.** The WinMM sink blocks when
   four 512-frame buffers are full. `audio_wait`, and the produced versus
   device-completed audio frames per frame, separate legitimate pacing from
   underruns (audio produced faster than the device consumes) and stalls.
5. **VU interpretation is expensive.** `vu1_interpreted` per guest frame gives
   the interpreted call rate; it does not by itself prove VU is the limit, and
   it must not be read as one.

Diagnostic variants (audio-disabled, probe-disabled, no-presentation) are
allowed only as labeled comparisons, never as the player build.

## Method for the manual comparison

- 16:9, 480p preferred (owner selects in game Options, F6 matches framing).
- Warm up, then press F7 to start a fresh interval, hold the scene, press F8 to
  print the interval, and record the scene, car/track, camera and window size.
- The session log is `private/manual-play/session-YYYYMMDD-HHMMSS-ffffff/probe.log`
  (stdout and stderr of the candidate). A per-session backup of the memory card
  is kept as `card-before.ps2` in the same directory; the persistent card
  `private/manual-play/card.ps2` is never reseeded.

Rollback: launch without `--runtime` (`Jogar-GT4.cmd`) to return to the qualified
slice114 baseline. Experiment outputs live in `private/slice115-perf-investigation/`
and never overwrite the frozen slice114 producer.

## Launch commands used

Both launches reuse the persistent launcher's card/lock/backup/probe protections
in interactive mode (`--frames 0`, a window, no `--press`, 43200 s timeout):

```powershell
# Hidden console, visible game window; log inside the session directory.
Start-Process cmd.exe -ArgumentList '/c',
  'C:\Antigravity\gt4-staticrecomp\private\slice115-perf-investigation\launch-baseline.cmd'
  -WorkingDirectory C:\Antigravity\gt4-staticrecomp -WindowStyle Hidden -PassThru
```

`launch-baseline.cmd` runs `scripts\play_external_runtime.py --runtime
slice115-perf-baseline.json`. Replace the manifest name to launch another
candidate; the default (no `--runtime`) remains the qualified slice114 baseline.

## Iteration 2 — first session analysis and subsystem attribution experiment

Analysis of the first manual session (494 interval reports, owner-driven El
Capitan/Nordschleife racing, menus and garage):

- FPS distribution: median 16, minimum 7.3, maximum 60.1. Most intervals fall
  in 15-19 FPS. Track sections moved between roughly 7 and 30 FPS, matching the
  owner's report.
- Menu and low-work scenes reached a fixed 60 FPS with a 16.7 ms frame period,
  but the composition was `audio_wait` 5-12 ms plus 2-6 ms of other work, not
  idle. The guest produces roughly 800 audio frames per guest frame, which at
  48 kHz is 16.67 ms, so a full WinMM queue paces the emulation to exactly the
  60 Hz guest VBlank rate. The 4 x 512-frame buffers make that pacing quantized
  in 10.67 ms steps, which is a plausible source of the owner's "weird
  frametime" in the menu.
- Every 3D interval had `acquire_wait` and `present_wait` at 0.01-0.07 ms per
  frame, so the presentation wait is not a factor in what was measured.
- `gs_submit_wait` (host blocked on the GS batch fence, which also measures GPU
  execution of that batch) was 9.5-10.5 ms per frame in most 3D intervals and
  rose to 17.4 ms in the heaviest ones.
- The rest of the frame was thread work outside the measured waits, i.e. guest
  CPU work: about 54 ms per frame at 15.5 FPS and about 126 ms per frame in the
  slowest interval. That is the leading hypothesis, ahead of VU, Vulkan and
  presentation.

Experiment built for it: `scripts/prepare_perf_runtime.py` derives three frozen
donor translation units into a private directory after verifying their donor
hashes, and applies four one-line patches that open a probe Scope around EE
translated dispatch, interpreted VU microprogram execution, IOP execution and
VIF/GIF packet processing. `GT4_RUNTIME_DIR` (default empty) selects the derived
unit per file, so the frozen donor stays the source of everything else;
`native/build_experiment.cmd` derives it when `GT4_PERF_RUNTIME=1`. The report
gains an `attribution` line and a nested `vu_interpret`/`vif_gif` line.

Candidate: `private/slice115-perf-investigation/subsystem-attribution/build/gt4vulkan.exe`,
SHA256 `69eb3cc98249eb125a9e6ae8f1942442c8f4696ff8944b1da9c128d3f923a726`,
manifest `docs/inputs/slice115-perf-subsystem.json`. Controls: zero-warning
build, 4/4 CTest, 34/34 Python, 120-frame headless smoke with identical guest
budget and frame 100 byte-identical to the preserved slice113 capture, and a
3-run headless A/B with 2.872 s (qualified) against 2.871 s (derived runtime).

Overhead limit: the `vu_interpret` scope is entered on every interpreted VU1
microprogram, so its cost scales with the VU1 call rate that the same build
reports per frame. The headless A/B scene has little VU work and therefore
understates that cost; treat the attribution percentages as an upper bound on
the subsystem they measure until the per-frame VU1 call count is known.
