# Slice112 — Windows audio, XInput and first manual play launcher

2026-10-12/13, baseline d7f25fe, decision0054. Owner requests these facilities
now, postpones Vulkan customization, and is away without a controller connected.
This is the separate native Clang pilot, not a migration of production MSVC.

## Confirmed implementation and inputs

Frozen donor1b4808793a12c45125262fd2d5f50a9ec9f7c020 remains read-only/MIT.
SPU2 supplies32 stereo frames at48kHz per callback; stack storage is copied.
Native WinMM output owns four512-frame buffers and counts actual driver completion,
cancellation, pending frames, peak ownership, waits and empty-queue observations.
No worker/callback thread changes guest execution; host waiting pumps messages.
Focus loss clears live pad buttons/pressure/axes and stops rumble. XInput axes are
RX/RY/LX/LY; pressure indexes are PS2 button bits, not the twelve wire-packet
positions. Xbox A/B/X/Y map Cross/Circle/Square/Triangle; LT/RT map L2/R2.
Scripted input exclusively retains the donor's existing pad contract.

Input pins remain USA2.00: ISO67b6c007...e824f; CORE85d26aa8...7ff9;
ELF10f82e22...c935c. Seed profile L8650752bytes/bb10a96f...e3072 is copied,
never written in place. Frozen111 executablecc6635e4...f96 and production
gt4boot839adaa2...6dfab remain preservation gates. Full pins are in metadata.

Toolchain: Clang23.1.2, VS2022 x64 SDK10.0.26100.0, Naga30.0.0 shaders,
WinMM/XInput system libraries; NVIDIA RTX3060Laptop with same qualified Vulkan
environment as111. Reproducible build: experimental/vulkan/build.cmd; new
private/slice112-input-audio layout protects all111 producers.

## Observations to date

Confirmed: real host fixture consumes2592frames (81 fragmented32-frame batches),
wraps the ring, drains a partial buffer and reports completed2592/canceled0.
Separate pending-only and submitted-buffer cancellation controls distinguish
natural completion from reset cancellation; second close is harmless and a
closed sink refuses new PCM. Mapping controls exercise fourteen digital buttons,
reserved bits, pressure, triggers30/31/255, signed endpoints/deadzones/Y inversion,
all65536 left-axis values, keyboard merge, focus-neutral state and rumble channels.
Physical XInput slots0..3 initially return1167; no physical input/rumble claim.

Preliminary race-prefix1 completes3200frames/15728862222 budgetcycles normally
in150.959s, through actual FordKa color selection. Guest WAV is48kHz stereo16,
2553888frames; PCM SHA256cc791a8319e9c97378a47262ce83ebdab72b3590cb9015d827e4dbb98077e7b5.
Left/right RMS4099.86/3895.61, nonzero2111921/2111952, no clipping at full16bit.
Windows received/submitted/completed2553888 each, canceled0/pending0, peak4;
1200empty-queue observations indicate output starvation. This proves synthesis and
device consumption, not continuous/acoustically heard/PS2-matched sound.
The preliminary producer predates cleanup fixes and is not final acceptance.

Race-final1 (frozen producer96fc90ef...c67, source-final capsule) reaches actual
Nürburgring rolling introduction: FordKa, AI cars, position6/lap1/2 HUD, track map.
Last capture4800; stops after218.355s with exit1 and Invalid GS Vulkan dispatch.
No successful6000-frame or playable-race claim from that run. Received3835488,
completed3834368/canceled1024/pending96; cleanup owns every submitted buffer.
Original guard also rejected x/y zero. Specification permits zero workgroups;
synthetic real-GPU control confirms zero raster dispatch leaves sentinel VRAM
unchanged, but invalid pipeline/parameter/device counts are still rejected.
Race-fixed1 (frozen producer07404c38...a7a7/source-race-fix capsule) confirms
first zero raster dispatch pipeline5/x0/y0/parameter91, then completes6000frames
normally in297.811s/29491440110 budgetcycles. All48 common100..4800 captures
are byte-identical to the failed run. Thus the valid zero-work dispatch was the
causal blocker at this frontier; unrelated graphics differences remain open.
Actual track/AI/HUD and running timer observed (7.533s at6000); screenshots
alone do not distinguish guest autopilot introduction from manual response.
Audio received/submitted/completed4793888 each, canceled0/pending0, peak4,
3733empty-queue observations. No missing/unhandled/unknown service warning
observed by bounded log scan; inherited CFC2(28) occurs once. Selected counters
and exact-frame comparison are stored in causal-repeat.json. They do not prove
full guest-state or PS2 fidelity.

