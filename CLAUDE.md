# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Read first

The ordered task list is `TASKS.md`. Current state is at the top of
`docs/STATUS.md`; technical background for the optimization work is in
`docs/OPTIMIZATION-PLAN.md`; daily progress goes to
`docs/journal/YYYY-MM-DD.md`. `README.md`, `docs/ARCHITECTURE.md` and
`docs/BUILD.md` give the public overview, architecture and build recipes per
slice. Older slice-by-slice history lives in STATUS (History section) and the
journals. The old EE recompiler is archived on `codex/archive-original-recomp-20261013`
(commit `95f615519b992c65505d77da35af825fc6a13a53`, see `docs/LEGACY.md`); do not
restore it without a documented technical reason.

## Mission and direction (owner, 2026-10-15)

GT4Recomp is a static recompilation of Gran Turismo 4 NTSC-U (SCUS-97328, disc
labeled v2.00; its CORE.GT4 is byte-identical to the gt4-decomp target) for
Windows x86-64 + Vulkan first, then Android (phase 2) and iOS (phase 3). **No JIT ever**: guest code
(EE, VU, IOP) is compiled ahead of time per target; interpreters may remain only
as explicit, counted fallbacks. GPU driver shader compilation is allowed. Never
claim zero emulation or full PS2 fidelity.

- **Phase 1 (current):** fast, stable recompiled port: 60 FPS at 100% game speed,
  smooth frametime, no interpreter on the normal path.
- **Out of scope here:** a decompilation-based source port, a native (non-GS)
  renderer, physics decoupled from frame rate, ultrawide and remaster features
  will be a separate project, possibly a fork. This repository's goal is the
  original game running natively, optimized and as faithful as possible.
- **Renderer focus now:** integrate paraLLEl-GS (TASK-012).
- **Phases:** Phase 1 = Windows PC (current), Phase 2 = Android, Phase 3 = iOS.
- **Graphics:** PC renderer = **paraLLEl-GS** (Vulkan compute, LGPL-3.0+); the
  current donor compute GS renderer is an experimental fallback/reference only
  (no new features). Mobile = **ARMSX2 GS pipeline** (Vulkan on Android, Metal on
  iOS) without ARMSX2's JIT. See `docs/GS-RENDERER-OPTIONS.md`.
- GPL-3.0 reuse (e.g. PCSX2 GS) is accepted (decision 0066): pin sources, keep
  notices, record provenance in `native/THIRD_PARTY.md`. Never import PCSX2 JITs.
- The launcher (TASK-020) and racing wheels (TASK-019) are planned tasks;
  LAN/online launcher, in-game settings menus and Spec II are recorded ideas.

Working fork: github.com/moesuito/gt4-staticrecomp. Upstream
github.com/Kaezzey/gt4-staticrecomp is read-only.

## Workflow

- Speak Brazilian Portuguese with the owner, in plain lay language. Keep all
  repository docs, reports, prompts and code comments in en-US.
- The main agent orchestrates, reviews and tests; implementation and research go
  to subagents (Haiku by default; escalate to Opus or do it directly when a task
  stalls). Research runs in parallel and read-only; **exactly one repository
  writer at a time**. Review every subagent claim: rerun CTest/Python and check
  pins before committing.
- One variable per candidate build; keep A/B switches (`GT4_PACING=audio`,
  `GT4_GS_SYNC=1`, `--vu1-interpret-only`, `--vu1-verify N`). Attack whatever the
  F8 report shows as the largest cost.
- Work is organized in `TASKS.md` (TASK-001, TASK-002, ...). Keep it current:
  states, branch, acceptance, and new tasks appended in order.
- **Task lifecycle:** each task gets its own branch from `main`
  (`task/NNN-short-name`) → implementation by the single writer → automated
  checks (CTest, Python, headless frame comparison, verify modes) → in-game test
  by the owner → **explicit owner approval** → PR into `main` and merge
  (`gh pr create` / `gh pr merge --merge`) → next task on a new branch. Never
  merge a task the owner has not approved. Commits and pushes are authorized;
  never rewrite history.
- Update README (progress, scene results, known issues), STATUS and the journal
  as part of each task.
- Ask the owner only for decisions and gameplay tests, not code review.

## Testing the game

- While the owner is awake, the owner plays and reports: F7 starts an interval,
  F8 writes the report to `private/manual-play/session-*/probe.log` (one interval
  per scene).
- When left unattended, the agent may launch candidates and drive the game via
  Computer Use. Keys: arrows = D-pad, Z = Cross/accelerate, A = Square/brake,
  S = Triangle, X = Circle, Enter = Start.
- Reference scene: Time Trial, Nordschleife, Toyota Supra Wood One, in-game video
  480p (the game boots 480i; menus stay interlaced).
- Preserve the owner's card (`private/manual-play/card.ps2`), backups and locks;
  never launch while an owner session is running; close the game normally.

## Commands

All native work runs from the repo root in a VS x64 environment (`cmd /c` with
`"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64`;
see `native/build.cmd`). Toolchain: clang-cl (C++20), Ninja, CMake 3.24+. Run
builds and tests serially.

- Configure a candidate build (current recipe = slice 153; every new candidate
  uses fresh `private/sliceNNN-*` dirs and new `docs/inputs/*.json` pins):
  ```
  cmake -S . -B private\slice153-pacing-build\build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" ^
    -DGT4_EE_PROFILE=production -DGT4_ASSETS_ROOT=C:/Antigravity/gt4-staticrecomp ^
    -DGT4_PYTHON=private/tooling-venv/Scripts/python.exe ^
    -DGT4_PERF_RUNTIME_SOURCE=private/slice151-vu1-aot-runtime ^
    -DGT4_VU1_GENERATED_DIR=private/slice151-vu1-gen-smoke ^
    -DGT4_PORT_DIR=private/slice153-pacing-port -DGT4_SHADER_DIR=private/slice153-pacing-shaders
  ```
  CMake refuses port/shader/runtime combinations that do not match the pins.
