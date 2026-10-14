# Windows-first hybrid optimization and portable no-JIT builds

2026-10-14, slice 141. Owner-requested direction change from the scene-renderer
work at commit `25fd40e5e6c62c6ee2695d633ba1040aab1a5847`.
Branch: `codex/hybrid-no-jit-port-plan-141`. This revision is planning and
read-only source research; no performance run, code import, build or game launch.
`main` and the complete slice 140 native-scene research remain preserved.

## 1. Decision and priorities

Keep the working hybrid architecture and optimize real gameplay before replacing
the game graphics pipeline. Retain AOT EE code, the necessary PS2 service models
and a GS-compatible renderer. Permit selective PCSX2 implementation reuse with
provenance, compatible licensing and independently tested interfaces. Do not
import PCSX2 CPU/VU JIT engines or assume its whole renderer is a drop-in library.

The owner selected **Windows first**, followed by **Galaxy S23 Ultra / Snapdragon
8 Gen 2** and **iPhone 15 Pro Max**. Current qualified Windows GPU is RTX 3060
Laptop; the earlier entry-level dedicated GPU target remains an additional
qualification target, not an established minimum. Record exact CPU/GPU, driver,
power/display mode and available memory for each measured configuration.

Priority order:

1. Correct game speed and stable roughly 60 Hz gameplay at native internal scale.
2. GS rendering efficiency, correctness and configurable graphics.
3. Real internal upscaling, dithering controls and scan-mode presentation.
4. Portable ARM64 core and Android, then iOS packaging and sustained-device tests.

The [native scene-renderer plan](NATIVE-PORT-PLAN.md) is retained as an optional
later track. Further Ford Ka capture/geometry work is deferred on this branch.
144 distinct frames, physics/render decoupling, native ultrawide HUD/projection,
deferred rendering, full decompilation and elimination of all hardware models
are outside the immediate optimization/port milestone.

**No JIT means no runtime compilation of PS2 CPU/VU instructions into host
machine code.** Build EE and any compiled VU code ahead of time for each target.
Explicit instruction interpretation may remain where qualified; no-JIT does not
mean zero emulation. GPU driver shader/pipeline compilation is a different
operation and is allowed; cache/precompile shader variants where supported.

## 2. What is known and what is not

**Confirmed:** Windows EE code is AOT; IOP and current VU microprogram execution
are interpreted. The current Vulkan adapter preserves GS algorithms and uses
compute shaders. It is Windows-specific and only qualifies scale 1. Current
60 FPS gameplay stability, root cause of low 3D performance, ARM64 execution and
mobile performance are not established. See [Architecture](ARCHITECTURE.md).

**Confirmed source boundaries:**

| Existing boundary | Required work before portability or graphics changes |
|---|---|
| `native/CMakeLists.txt` | Windows libraries/options, private `.lib`/hash selection and fixed producer paths need explicit platform/ABI profiles |
| `native/native_vulkan.cpp` | System32 loader, Win32 surface and mandatory `VK_KHR_swapchain_maintenance1` require portable loading/surfaces and a tested presentation path without that optional extension |
| `native/window.cpp`, `native/host_io.cpp` | Win32 events, WinMM and XInput need platform implementations; preserve existing keyboard fix and save protections |
| `native/gpu_init.inc` | Explicitly rejects `PS2_SCALE` other than 1 |
| `scripts/prepare_external_vulkan.py`, `native/gpu_tail.inc` | Scale 1 memory sizes and state read/write contracts must change with a scaled renderer, not just the guard |
| `tools/ee_producer`, private EE library | Rebuild generated C++ for ARM64/NDK and Apple toolchains; x86-64 objects cannot be repackaged into a mobile app |
| Donor `runtime/src/vu_micro.cpp` | Compiled VU1 registration exists; active workload coverage, variants and speedup are unqualified |

**High confidence:** retaining the hybrid game-facing interfaces avoids making
complete mesh/camera reverse engineering a prerequisite for GS improvements.
**Hypothesis:** selective AOT VU or a hardware-raster GS backend could reduce
cost substantially. **Unknown:** which dominates, the required coverage, and
whether either alone reaches 60 FPS on the selected hardware. Measure first.

**Confirmed reusable donor mechanisms:** `recomp/vurecomp.cpp` already emits
offline VU C++/registration code with explicit interpreter exits; its GT4 coverage
is not qualified. `runtime/src/gs/gpu/gs.wgsl` has a `SCALE` override and
`runtime/src/gs/gpu/gpu.cpp` allocates scale 1..3 buffers with quadratic growth.
The native adapter does not expose those scales. Port and qualify that existing
scaling mechanism first when suitable; do not begin by replacing the whole GS.
DIMX/DTHE registers are stored, but correct active dithering remains Unknown.

