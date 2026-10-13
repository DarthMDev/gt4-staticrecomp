# Slice114 — native base adoption and legacy cleanup

2026-10-13, baseline `95f615519b992c65505d77da35af825fc6a13a53`,
decision0056. Owner request: preserve state on an archive branch, return to
main workflow, clean up the original engine, and update project vision.

## Confirmed: preservation and dependencies

Branch `origin/codex/archive-original-recomp-20261013` published prior to any
removal, pointing to baseline. `origin/main` was synchronized via fast-forward
(already up to date); work isolated on `codex/cleanup-native-base-114`.
Private plan lists 411 removed files out of 466 versioned: docs 294,
include 17, src 20, tests 48, scripts 12, tools 16, and 4 root feedback/plan/handoff files.
Each target was verified as a file inside the workspace. No private,
generated, ISO, legacy build, memory card, or capture files were removed. Git history preserved.

Separate read-only audit verified transitive dependencies. Launcher still
uses gt4disc/fingerprint, pycdlib, and input/seed manifests; these were retained.
No active dependency imports or compiles src/include/tools-gt4/Ghidra scripts/
PCSX2/legacy engine tests. Native 14 source files moved from `experimental/vulkan`
to `native`; root CMake now only contains runtime/host/fixtures for this active base.

## Confirmed: definition of native

EE AOT: 326 registered C++ compilation units compiled to x86-64; native dispatcher
rejects missing code. IOP interprets R3000 and uses HLE. VU runtime supports
registered compiled programs, but the currently generated set registers no
VU AOT; VU JIT is disabled outside Emscripten, so VU on Windows is interpreted.
Hardware and peripheral devices remain software models. Vulkan/Win32/WinMM/
XInput are native host integrations. No browser, WebAssembly, or embedded PCSX2.
`ARCHITECTURE.md` documents paths and lines from code review; this is not a performance measurement.

## Build and controls

New directory `private/slice114-native-base` keeps all previous producers intact.
Build using Clang 23.1.2 / C++20 / VS2022 SDK / Ninja and Naga 30.0.0 reproduces
the 9 SPIR-V modules byte-identically. AOT library `c837d13d...6de1f` is a
prerequisite verified by CMake. Generation of the library remains private and historical,
with no guarantee of reproduction from a clean checkout.
New executable `c634a3d32c9b3c32d2b0ab9d769d0682572a2f35761e397669e044117e2dc033`.
Manifest 114 selects the new base; probe script without manifest argument
also defaults to 114, removing the obsolete ps2run/CPU default.
Its tamper/path/seed/protection/card/timeout/termination checks remain intact.
Build clean without warnings; 3/3 CTest in 11.13s includes 32 Python tests (6.439s);
final Python repetition 32/32 without skips in 5.812s after simplifying probe default.
No retained acceptance assertion was weakened. Old tests accompany the archived engine.
Headless smoke 120 terminates normally / 589983844 budget cycles in 2.081s with seed copy,
no automated input, no window, and without modifying the manual memory card.
This is a link/execution smoke test; it does not prove a new race or 100% real-time execution.

Capture frame 100 is byte-identical to the preserved capture in 113/options-nav1,
using identical input/seed. Manifest 114 freezes 60 private artifacts and 27 publication
sources, including immutable producer and generated ports. Final read-only review
found no active dependency on removed files and no blockers in architecture/documentation.

## Unknown and requested exclusions

Do not investigate or fix performance or duplicate input. Do not manipulate
the owner's manual session or card. Do not run automated tests for new races or modes.
No clock adjustments, 144FPS physics, HD/4K scaling, deinterlacing, service modifications,
or recompiler translation changes.
Human 480i/480p/1080i results remain separate from scan-field fidelity.
Legacy base fixtures remain recoverable on the archive branch; their removal does not
weaken an active acceptance assertion, but retires an entire legacy implementation
by owner directive.
