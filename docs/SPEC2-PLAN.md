# Spec II support — implementation plan (TASK-100, release 1.4.0)

Owner request 2026-10-22. Scope: [decision 0072](decisions/0072-spec2-second-target.md) — Gran Turismo 4 Spec II
v1.10 (SCUS-97436, GT4 Online Public Beta lineage) as a **second target** with its own executable, not a content
merge into v2.00. Background measurements: [SPEC2-STUDY](SPEC2-STUDY.md). Branch `task/100-spec2`.

The work is split into subtasks S1..S9. Each one is a commit series with its own checks and an entry in this file's
"Progress" section. Subtasks marked *independent* can be done in any order; the others follow the arrows.

## Inputs

| Input | Where | Status |
|---|---|---|
| Spec II v1.10 ISO | `C:\Antigravity\gt4-staticrecomp\Gran Turismo 4 Spec II v1.10.iso` (owner's, ignored) | available; SHA-256 `6a3484b9...04b2` |
| GT4 Online Public Beta ISO (unmodified SCUS-97436) | to be supplied by the owner | **wanted** (see S1, S7a) |
| v2.00 retail ISO and pins | as today | available |

Why the unmodified Public Beta helps: (1) a byte diff of `SCUS_974.36` against the beta ELF gives the exact static
patch set of Spec II (today only ~27 addresses and 5 `jal` redirections are known from a scan); (2) the beta is a
cleaner first target (S7a): it proves the second-target pipeline without the plugin injector. Nothing is blocked
without it: S1 starts from the payload itself, and S7 can go straight to Spec II.

## Subtasks

### S1 — Patch set and plugin injector map (study; first)
Goal: the complete list of every change Spec II makes to game code, static (in the ELF file) and dynamic (written at
boot by the injector), with the plugin's own code and data, so S2 can build a derived ELF.
- Disassemble the `.plugin_note` payload (RWX segment at VA `0x68A480`, 0x2CCC bytes) and the call at `0x1001E8`.
  Map its patch tables: target addresses, replaced words, `jal`/`j` redirections into the payload, prologue rewrites,
  data writes (physics words), and any variant chosen by arguments or settings (each variant listed explicitly).
- With the beta ISO: byte diff `SCUS_974.36` (Spec II) against the beta ELF, section by section.
- Optional cross-check: a guest RAM snapshot taken after boot in PCSX2 (allowed for this task; it is not the GS clean
  room of TASK-027) compared with the ELF image, to catch any boot-time write the static map missed.
- Output: `docs/SPEC2-PATCHES.md` (every patch: address, old word, new word, purpose if known, source: static /
  injector / both, confidence label) and a machine-readable table `docs/inputs/spec2-patches.json`.

### S2 — Derived ELF (after S1)
Goal: a build-time ELF with every patch applied, as the existing `aspect` EE profile does for v2.00
(`scripts/aspect_elf_patch.py` is the precedent).
- `scripts/spec2_elf_patch.py`: reads `SCUS_974.36` from the ISO (plain ELF, no `CORE.GT4`/Salsa step), applies
  `spec2-patches.json`, keeps the plugin segment as translatable code, and writes `private/sliceNNN-spec2-elf/`.
- The injector call at `0x1001E8` is neutralised in the derived ELF only when S1 shows that its whole effect is
  covered by the applied patches; otherwise its runtime writes are reproduced natively. Either way, documented.
- Pins: `docs/inputs/spec2-iso.json` (ISO identity), `docs/inputs/spec2-elf.json` (original and derived ELF hashes).
- Tests: Python fixtures for the patcher (every patch applied once, the inverse restores the input, refuses an
  unknown ISO).

### S3 — Target-aware host hooks (*independent*; can start now, on v2.00 only)
Goal: no v2.00 address is written or read in another target. Today the VBlank hooks write fixed v2.00 addresses:
16:9 hold (TASK-079, `native/race_widescreen.*`), display aspect / race camera / HUD canvas (TASK-080,
`native/display_aspect.*`, `scripts/display_aspect_overlay.inc`, `scripts/aspect_elf_patch.py`), race display 480p
(`native/race_display.*`, `0x009A86C0`), license demo guard (`native/license_guard.*`,
`scripts/prepare_license_guard.py`), and any other fixed guest address in `native/` and the overlay capsules.
- Introduce one per-target table (`native/game_target.hpp`: target id, and for each hook its guest addresses or
  "not available"). v2.00 fills it with today's values; a target without an entry disables that hook and logs once.
- v2.00 must stay byte-identical: headless frame identity (menus, day race, night race) and GS stream hash against
  the current release build (slice268), CTest, Python suite.
- This is the only subtask that touches the shipped v2.00 code path; it lands on its own commit series and is
  reviewed before anything else merges.

### S4 — Second-target build (after S2; S3 for running)
Goal: `SCUS_974.36` (derived) through the donor translator into its own pinned EE library and its own
`gt4vulkan` executable.
- CMake option for the target (for example `-DGT4_TARGET=spec2`), separate build and library dirs, separate
  manifests in `docs/inputs/` (never edit an existing pin). The generated units use one global symbol set
  (`gen::unit_XXXX`, `gen::kUnits`), so the two targets are two executables (decision 0072).
- `scripts/ee_producer.py` input for a plain ELF; entry ledger with the generic sources only (`fallthrough`,
  `dataword`, `luipair`); retail decomp names and the seeded entry list do not apply.
- The plugin segment overlays a zero-filled part of the data image: decide and document the translator policy
  (translate it as code from the derived ELF).

### S5 — VU0 and VU1 (*independent* once the ELF is readable)
- VU1: confirm the 0x3FF0-byte blob at file offset `0x59C110` is byte-identical to v2.00 (decrypted SHA-256 prefix
  `ae6b6cb3b4406ff3`); the content-addressed VU1 AOT table then applies unchanged. Add a fixture.
- VU0: the upload is 0x920 bytes (v2.00: 0xA80), 1,050 of 2,336 bytes differ. Generate a Spec II VU0 AOT table with
  the existing tools (`tools/vu_recomp`, the slice160 VU0 flow), or use the counted interpreter fallback first.

### S6 — IOP modules (*independent*)
- The ISO adds network modules (`INET`, `NETCNF`, `DEV9`, `SMAP`, `RTINETB`, `INETCTL`, `PDINETCNF`, `PPP`, `PPPOE`,
  `LGAUD`, `DNAS310.IMG`, `NET/`). Map them in `docs/IOP-MAP.md` style; decide per module: HLE stub that reports "no
  network device", interpreted as today, or not loaded. Goal: offline play without hangs.

### S7 — First boot (after S2, S3, S4, S5, S6)
- S7a (optional, with the beta ISO): boot the unmodified Public Beta headless to its first menus.
- S7b: boot Spec II headless; hunt every "no translated code" halt (coverage runs, ledger), then menus, then a race.
  Frame dumps reviewed by eye; no window launched by agents.

### S8 — Re-target the host hooks for Spec II (after S7)
- Find the Spec II equivalents of the 16:9 hold, 480p race display, aspect/HUD hooks and the license demo guard, and
  fill the S3 table for the target. Each hook with its fixture and a headless check.

### S9 — Product (after S7, before release 1.4.0)
- Launcher: `gt4l-disc` accepts SCUS-97436 by its pinned identity (today `Unsupported::SpecII`), the prepared layout
  without `CORE.GT4`, a second game executable in the installer, a separate save folder, the second disc copy, a game
  chooser when both discs are set up.
- Credit the Spec II authors and ask them for permission before any public release (decision 0072).

## Rules for every subtask
- AGENTS.md applies: one variable per build, fresh `private/sliceNNN-*` dirs, new pins only, CTest and Python
  fixtures for new behaviour, Confirmed / High confidence / Hypothesis / Unknown labels, STATUS and the journal kept
  current, TASKS.md updated.
- Never distribute or commit game data (ISOs, ELFs, extracted files, RAM dumps).
- No JIT: injected code is translated ahead of time like the rest of the ELF.
- C: is short of space: big build trees and run outputs go to `O:\gt4recomp-scratch\spec2-*`.

## Progress
(Each subtask appends: date, what was done, checks, commits, open points.)
