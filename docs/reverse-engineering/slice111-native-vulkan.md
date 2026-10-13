# Slice111 — native Vulkan with real GT4 output

2026-10-12, baseline `bd9ae77`, branch `codex/vulkan-renderer-pilot-111`.
Decision0053; owner priority following menu pilot 110.

## Results and scope

**Confirmed:** the new backend executes rasterization, texturing, transfers,
and framebuffer readback via Vulkan compute. It does not present an image
drawn by the CPU. The host enables `PS2_GS=gpu`; log confirms CPU rasterizer
disabled and real counters across the nine compute kernels. Win32 copies the
GPU-produced RGBA buffer into an image and blits to the swapchain, with no
image download to CPU on the presentation path. Separate PPM captures use
explicit GPU framebuffer readback.

Coldboot using a copy of card L reaches menu, course selection, and FordKa01
in 3D. Window screenshots show Nürburgring and the car. Execution of 3000 frames
terminates normally at the requested limit, without timeout. We do not qualify
racing, prolonged stability, complete GS coverage, or PS2 hardware fidelity.

## Build and constraints

MIT donor `snuri00/ps2-web-recomp` pinned at
`1b4808793a12c45125262fd2d5f50a9ec9f7c020`. Generator verifies five sources
before deriving GPU/GS/host under `private/slice111-vulkan/port`. Original checkout
preserved. Existing AOT `ps2game.lib` is reused; no new EE interpreter or EE JIT
was added. Inherited runtime and VU limitations remain isolated from production.

Clang 23.1.2, C++20/Release, VS2022 x64/Windows SDK, Ninja. Naga 30.0.0 with
a 32-package Cargo.lock transforms SCALE1 and explicit bindings across the kernels:
upload, copy_read, copy_write, load_clut, display, raster, resolve, snapshot, decode.
SPIR-V binaries remain private. Khronos 1.4.362 headers have their own
MIT/Apache2 license; no GPL implementation code from PCSX2 or PS2Recomp is linked.

Observed GPU: NVIDIA GeForce RTX 3060 Laptop, API 1.4.351; loader 1.4.341.
Backend requires Vulkan 1.2, audited limits, and KHR_swapchain_maintenance1.
Coherent mapped buffers prefer memory that is also GPU-local. Single queue,
synchronous fences, compute→compute/host/transfer barriers; zero-initialized allocations.
Destructor frees buffers, pipelines, descriptors, image, swapchain, semaphores,
fences, surface, device, instance, loader. Independent review identified improper
reliance on queue-idle as proof of present semaphore consumption: corrected via
maintenance1 fence waited on per present. The fence proves resource safety,
not the moment of physical monitor scanout.

Win32 has message pump on the same thread, basic keyboard input, and preserved
scripted input. Resize/zero-extent/restore are handled. Scale 1, nearest blit with
4:3 aspect ratio, no temporal field compositing. No GPU busy profiling,
qualified fullscreen, audio, or XInput. No Khronos validation layer installed;
absence of validation messages must not be claimed as proof of Vulkan validation.
Legacy OBS/RTSS layers appear in vulkaninfo, without causal diagnostic against the pilot.

## Controls

Integrated fixture within existing unittests when private build exists:
overlapping sprites across multiple tiles, channel masking, known display
pixels, snapshot/restore, and single-use readback serial. Compares the full 4MiB
VRAM against the separate CPU rasterizer. Byte-by-byte fragmented transfers pass
for formats PSMCT32/24/16/16S, PSMT8/4/8H/4HL/4HH, including neighbor bit
preservation. The CPU oracle shares addressing concepts with the donor; agreement
does not prove PS2 hardware fidelity. Thirteen repeated presents, zero-extent
rejection, and restore/resize pass. Final builds clean with no warnings.

Existing launcher extended for separate runtime with hash-pinned executable + shaders.
Continues verifying full ISO, CORE, ELF, and seed; fresh private copies and outputs,
literal argv, reason/frames/timeout at the gate. Negative controls reject altered
runtime/shader/donor and public output / frozen checkout targets. Game manifests
are never updated during verification.

## Preserved runs