## 3. Phases and exit gates

| Phase | Deliverable | Completion evidence |
|---|---|---|
| H0 | Freeze candidate baseline, inputs, no-JIT/build/license inventory | Fresh output profile, validated pins, source/dependency map, saves untouched, no reachable guest JIT |
| H1 | Owner-driven performance attribution on Windows | Comparable menu/showroom/race intervals; CPU work/waits, GPU work and guest speed separated; overhead quantified |
| H2 | Optimize demonstrated costs without JIT | One variable per candidate, tested semantics, repeated same-scene gain with audio/input/save behavior preserved |
| H3 | Qualify GS renderer direction and minimum interfaces | Reuse current compute mechanisms first; a bounded PCSX2-derived path is evaluated when profiling/capability evidence warrants it |
| H4 | Graphics controls and true internal upscaling | Tested 1x/2x/3x presets, color/depth/texture/feedback correctness, explicit dithering/deinterlacing, bounded GPU memory |
| H5 | Portable AOT core and host interfaces | ARM64 compile/link and numerical/dispatch controls; Windows remains green; no guest JIT dependency |
| H6 | Android arm64-v8a APK on S23 Ultra | Owner installs private build; gameplay, lifecycle, audio/controller, storage and sustained thermal performance qualified |
| H7 | iOS arm64 app/IPA on iPhone 15 Pro Max | macOS/Xcode build, signed installation, Metal/MoltenVK capability checks and equivalent sustained tests |

H1/H2 are the immediate critical path. H3 dependency/source research and H5's
small cross-compilation feasibility check may run in parallel; do not build full
mobile frontends before the core works. H4 high-resolution presets depend on
measured headroom, not on the ability to enlarge the window. H6 precedes H7.

### H0/H1: one baseline and an honest frame budget

Reconcile the current OpenCode work with its exact commit, changes, logs and
owner feedback. Keep its checkout/session untouched. Integrate only verified
proposals into fresh candidates; do not overwrite frozen 114/118/136 outputs or
reuse captures as performance traces. The keyboard correction stays in the
candidate; capture/verbose timing/instruction logging are off in normal runs.

Use a Release baseline with original native resolution and owner-selected 480p
race mode. Measure menu, Ford Ka selector and a fixed Nürburgring segment, then
expand to opponents, mirrors, effects and completed races. Record warm-up,
loading boundaries, car/track/camera, display mode and timed duration. All game
navigation is manual. Benchmark hotkeys delimit host telemetry only.

Report median/p95/p99 frame intervals, guest progress versus wall time, fresh
game-frame production versus presentation count, CPU execution/waits by EE/VU/
IOP/GS/audio, GPU timestamps where supported, submissions, readbacks, memory and
audio underruns. Mark unavailable measurements Unknown; do not double-count
nested timings or treat aggregate CPU utilization as attribution.

Proposed H1 screening gate: nominal 60 Hz (verify the actual guest/video
cadence), game speed within 99–101% over measured intervals, frame-budget reporting
around 16.67 ms with p99 <= 20 ms and no recurring stalls over 33.3 ms. This tolerance is
an initial diagnostic policy: **passing H1 does not qualify stable 60 FPS**.
Exclude explicitly marked loading transitions; report them separately. Require
three comparable race intervals plus one 30-minute session/completed race with
working sound, input and save/return. Later low-tier/mobile targets receive the
same screening at a separately qualified preset; do not promise 1080p mobile 60 FPS.

Before claiming stable 60 FPS, H2/H4 must demonstrate sustained correct-speed
fresh game frames and presentation at the verified cadence on named hardware/
preset, with CPU/GPU critical-path p99 inside the actual frame budget and no
recurring missed deadlines. Report presentation intervals/deadline misses
separately from work timings, with explicit measured timestamp/display jitter
tolerance; test a suitable 60Hz/VRR display mode before generalizing to other
refresh rates. An average FPS value or H1's 20ms allowance cannot pass this gate.

### H2: select optimizations by measured cost

- **CPU/GPU synchronization:** batch work, eliminate proven redundant copies/
  readbacks and reuse resources. Replace ownership/lifetime synchronization
  correctly before reducing fences; retain feedback/dependency correctness.
