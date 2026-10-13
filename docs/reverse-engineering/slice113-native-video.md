# Slice113 — native widescreen and guest video-mode qualification

2026-10-13, baseline f9346c8, decision0055. Separate Clang/native Vulkan pilot;
production MSVC and frozen111/112 inputs/executables/shaders remain preserved.

## Confirmed implementation

Presentation defaults to centered16:9; optional4:3 through F6 or
Jogar-GT4.cmd --aspect 4:3. The initial client rectangle is960x540 for16:9,
960x720 for4:3, with frame/title decorations accounted separately. Resize
keeps aspect and black margins. Tiny1x1 and empty extents avoid zero-sized blits;
intermediate arithmetic is64-bit, including UINT32_MAX fixture controls.
Display scratch now accepts at most2048x2048 pixels rather than1<<20.
Internal raster scale remains1. The nine compiled SPIR-V modules are unchanged.

The original game Screen Settings UI selects16:9. Pinned ELF10f82e22...c935c
qualifies getter/setter00180108, setter00437110 and projection updater004391C8.
Setter48B SHA256f77d23893267f701cfae129125c3f1a1ba5fc4d8ab072dcc2a886b1d600376da.
Normal projection templates use275/205, widescreen425/240. Race application
004365F0 writes wide flag006184F0 used by HUD layout; this flag does not qualify
menus before race setup. No guest-memory writes or translated-code changes.

Original UI path: GT Mode -> L's Home -> Options -> Screen. Aspect dropdown
Cross/Down/Cross chooses16:9; fourDown thenCross applies. Triangle opens Exit
Options, Cross confirms, and loading transitions must finish before subsequent
inputs. Home Options -> Down/Down -> Save, confirm through the actual game.
Derived widescreen/normal-video seed b757feff...609e6 is8650752bytes. Normal110
seed bb10a96f...e3072 stays frozen. Newly created cards select the aspect-matching
seed; existing cards are not reseeded by the launcher. Runtime-qualified seed
extension keeps size/hash checks and prevents treating a pinned seed as a mutable
manual card. Each manual session still backs up its card and holds both locks.

## Video options and observations

Screen Settings Video Output offers Normal, Progressive(480p), HDTV(1080i).
After selection, the game temporarily tests that mode with Cancel/OK; Right then
Cross confirms. A second Down/Cross applies the Screen Settings dialog. The
game then returns menus to the normal CRT mode while retaining the race option.
480p preview requests SetGsCrt interlace0/mode0x50/field1,640x448 no-halving.
1080i preview requests1/0x51/1,576x960 half-height source. Returned menu requests
1/0x2/1,640x448 half-height source. Nominal video mode and buffer dimensions
must be distinguished; the observed1080i preview fits even the former1M limit.
Race controls and final acceptance boundary are recorded below.

Original Save produces wide+480p card239d4c4d...39574 and wide+1080i
card492a2d80...fe0af. These are private derived fixtures, never payloads in Git.
Coldboot of these actual GT Mode saves resumes Home. The old110 Arcade input
prefix therefore incorrectly opens Garage: race480p17 completes5200frames in
118.360s but is explicitly rejected as race evidence. Garage/Home/GTMap use
Triangle with appropriate menu/transition handling; Triangle at the GTMap opens
Quit Gran Turismo Mode?, Cross confirms, title initially selectsGTMode, Up
selectsArcade. No scripted normal stop alone proves a race.

Private donor save-states are limited to this pinned113 producer; no production
checkpoint compatibility claim. Reader verifies PS2STATE/version2/ee tag and
three48B ELF anchors to locate RAM independently at fileoffset1460; only then
reads006184F0. Input states and copied cards retain hashes in each probe result.

## Automated gates

### Owner manual outcome after publication86b9df6

Confirmed owner report2026-10-13:480i,480p and1080i all worked in gameplay,
each selected16:9.480p was subjectively the most stable. Screenshot supplied
as1080i shows actual track/HUD and overlay18FPS/72.5ms with variable frametime.
Private418360B image owner-1080i-race-2026-10-13.png
SHA2560d9da76c6a3f3df83c4eaaba4e3ccb7be07e15c3a7a6f4c9a286feebd0c33623,
under private/slice113-owner-results; metadata in slice113-owner-results.json.
This supplies the requested owner functional confirmation, separately from
the preceding automated controls and immutable113 evidence. No independent
CRT/buffer/field trace was collected for these three manual selections.

