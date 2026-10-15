# Persistent project instructions

## Read first

Read `docs/STATUS.md` (current state at the top), then `docs/OPTIMIZATION-PLAN.md`
(active plan and backlog), `README.md`, `docs/ARCHITECTURE.md` and `docs/BUILD.md`
as needed. The latest daily journal is `docs/journal/YYYY-MM-DD.md`. Earlier
slice-by-slice history lives in STATUS (History section) and the journals.
Current implementation lives in `native/`, `tools/` and `scripts/`.

The old EE recompiler/runtime is archived on
`codex/archive-original-recomp-20261013` (commit
`95f615519b992c65505d77da35af825fc6a13a53`); do not restore it without a
documented technical reason. See `docs/LEGACY.md`.

## Mission and direction (owner, 2026-10-15)

GT4Recomp targets Gran Turismo 4 NTSC-U (SCUS-97328, disc labeled v2.00; its
CORE.GT4 is byte-identical to the gt4-decomp target) on Windows x86-64 first,
then Android and iOS. **No JIT ever**: guest code (EE, VU, IOP) is compiled ahead
of time per target; interpreters may remain only as explicit, counted fallbacks.
GPU driver shader compilation is allowed.

- **Phase 1 (current):** a fast, stable recompiled port: 60 FPS at 100% game
  speed, smooth frametime, no interpreter on the normal path.
- **Phase 2 (later):** apply the gt4-decomp decompilation function by function
  behind a guest ABI/memory bridge so the game reads like source; then a native
  renderer, physics decoupled from frame rate, ultrawide, remaster features.
- Graphics: two launcher-selectable renderers: **Vulkan (GS)** = current accurate
  compute GS emulation (to gain internal scaling and options) and **Vulkan
  (Custom)** = PCSX2-based hardware renderer specialized for GT4 (mobile, weaker
  PCs, upscaling, dithering control, deinterlacing, texture packs).
- GPL-3.0 reuse (e.g. PCSX2 GS) is accepted (decision 0066): pin sources, keep
  notices, record provenance in `native/THIRD_PARTY.md`. Never import PCSX2 JITs.
- Future ideas recorded in docs (not scheduled): launcher (Rust + egui/GPUI),
  racing wheels with force feedback, LAN/online launcher, in-game settings
  menus, Spec II as a second target or data source.

Working fork: github.com/moesuito/gt4-staticrecomp. Upstream
github.com/Kaezzey/gt4-staticrecomp is read-only.

## Current base

MIT ps2-web-recomp donor at `1b4808793a12c45125262fd2d5f50a9ec9f7c020`
(`private/references/ps2-web-recomp`, frozen; change donor behavior only through
pinned overlay capsules such as `scripts/prepare_perf_runtime.py`).

- EE: translated AOT to C++ (private pinned `ps2game.lib`); VU0 macro inline.
- VU1 microcode: compiled AOT by `tools/vu_recomp` (derived from the donor
  emitter); the whole image is decrypted from the ELF (docs/VU1-MICROCODE-SOURCE.md).
- VU0 micro, IOP game modules: interpreted for now (mapped in
  docs/VU0-MICROCODE-MAP.md, docs/IOP-MAP.md).
- GS: donor compute rasterizer on Vulkan with asynchronous submission; frames
  paced by one high-resolution clock with rate-controlled audio.
- Never claim zero emulation or full PS2 fidelity.

Toolchain: clang-cl (C++20), VS2022 x64 SDK, Ninja, CMake 3.24+. This machine:
Ryzen 7 5700X, RX 9060 XT (AMD Vulkan driver), Windows 11.

## Workflow

- Speak Brazilian Portuguese with the owner, in plain lay language; keep all
  repository docs, reports, prompts and code comments in en-US.
- The main agent orchestrates, reviews and tests; implementation and research go
  to subagents (Haiku by default; escalate to Opus or do it directly when a task
  stalls). Research/studies run in parallel and read-only; **exactly one
  repository writer at a time**.
- Review every subagent claim: rerun CTest/Python and check pins before commit.
- One variable per candidate build; keep A/B switches (`GT4_PACING=audio`,
  `GT4_GS_SYNC=1`, `--vu1-interpret-only`, `--vu1-verify N`).
- Commits, branches and pushes are authorized; never rewrite history. Active
  branch `codex/optimization-plan-151`; fast-forward `main` when green.
- Ask the owner only for decisions and gameplay tests, not code review.

## Testing the game

- While the owner is awake, the owner plays and reports (F7 starts an interval,
  F8 reports to the session `probe.log`, one interval per scene).
- When left unattended, the agent may launch candidates and drive the game via
  Computer Use. Keys: arrows = D-pad, Z = Cross/accelerate, A = Square/brake,
  S = Triangle, X = Circle, Enter = Start.
- Reference scene: Time Trial, Nordschleife, Toyota Supra Wood One, in-game
  video 480p (game boots 480i; menus stay interlaced).
- Launch through `scripts/play_performance.py --assets-root <repo> --mode <mode>`
  (current: `pacing`). Preserve the owner's card, backups and locks; never
  interfere with an owner session; close the game normally.

## Code and evidence

All code and tooling must be human readable: descriptive names, straightforward
flow, explicit guest addresses and fixed-width state, defined wrapping, no host
undefined behavior standing in for guest semantics. New behavior needs automated
controls in CTest and Python fixtures. Never silently accept unsupported
instructions or services. Do not weaken acceptance checks to make tests pass.

Label claims Confirmed / High confidence / Hypothesis / Unknown. Record hashes,
tool versions, addresses/offsets, method and limits.

## Data, builds and documentation

- ISO/BIOS/CORE/ELF, translated code, cards, captures, outputs and binaries stay
  ignored under `private/`, `generated/`, `build/` or ignored ISO paths. Never stage.
- Verify inputs against `docs/inputs/`; verification never updates pins.
- New candidates use fresh `private/sliceNNN-*` directories and new manifests;
  never overwrite earlier slice outputs.
- Build/test serially (`cmd /c` with VsDevCmd; see BUILD.md for the current
  candidate recipe). CTest per build dir; Python:
  `GT4_NATIVE_BUILD=<build dir> private/tooling-venv/Scripts/python.exe -m unittest discover -s tests/python`.
- The build still needs the pinned private `ps2game.lib`; it is not an
  end-to-end clean-checkout build.
- Documentation is part of completion: update STATUS when state changes, append
  the daily journal, add numbered decisions as useful, keep README progress and
  known issues current. Before ending: `git status --short`, commit and push.