- **VU microprograms:** if costly, collect content identities and actual variants,
  generate AOT C++ for those workloads, compile for x86-64 and ARM64, compare
  registers/flags/memory/GIF output against the reference interpreter. Address
  alone is not identity. Self-modifying/unknown variants keep an explicit measured
  interpreter fallback; no hidden JIT. VU0 micro execution has its own contract.
- **EE:** profile dispatch/generation overhead, then evaluate inlining or LTO
  with separate manifests. Do not enable blanket fast-math, change guest rounding
  or use host undefined behavior to gain throughput.
- **IOP/audio:** profile before considering AOT or service-level changes. Preserve
  DMA/IRQ/RPC, audio clock and card contracts; disabling sound is diagnostic only.

No speedhack, raised guest clock, omitted opponent/effect work, dropped simulation
steps or duplicate presentations qualifies as an improvement. A differential
against the hybrid baseline proves preservation within that model, not independent
PS2 fidelity; known divergences stay documented.

### H3/H4: GS hardware rendering and PCSX2 features

PCSX2's pinned GS code has internal scale handling, a texture/target cache,
hardware primitive rendering and Vulkan shader variants for dithering. It is
coupled to its GS state, local memory, draw analysis, device abstraction, host
configuration and common utilities. Its GLSL/fragment-shader logic cannot simply
replace this donor's compute rasterization with the same inputs.

Use a bounded interface at the GS packet/register/transfer boundary. Inventory
the required GS state, vertex conversion, texture/CLUT/cache coherence, render
targets, CPU readbacks, blending/depth/alpha test, framebuffer masks, destination
alpha and feedback effects. Keep current compute 1x as a development/reference
path; an optimized optional path must report unsupported behavior explicitly.
Do not silently disable GT4 effects to satisfy missing mobile GPU features.

First use the donor's existing scale-aware compute implementation as the smallest
candidate for internal upscaling, with qualified allocator/shader/transfer/state
changes. If profiling or capability limits justify a different GS renderer,
build independent small primitive/texture/depth/blend/feedback
fixtures for the proposed boundary. Measure CPU conversion cost, GPU work,
memory and dependencies before choosing **extend compute** or **adopt a bounded
PCSX2-derived hardware backend**. Import GS/common code only as required; no Qt
frontend, generic EE JIT or whole emulator import is a prerequisite or default.
Pin every imported file/dependency and preserve notices/tests. A hardware-GS
renderer still models GS and still receives VU-produced work.

Graphics work order:

1. Correct 1x rendering and a stable baseline; retain 16:9/optional 4:3.
2. Internal 2x/3x scale with separate output/window dimensions and memory budget.
   Native guest buffer dimensions vary, so 3x is not universally 1920x1080.
   Rasterization, color/depth targets, texture feedback and readbacks must scale
   together; enlarged blits or final-image filters alone are not internal upscaling.
3. Dithering default consistent with GS state, then tested disabled/native/scaled
   options if the chosen backend supports them. Validate color quantization,
   dither-matrix coordinates and ordering with blending/framebuffer formats.
4. Interlace presentation as a separate feature: initially qualify 480p where the
   game supports it; implement field-aware deinterlacing for menus/480i/1080i.
   Dithering does not remove interlacing. Progressive host output is not proof
   that guest video timing is progressive or that forced 480p is safe everywhere.
5. Texture filtering/mipmap controls only with correctness fixtures and measured
   cost. Optional final-image scaling is labeled separately. 4K is a later preset.

Native ultrawide camera/HUD behavior and 144 distinct frames still require game
hooks. A GS backend does not automatically deliver either feature.

### H5/H6: portable core and Android

Start with small compiled controls: endian/bit conversions, wrapping arithmetic,
128-bit guest state, alignment, floating-point behavior, dispatch and memory
bounds. Audit intrinsics/assembly/ABI/toolchain flags; provide explicit portable
implementations before targeted NEON optimizations. Recompile every AOT object
for each ABI; retain the pinned translation inputs and separate target manifests.

Extract only necessary host interfaces: window/surface/events, monotonic clock,
audio queue, input/rumble, file selection/disc reads, and card storage/locks.
Prefer existing project interfaces and small platform implementations over a
general plugin system. SDL is an option to evaluate, not a required new dependency.
Saves remain atomic/backup-protected; Android import/storage permissions and app
lifecycle must not corrupt or reseed the owner's card.

Android uses NDK/CMake `arm64-v8a`, Vulkan capability negotiation and Android
surface/audio/controller integration, packaged in a private signed APK. Audit
timestamp availability, alignment/page sizes, memory budget and extension support;
remove the assumption that all devices expose desktop presentation extensions.
Test pause/resume, focus/audio interruptions, surface recreation and controller
disconnect. The owner navigates the game. Initial mobile preset is 1x, followed by
an optional higher scale only with measured sustained headroom.