- headless1: 1500 normal frames, 27.320833s, active GPU kernels. Menu at 600,
  Arcade at 1000, Nürburgring at 1400. Initial producer frozen in run directory.
- window2: first window control; keyboard neutralized scripted input because
  the donor applies presses before the callback. 1500 normal frames but divergent
  trajectory; rejected as equivalent. Exact producer was not frozen prior to rebuild;
  log/captures/result retained without treating run as final reproducible renderer evidence.
- window3: precedence corrected; 1500 normal frames in 32.221853s. 15/15 PPM captures
  byte-identical to headless1, budget cycles 7372994805 in both.
- window4: 3000 normal frames in 121.951303s, extended navigation through 3D Ford Ka,
  real window screenshots and private PPMs. Did not enter race.
- headless5: final confirmation after 64→32 offset guards and null buffer checks;
  1500 normal frames in 24.919064s, 15/15 PPMs identical to baseline.
- window6: final build reproduced with /W4 across all targets; 1500 normal frames
  in 30.679208s, 15/15 PPMs identical to baseline. Final producer frozen and
  runtime + shaders pinned for the launcher. Final SPIR-V compiler also reproduces
  9/9 binaries exactly in a separate directory.

Six runs / 248 artifacts and 17 proprietary source files pinned in manifest; window2
is explicitly rejected. Five accepted probes have 0 unknown services under audited
patterns and 1 inherited `cfc2 28` warning each, not yet qualified.
No timing policy was modified.

Full CTest 54/54 in 52.21s and Python 108 (102 run / 6 skipped) in 73.505s green.
Following final guards, the 5 Vulkan controls and 7 launcher controls pass again.
Production SHA `839adaa22f48ba674fc8f5f6c498299a9329598dedf23ca3ee507f1f1c76dfab`
and seed SHA `bb10a96fd78e45d648023f9962739c09f152ce01ae45c3fea1d998a71d5e3072`
remain unchanged. Copy_read, copy_write, and resolve compiled into pipelines
but were not dispatched during real probes; no execution coverage is claimed for them.
Snapshot, decode, and CLUT are exercised by the game, without a separate oracle
for each graphic contract.

Initial comparison between CPU110 and GPU111: frames 100/200/300 identical;
frame 400 has 142 different RGB bytes, subsequently up to 81972/860160 bytes in common frames.
We do not automatically attribute this to rounding or tolerance. Root cause remains
**Unknown**. The comparison file retains per-frame figures. Menu appearance and
navigation are confirmed; pixel-perfect accuracy of the full renderer is not.
Timing of 27.32s versus 56.57s for CPU control represents duration of these probes
with logging and captures, not sustained race FPS.

Failures during development: offline Cargo resolution rejected prior to locked
online build; initial adapter trimming removed addressing helpers and was corrected
prior to execution; mask fixture initially omitted setup generation, fixed to
reproduce real GS writes. LoadCursorW corrected to Unicode resource; proc list macro
rewritten one function per line following an incomplete mechanical cut. None of these
failed builds was accepted as a producer. Acceptance controls were not weakened.

## Reproduction and continuity

`experimental/vulkan/build.cmd` builds the pilot in the qualified local environment.
Verify manifest 111 prior to execution:

```powershell
private/tooling-venv/Scripts/python.exe scripts/external_runtime_probe.py `
  private/slice111-vulkan/new-run --frames 1500 --timeout 180 `
  --runtime-manifest docs/inputs/slice111-native-vulkan.json --window `
  --card-seed private/slice110-menu-pilot/name-control4/card.ps2 `
  --press 300:start --press 600:cross --press 900:cross --press 1200:cross
```

Build does not alter manifests; distinct binaries require requalification.
Next slice: first pixel divergence / Vulkan validation and controlled extension
into racing. Queue optimization or production adoption only with evidence;
audio, XInput, and saves remain subsequent goals.

Primary references: [semaphore reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html),
[present fences](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html),
[presentation errors](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html).
Hashes/versions/gates/captures in `docs/inputs/slice111-native-vulkan.json`;
private payloads, production binary, and original seed preserved.