Race-controls1 uses the same pinned producer/seed and prefix, then Cross6100..6699,
Cross+Left6750..6929 and Square7000..7599. Confirmed normal9000-frame stop,
370.411s/44237040110 budgetcycles, timer57.333s at9000. All60 common100..6000
captures and the entire6000-frame PCM prefix are byte-identical to race-fixed1.
Guest audio7193888 stereo frames/48kHz, PCM SHA256
b66225d7d0cd3186c2a07e3e50b198e2509261121ba485916f897c079d4c4531;
both channels nonzero/RMS3827.12/4024.38. Windows completes every frame naturally,
no canceled/pending,3810empty-queue observations. One inherited CFC2(28) warning;
no observed missing/unhandled/unknown service warning in the same bounded scan.

High confidence: driving responds consistently with scripted direction/brake,
including wrong-way indication after turning,0mph while braking and reverse10mph
later. A collision with the barrier already occurs during straight acceleration
(6500), so this does not isolate acceleration quantitatively; steering/braking
also lack a matched no-input causal control. Confirmed are real race execution,
explicitly applied inputs and observed HUD/world progression, not a finished lap
or comprehensive handling validation. Actual keyboard/XInput driving feel remains
for owner test. Captures/commands/counters/hashes in driving-controls-analysis.json
and metadata retain these limits. No production behavior was altered to gain laps.

Production build has no work and54/54CTest passes61.70s. Python114 tests
(108 run/6 skip) pass90.437s; actual updated Vulkan and WinMM fixtures included.
No W4 warnings in final native build. Two actual manual launches each close
normally; a simultaneous second launcher is refused. Persistent card remains
8650752bytes with original seed hash after both early-close sessions. This proves
reopen preservation and close handling, not a completed race save.

## Manual use and safety

Open root Jogar-GT4.cmd. It verifies all pinned files before starting the game;
the first check reads the full ISO. The game window has actual Vulkan output,
native audio, keyboard and automatic XInput slot selection. Enter=Start,
Z=Cross/accelerate, A=Square/brake, arrows=direction, X=Circle/back,
S=Triangle, Q/W=L1/R1,1/2=L2/R2, Backspace=Select,C/V=L3/R3.
Xbox A accelerates and X brakes under GT4's default assignment; the game options
can change that assignment. LT/RT do not silently substitute for acceleration.
Close the game window to leave normally. Manual play has no scripted presses,
frame dumps, syscall trace or audio recording; a12-hour ceiling bounds abandoned
sessions. A normal window close is recorded separately from diagnostic frame
completion; early errors/timeouts never satisfy either completion gate.

Save file: private/manual-play/card.ps2, initialized once from profile L. Every
session has card-before.ps2 and before/after hashes in a new private directory.
Existing saves are never reseeded. A malformed card is preserved and refused.
Windows releases both launcher byte lock and native exclusive card lock on exit;
the child's lock remains if only its parent dies. Pinned evidence cards are
refused as mutable manual targets. Native card lock is independently exercised
with a competing Windows handle; the second game stops before loading/writing.

## Limits and next validation

Confirmed: inherited CFC2(28) donor warning and CPU/GPU pixel divergence remain;
no production semantics, clock quantum or frame tolerance was changed. Only this
GPU/audio device exercised, no installed Khronos validation layer. Unknown:
physical controller reconnect/rumble, acoustic sound, SPU2 reference fidelity,
race stability and wider saves. Runtime/renderer slowness can interrupt sound.
Owner-directed next action is functional play validation and measured blockers,
not higher resolution/effects/ultrawide customization.
