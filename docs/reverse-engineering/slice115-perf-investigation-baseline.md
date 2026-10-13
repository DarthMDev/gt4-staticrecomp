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

## Rollback

Launch without `--runtime` (`Jogar-GT4.cmd`) to return to the qualified slice114
baseline. Experiment outputs live in `private/slice115-perf-investigation/` and
never overwrite the frozen slice114 producer.