Owner reports directional keyboard presses cause two menu movements. Other
buttons may be affected (Hypothesis); they were not tested. XInput physical
use remains Unknown. Duplicate delivery, key repeat and guest/host timing are
unqualified causes; no investigation or fix performed. Owner explicitly asks
only to register results; performance mapping remains future work.

Progressive-only game changes remain the intended later route, so additional
interlace work is deferred. Temporal deinterlacing and dithering are different
operations; neither the screenshot nor this report establishes a required fix.
Existing pending-mode paragraphs below record the earlier handoff checkpoint.

Race480p21 actually reaches Nürburgring with AI, car and running HUD timer
(capture13900:8.983s); normal14000 stop251.411s. Native race-wide flag is1 at
006184F0, independently anchored to the original ELF in the saved RAM.
Despite its experiment name, this race is normal640x448/half-height1 with
serialized CRT1/2/1. It is accepted for real widescreen racing, rejected as
480p race evidence. Pause Screen Settings only exposes brightness/contrast.
Pre-race fourth icon opens full Options; its Screen dialog shows16:9/Normal.
Prerace480-preview28 selects480p there and reaches the preview normally14300
in16.372s; final race-mode application remains unqualified.

Owner explicitly ended automated navigation and requested a persistent manual
window to configure and start races personally. Launcher startedPID21412,
session private/manual-play/session-20261013-020026-017182. Existing owner
card preserved, before hashbb10a96f...e3072: original4:3 seed, so owner must
select16:9 in GT4 as well as the already-wide window. No owner progress replaced.
No current-session completion/stability claim while that session remains open.
Real480p/1080i race activation is now an owner-assisted pending check.

Warning-free final new-target build after an initial missing derived-header
include directory failure (retained build.log). Fixture-only rebuild adds tiny
window control without relinking the game producer. Python117 tests,111run/6skip,
76.760s pass;54/54CTest51.37s pass. Existing acceptance expressions unchanged.
Actual GPU comparisons inspect every pixel of640x448/halved,640x480/progressive,
640x1080/halved,1920x1080 and2048x2048 using patterned full4MiB VRAM and
independent CPU swizzle reads. Actual presentation exercises all five shapes
in both aspects, repeated/minimized/resized surfaces and existing bounds checks.
This verifies shape/sampling/memory lifetime, not PS2 field fidelity.

## Owner priorities and limits

Confirmed owner report: extremely unstable frametime. Private screenshot
owner-frametime-2026-10-13.png SHA256df0d9582e787aef7e3861eb9a37a22f383ac587bc2fafdaa0402f05567d2e95f
shows an oscillating overlay graph,42FPS and5.4ms. These overlay values are
not independently calibrated measures of simulation speed, nor a causal diagnosis.
Measure actual present intervals/median/p95/p99/spikes and distinguish guest
work, GPU submission/fences/presentation, audio waits and capture/overlay overhead.

Owner supplied two further reports: real race19FPS/57.3ms and car-selection3D
11FPS/93.3ms. Private images owner-race-performance-2026-10-13.png
SHA25648b3d957e06a9d66f59ffaef0c85bd75772c2ef60104d28ff5f1a26d21c7429f
and owner-car-performance-2026-10-13.png
SHA2564a431591ab0ea0e003372662f8d7f016a5033cd8f450f67b1fccad15d7d5ca45.
Owner explicitly requests recording only: do not pursue performance audit or
optimization in this slice. The apparent association with3D is an owner
observation; no isolated causal measurement establishes the bottleneck yet.

Owner now prioritizes progressive-only rendering, native internal1920x1080,
2560x1440 and3840x2160; initial480p base; launcher configuration before boot;
and rendering at144FPS or monitor refresh with100% game speed independent of
physics. Current presentation size is not internal-HD/4K rendering. Default
480p race does not remove the original interlaced menus or qualify field fidelity.

Confirmed current code: guest video event cadence is NTSC60Hz; window presents
inside on_vblank; compute submissions wait fences, swapchain uses FIFO and
presentation completion fences. Donor has render queues/post_present that the
pilot does not activate. High confidence these are useful places to measure;
actual bottleneck attribution and100% realtime remain unknown. Increasing
guestVBlank to144 changes PS2 events; repeated frames at144Hz do not constitute
144 new gameplay images. Identify physics/camera/animation/HUD/audio/input
dependencies, then separate simulation and rendering with measured invariants.
Do not adopt a guessed clock/physics policy to satisfy an FPS counter.

Full temporal deinterlacing, Vulkan validation-layer coverage, known CPU/GPU
pixel divergence, inheritedCFC2(28), performance/audio starvation, physical
controller qualification, finished races and broader saves remain open.
