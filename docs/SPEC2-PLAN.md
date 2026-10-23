# Spec II support — implementation plan (TASK-100, release 1.4.0)

Owner request 2026-10-22. Scope: [decision 0072](decisions/0072-spec2-second-target.md) — Gran Turismo 4 Spec II
v1.10 (SCUS-97436, GT4 Online Public Beta lineage) as a **second target** with its own executable, not a content
merge into v2.00. Background measurements: [SPEC2-STUDY](SPEC2-STUDY.md). Branch `task/100-spec2`.

The work is split into subtasks S1..S9. Each one is a commit series with its own checks and an entry in this file's
"Progress" section. Subtasks marked *independent* can be done in any order; the others follow the arrows.

## Inputs

| Input | Where | Status |
|---|---|---|
| Spec II v1.10 ISO | `C:\Antigravity\gt4-staticrecomp\Gran Turismo 4 Spec II v1.10.iso` (owner's, ignored) | available; SHA-256 `6a3484b9672c10a92154ecee6faa5bbcfd4ed8cd538d8283e429c3fe6e1704b2`, 3,683,418,112 bytes |
| GT4 Online Public Beta ISO (unmodified SCUS-97436) | `C:\Antigravity\gt4-staticrecomp\Gran Turismo 4 (USA) (Beta).iso` | available (owner, 2026-10-22); 2,654,109,696 bytes, SHA-256 `1f49b9d8077801188860b62c3efdbc631ba8af7df979373811ce8c60dcc48ebe` |
| Spec II 1.10 community `.xdelta` | `C:\Antigravity\gt4-staticrecomp\Gran Turismo 4 Spec II (1.10 Full).xdelta` | cross-check done: decoding it with the beta ISO produces exactly the Spec II ISO hash above, so the beta is the confirmed base. Not used for anything else |
| v2.00 retail ISO and pins | as today | available |

Why the unmodified Public Beta helps: (1) a byte diff of `SCUS_974.36` against the beta ELF gives the exact static
patch set of Spec II; (2) the beta is a cleaner first target (S7a): it proves the second-target pipeline without the
plugin injector. Both are now unblocked: the beta's game image is rebuilt from its encrypted `CORE.GT4` (S1) and the
static patch set is measured. The beta ISO is also the base the S7a boot will use.

Note: the beta is *not* "Spec II minus the injector" at the file level. Its `SCUS_974.36` is a small loader and the
game lives in `CORE.GT4`; Spec II ships the game itself as one plain ELF. Rebuilding the beta image therefore needs
the loader's container (Salsa20/20 + CRC-32C + deflate), which `scripts/spec2_beta_elf.py` reproduces.

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

### S1 — patch set and plugin injector map: DONE (2026-10-22)

Report: [SPEC2-PATCHES](SPEC2-PATCHES.md). Pins: [spec2-patches](inputs/spec2-patches.json),
[spec2-beta](inputs/spec2-beta.json). Tools: `scripts/spec2_patches.py`, `scripts/spec2_beta_elf.py`. Tests:
`tests/python/test_spec2.py` (14 tests).

- The beta is the confirmed base of Spec II 1.10: the community `.xdelta` decodes to the owner's Spec II ISO hash.
- The beta's encrypted `CORE.GT4` is reproduced (Salsa20/20, key `PolyphonyDigital`, CRC-32C trailer, then the retail
  v2.00 container) and rebuilt into `private/slice271-spec2-beta-elf/SCUS_974.36.elf` (`2e5621ed…`, entry 0x100008).
- Static patch set: **one word** (`0x001001E8`, `jal 0x005E3AA8` -> `jal 0x0068A768`) plus the `.plugin` segment at
  `0x0068A480` (0x2CCC bytes, RWX) over 0x2CCC zero bytes of the beta data image. Every other word is identical.
- Injector map: the init at `0x0068A480` executes 356 instructions, calls `0x005E3AA8` (the function it replaced) and
  `0x004E2940`, and makes 35 unconditional word stores (18 text, 17 data). No variants: same table under stack and
  external-return perturbation; no settings or arguments are read.
- Open points: the `0x006605F4` flag word has no reader in a linear text scan (Unknown); the two 128-byte container
  header blobs are opaque (Unknown); purpose labels marked Hypothesis are documentation only.
- Next: S3 (started), S5, S6, then S2/S4.
