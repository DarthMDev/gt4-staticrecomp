# Contributing to GT4Recomp

Thank you for wanting to help. GT4Recomp is a static recompilation of Gran Turismo 4 (NTSC-U, SCUS-97328) for
Windows and Vulkan. Contributions are welcome in code, testing, documentation and research. This page explains the
rules every contribution follows and how to open a pull request that can be reviewed quickly.

If you would rather support the project another way: tokens are expensive, and they are what make this project
possible. You can donate on [Ko-fi](https://ko-fi.com/moesuito).

## Hard rules

These protect the project legally and technically. A pull request that breaks one of them cannot be merged.

1. **No game data, ever.** Never commit disc images, BIOS files, `CORE.GT4`, the ELF or anything extracted from it,
   translated game code (`unit_*.cpp`, `ps2game.lib`), memory cards, saves, replays, GS captures, frame dumps of the
   game, keys or binaries. They stay in ignored folders (`private/`, `generated/`, `build/`).
2. **MIT only in this repository.** Your contribution is licensed under the [MIT licence](LICENSE) of the project.
   Do not add GPL or other copyleft code, and never link GPL code into the game executable or the launcher. The PCSX2
   GS ("GSdx", GPL-3.0-or-later) lives only in the separate `gt4-gs.exe`, built from
   [`moesuito/gt4recomp-gs`](https://github.com/moesuito/gt4recomp-gs); changes to it go to that repository.
3. **Only code you may share.** Third-party patches, cheats or repositories without a licence (for example PCSX2
   `.pnach` files) may inspire ideas, but their content is never copied.
4. **No JIT.** Guest code (EE, VU, IOP) is compiled ahead of time. Do not add code that generates machine code while
   the game runs. GPU driver shader compilation is fine.
5. **Clean room for CoreGS 2.0.** CoreGS 2.0 (`native/hw_gs/`, branches `task/027-*`) is our own MIT renderer, written
   only from the specification in `docs/hw-renderer-spec/`. If you have read the PCSX2 sources, the `gt4recomp-gs` fork
   or ARMSX2, you must not contribute code to CoreGS 2.0; you can still help everywhere else. State your status in the
   pull request (see the template).
6. **Stay in scope.** This repository is the original game running natively, optimised and as faithful as possible.
   A decompilation-based source port, changes to the game's physics or timing, and remaster features belong to a
   separate project. Optional enhancements (resolution, aspect ratios, texture packs, renderers) are welcome; the
   faithful picture stays the default.

## Before you start

- Look at [TASKS.md](TASKS.md) and the [open issues](https://github.com/moesuito/gt4-staticrecomp/issues). Every piece
  of work is a numbered task (`TASK-NNN`).
- For anything bigger than a small fix, open an issue first (or comment on the existing one) and describe what you
  plan. The maintainer confirms the task number and whether it fits the roadmap, so your time is not wasted.
- Read [AGENTS.md](AGENTS.md): it is the working guide for everyone, human or AI, and lists the build commands, the
  architecture and the testing rules. [docs/BUILD.md](docs/BUILD.md) has the build recipes.
- AI-assisted contributions are welcome. Say so in the pull request, and make sure the agent followed AGENTS.md. You
  are responsible for every line you submit.

## Building and testing

- **Launcher** (Rust, Tauri 2): from `launcher/`, run `cargo test --workspace`. Set `GT4_SDL3_ROOT` to an SDL3 3.4.18
  folder that holds `devel/` and `runtime/`.
- **Native game code** (C++20, clang-cl, CMake, Ninja, Vulkan): the full game build needs a pinned private library
  translated from your own copy of the game, so a clean clone cannot build the game executable. Build what you can
  (tools, libraries, CTest fixtures) and say in the pull request what you could not run.
- **Python tooling and checks:** `python -m unittest discover -s tests/python`, with `GT4_NATIVE_BUILD` pointing at
  the build under test when you have one.
- **New behaviour needs automated checks** (cargo tests, CTest fixtures or Python tests). Never weaken or delete an
  existing check to make a test pass.
- In-game testing is done by the maintainer before a merge. If you play-tested your change, describe the scenes
  (menus, race, night, rally, replays, photo mode) and your GPU and driver.

## Code and documentation style

- Write code that reads like the code around it: descriptive names, straightforward flow, explicit guest addresses,
  fixed-width types and defined wrapping. No host undefined behaviour standing in for PS2 semantics.
- Never silently accept an unsupported instruction, service or setting; fail loudly and count fallbacks.
- When you state a finding about the game or the hardware, label it **Confirmed**, **High confidence**, **Hypothesis**
  or **Unknown**, and give addresses, hashes and the method.
- Repository text is in **English (en-US)**: code comments, docs, commit messages and pull requests.
- Update the documentation in the same pull request:
  - [TASKS.md](TASKS.md): the task's row and section (state, result, date);
  - [docs/STATUS.md](docs/STATUS.md) when the state of the project changes;
  - the daily journal `docs/journal/YYYY-MM-DD.md`;
  - [README.md](README.md) only when a badge, a headline result or a known issue changes.
- `TASKS.md` and `README.md` use CRLF line endings; keep them that way.

## Branches, commits and pull requests

- Fork the repository and create a branch from `main` named `task/NNN-short-name` (or `fix/short-name` for a small
  fix without a task).
- Keep a pull request to one topic. Smaller pull requests are reviewed faster.
- Write clear commit messages: a short summary line, then what changed and why.
- Do not rewrite history that others already pulled. Pull requests are merged with a merge commit, so your commits
  and authorship are kept.
- The maintainer reviews, tests in game and decides. A pull request may be merged as it is, merged into a release
  branch with follow-up changes, or closed with an explanation. Contributors are credited in the release notes.

## Pull request template

GitHub fills this in when you open a pull request ([.github/pull_request_template.md](.github/pull_request_template.md)).
If it does not, copy it into the description and fill in every section.

```markdown
## Summary
<!-- What does this change do, in two or three sentences? -->

## Task
<!-- TASK-NNN and/or issue link. "None" for a small fix. -->

## Why
<!-- The problem it solves or the improvement it brings. Link evidence (logs, measurements, screenshots). -->

## Changes
- <!-- File or area: what changed -->

## Testing
- [ ] `cargo test --workspace` (launcher)
- [ ] CTest on a native build (say which)
- [ ] Python suite (`python -m unittest discover -s tests/python`)
- [ ] Played in game (scenes, GPU, driver):
- Not run, and why:

## Checklist
- [ ] No game data, binaries, saves, captures or keys are included
- [ ] My code is MIT-compatible; no GPL code and nothing copied from unlicensed sources
- [ ] No JIT or runtime code generation
- [ ] TASKS.md, STATUS and the journal are updated where needed
- [ ] Claims about the game are labelled (Confirmed / High confidence / Hypothesis)

## Clean-room status (CoreGS 2.0)
- [ ] I have NOT read the PCSX2 sources, the gt4recomp-gs fork or ARMSX2 (required for changes to `native/hw_gs/`)
- [ ] I have read them, and this pull request does not touch CoreGS 2.0

## AI assistance
<!-- None, or which tools/agents you used and for what. -->
```

## Reporting bugs

Open an issue with:

- the GT4Recomp and GSdx versions (Settings > About);
- your GPU, driver version and Windows version;
- the renderer (GSdx or CoreGS 1.1) and the graphics settings you changed;
- the exact steps, what you expected and what happened;
- the launcher log folder (Settings > About > Open log folder), attached as a zip. Do not attach saves, memory cards
  or anything from your disc.

## Conduct

Be respectful and patient. Critique code, not people. Harassment, discrimination or sharing pirated game data leads to
removal from the project.