### H7: iOS, executable provenance and signing

iOS needs ARM64 recompilation, Apple SDKs/Xcode on macOS and a signed application;
an IPA filename is not an installability guarantee. Use a private development/
registered-device installation first. Exact signing accounts/build host are open
prerequisites; no service, subscription or certificate is created by this plan.

Prefer Vulkan through MoltenVK first to share rendering code, with capability
tests and `VK_KHR_portability_subset`/Metal surface handling. MoltenVK translates
Vulkan to Metal; it is not PS2 emulation and cannot guarantee every Vulkan
extension or desktop fast path. A direct Metal backend is a measured fallback
decision if essential workload behavior cannot be mapped efficiently.

Precompiled EE/VU native code must be part of the app's signed build. Loading
an arbitrary newly compiled/unsigned game library on the phone is not the
no-JIT deployment strategy. The owner prepares local private inputs before
building/signing. Keep game assets, translated game code, cards and binaries
out of Git/public artifacts. Distribution of an APK/IPA containing translated
game code is not automatically permitted by an emulator library license.

App Store distribution is separate from personal installation: review Apple
executable-code/IP rules and GPL delivery compatibility before choosing that
channel. No-JIT alone does not establish App Store eligibility. Sustained tests
on both phones include thermal throttling, battery/power state, memory pressure,
audio continuity and lifecycle, not just a cold short benchmark.

## 4. Reuse and licensing decision

Owner explicitly permits PCSX2 source reuse. Local research reference is clean
at `aa7ab4306e269075784c7ac3eb4b45e6e6c53445`; GS source headers declare GPL-3.0-or-later.
This slice copies no implementation and changes no existing license.

Private modifications and conveying a build have different GPL obligations.
Plan any combined derivative containing copied/linked GPL GS code as a
GPL-compatible distribution, with corresponding source, license texts,
copyright notices and build information as required; a separate module name is
not an automatic exemption. Audit per-file/third-party licenses and project
copyright authority first. Preserve MIT donor and Khronos/Naga notices.
Store terms/signing and game-code rights remain separate release questions.
See [third-party boundaries](../native/THIRD_PARTY.md).

## 5. Work allocation and immediate package

Economical workers handle source/dependency inventories, bounded ARM compile
checks, repeatable fixture scaffolding and verified documentation. Strong
reviewers handle synchronization, GS memory/cache semantics, numerical VU
translation and backend cutover. One integrator owns tracked changes; builds/
tests run serially. OpenCode's current investigation stays isolated and supplies
evidence rather than assumptions. All gameplay remains owner-controlled.

Next package: [H0/H1 baseline and attribution](tasks/HYBRID-H0-H1.md). Do not resume
the old geometry capture by default or launch all stages in one unreviewed rewrite.
Each accepted slice gets code/tests/docs, fresh private outputs, a commit and a
push on this track; `main` advances only when a qualified slice is explicitly
integrated. No stable 60 FPS, upscale or mobile-build claim is made by this plan.

## Primary sources checked on 2026-10-14

- [PCSX2 GS hardware renderer](https://github.com/PCSX2/pcsx2/blob/aa7ab4306e269075784c7ac3eb4b45e6e6c53445/pcsx2/GS/Renderers/HW/GSRendererHW.cpp): scale/cache/dithering and GS coupling.
- [PCSX2 Vulkan device](https://github.com/PCSX2/pcsx2/blob/aa7ab4306e269075784c7ac3eb4b45e6e6c53445/pcsx2/GS/Renderers/Vulkan/GSDeviceVK.cpp): shader/device integration and dithering variants.
- [PCSX2 GPLv3 text](https://github.com/PCSX2/pcsx2/blob/aa7ab4306e269075784c7ac3eb4b45e6e6c53445/COPYING.GPLv3): sections 2/5/6; implementation files specify GPL3+.
- [Android NDK ABIs](https://developer.android.com/ndk/guides/abis): per-ABI compilation and arm64-v8a packaging.
- [MoltenVK runtime guide](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md): Metal mapping, surfaces, portability and capability limits; implementation pin selected before use.
- [Apple registered-device distribution](https://developer.apple.com/documentation/xcode/distributing-your-app-to-registered-devices): signing/install flow.
- [Apple App Review Guidelines](https://developer.apple.com/app-store/review/guidelines/): executable-code and intellectual-property requirements; future distribution needs a fresh review.