- Rebuild an existing dir: `cmake --build private\slice153-pacing-build\build`
- Baseline (slice 114) build: `cmd /c native\build.cmd`
- Tests: `ctest --test-dir <build dir> --output-on-failure`; one test:
  `ctest --test-dir <build dir> -R native_pacing --output-on-failure`
- Python suite (must point at the build under test, otherwise it checks the stale
  slice 114 binaries and two tests fail):
  `GT4_NATIVE_BUILD=<build dir> private/tooling-venv/Scripts/python.exe -m unittest discover -s tests/python`;
  one module: `... -m unittest discover -s tests/python -p test_pacing_launcher.py`
- Launch: `private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode pacing`
  (modes map to pinned manifests: `vu1-aot`, `gs-async`, `pacing`, `vu1-record`;
  options `--vu1-verify N`, `--vu1-interpret-only`; env `GT4_PACING=audio`,
  `GT4_GS_SYNC=1`, `GT4_PRESENT_MODE=fifo|mailbox|immediate`).
- VU1 emitter: `gt4_vu_recomp <corpus dir> <fresh out dir>` (built from
  `tools/vu_recomp`; refuses non-empty output dirs).

## Architecture (big picture)

- **EE pipeline:** ISO → `CORE.GT4` → reconstructed ELF
  (`private/reconstructed/SCUS_973.28.elf`) → donor translator (`tools/ee_producer`,
  `scripts/ee_producer.py`) → 326 C++ units (`unit_XXXX.cpp`, one per 16 KiB
  address window, `switch(entry)` to labels; VU0 macro ops inline) → pinned private
  `ps2game.lib`. A call to an address without a translated entry halts ("no
  translated code"); there is no EE interpreter. The build therefore needs the
  pinned private library and is not an end-to-end clean-checkout build.
- **Donor runtime:** MIT ps2-web-recomp at `1b4808793a12c45125262fd2d5f50a9ec9f7c020`
  (`private/references/ps2-web-recomp`), frozen and hash-checked. Behavior changes
  go through overlay capsules generated by `scripts/prepare_perf_runtime.py`
  (e.g. `--vu1-aot` applies `scripts/vu1_aot_overlay.inc` to `vu_micro.cpp`) and
  selected by `GT4_PERF_RUNTIME_SOURCE`. IOP: system libraries are native HLE, the
  23 game IRX modules are interpreted (`docs/IOP-MAP.md`); VU0 micro is
  interpreted (`docs/VU0-MICROCODE-MAP.md`).
- **Native adapter** (`native/`): the donor GS backend `gpu.cpp` is spliced with
  `native/gpu_*.inc` by `scripts/prepare_external_vulkan.py` into a port dir;
  `native_vulkan.cpp` (Vulkan context, 3-slot async submit ring, present),
  `window.cpp` (Win32, VBlank hook, F-keys), `host_io.cpp` (WinMM audio, XInput,
  keyboard), `pacing.cpp`/`frame_clock.cpp` (single-clock pacing, audio rate
  control), `perf_probe.*`/`perf_accounting.hpp` (F7/F8 categories). Shaders are
  the donor WGSL compiled to SPIR-V by `tools/wgsl_spirv` (naga).
- **VU1 AOT:** the whole VU1 microcode image is XOR-encrypted in the ELF (file
  offset 0x525DD0, `docs/VU1-MICROCODE-SOURCE.md`); `tools/vu_recomp` emits C++ per
  (key, entry) with identity digests, a budget/resume contract
  (`vu_recomp_contract.hpp`) and differential tests against the donor interpreter.
  Unseen entries fall back to the interpreter and are counted in F8.
- **Launchers and pins:** `scripts/play_performance.py` → `play_external_runtime.py`
  → `external_runtime_probe.py`, which verifies every file against
  `docs/inputs/*.json`, scrubs inherited `PS2_*`/`GT4_*` variables, backs up and
  locks the card, and records the run in `result.json`.
- **Research references** (private clones): gt4-decomp with names/types at
  identical addresses (`private/slice151-research/gt4-decomp`), Nenkai repos,
  PCSX2 (`private/tooling/pcsx2-slice101-source`).

## Code and evidence

All code and tooling must be human readable: descriptive names, straightforward
flow, explicit guest addresses and fixed-width state, defined wrapping, no host
undefined behavior standing in for guest semantics. New behavior needs automated
controls in CTest and Python fixtures. Never silently accept unsupported
instructions or services; never weaken acceptance checks to make tests pass.
Label claims Confirmed / High confidence / Hypothesis / Unknown and record hashes,
tool versions, addresses/offsets, method and limits.

## Data, pins and documentation

- ISO/BIOS/CORE/ELF, translated code, cards, captures, outputs and binaries stay
  ignored under `private/`, `generated/`, `build/` or ignored ISO paths. Never stage.
- Verify inputs against `docs/inputs/`; verification never updates pins. Never
  overwrite an existing `private/slice*` directory or edit an existing manifest
  to make a hash match; create a new slice.
- After a rebuild the executable hash changes; the launcher manifest pin must be
  regenerated (Python pin tests enforce it).
- This machine (Ryzen 7 5700X, RX 9060 XT) has an AMD driver reporting
  `maxComputeWorkGroupCount[0] = UINT32_MAX`; fixtures must derive limits from the device.
- Documentation is part of completion: update STATUS when state changes, append
  the daily journal, add numbered decisions (`docs/decisions/`) as useful, keep
  README progress and known issues current. Before ending: `git status --short`,
  commit and push.
