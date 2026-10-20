# EE entry ledger (TASK-014 stage B, Phase 1)

2026-10-20. Read-only analysis. `scripts/ee_entry_ledger.py` reads the reconstructed ELF, the
generated units and the name table, and writes a ledger of candidate function starts that have no
translated entry. Nothing is added to the translator, the library, a pin or a build. The ledger itself
is game-derived and stays private (`private/slice207-ee-ledger/`); this file records only aggregates.

Inputs (private checkout, not this worktree):

| input | identity |
|---|---|
| reconstructed ELF `private/reconstructed/SCUS_973.28.elf` | sha256 `10f82e22...c935c` |
| generated units `private/reference-acceleration-study/generated` | 326 `unit_XXXX.cpp` + `table.cpp` |
| name table | `docs/data/ee-names.csv`, regenerated from gt4-decomp `e0b70c4d6` (see below) |
| gt4-decomp | commit `e0b70c4d666b13a2daa1cc1959f83868cb8ae909` (80 commits after `80b81d10`) |

## Method

A translated entry is a `case N: goto L_XXXXXXXX;` label with non-zero `N` in a generated unit. That is
the rule `render_contract_inventory.resolve_dispatch` already uses. The ledger never treats a unit window
as an entry.

Four sources. Each candidate records every source that found it, and whether it is already translated.

| source | rule |
|---|---|
| `decomp` | Function addresses from `ee-names.csv` (kind `func`) and from the gt4-decomp `config/symbol_addrs.txt` and `config/adhoc_methods.txt`, inside the executable segment. |
| `fallthrough` | The word after the delay slot of an unconditional jump (`jr ra`, `j`, or `b`) when no conditional branch or direct `j` targets it and it opens a stack frame (`addiu`/`daddiu sp,sp,-N`). `jal` targets do not exclude a candidate. |
| `dataword` | Aligned words in non-executable PT_LOAD segments whose value is an aligned, non-zero word inside the text. |
| `luipair` | `lui rt,hi` followed within 3 instructions by `addiu rd,rt,lo` (signed) or `ori rd,rt,lo` (unsigned) that builds an address inside the text, with no intervening write to `rt`. The scan covers all of the text, not only code already discovered. |

Confidence (new candidates only):

- `high`: stack frame, and a decomp name, a fallthrough, or at least two sources.
- `medium`: stack frame alone, or a decomp name without a stack frame.
- `low`: one weak source (`dataword` or `luipair`) and no stack frame. These mostly look like data.
- `rejected`: the word at the candidate is zero or `0xFFFFFFFF`. Certain false positive.

## Numbers (run of 2026-10-20)

Translated entries (distinct addresses from the case labels): **135,806** in 326 units.

| source | raw unique | new (untranslated) | already translated | precision proxy | recall proxy |
|---|---:|---:|---:|---:|---:|
| `decomp` | 7,740 | 83 | 7,657 | 0.989 | 0.056 |
| `fallthrough` | 9,435 | 1,055 | 8,380 | 0.888 | 0.062 |
| `dataword` | 10,672 | 4 | 10,668 | 1.000 | 0.079 |
| `luipair` | 2,926 | 15 | 2,911 | 0.995 | 0.021 |

- Precision proxy = share of a source's raw hits that are already translated. It is a calibration, not a
  false-positive rate: translated entries are known function starts, so a high share means the detector's
  signature matches real starts.
- Recall proxy = share of translated entries the source also finds. It shows how much of the known code
  shape each source sees.

Candidates: **20,353** in total, **1,122 new**. New by confidence: high 1,076, medium 39, low 6, rejected 1.
Single-source new candidates: 1,087; two-source: 35. The new candidates fall in **246** distinct 16 KiB
unit windows (245 for the high group).

Overlaps (total hits / new untranslated):

| pair | total | new |
|---|---:|---:|
| decomp + fallthrough | 2,894 | 33 |
| decomp + dataword | 4,260 | 1 |
| decomp + luipair | 2,195 | 0 |
| fallthrough + dataword | 2,271 | 0 |
| fallthrough + luipair | 1,196 | 1 |
| dataword + luipair | 233 | 0 |

Other counters: jumps followed by a non-stack-frame word (not counted as hits): 31,560. Decomp names
outside the text: 0.

Sensitivity, not a source: a `lui`/`addiu` window of 40 words (the window stage A's study used) gives
3,887 addresses. Three of them are new and found by no other source (0x004D2620, 0x00523AF0,
0x0058E378).

## Spot check

20 new candidates, seeded (`--spot-check 20 --seed 207`). Each was disassembled for 3 instructions.
All 20 are high confidence. Each starts with `addiu $sp,$sp,-N`, then a `sd` register save or a body
instruction, with plausible decoded code after it. None looked like data. Reachability was not checked;
that is the open question for Phase 2.

## Stage A 16 seeds

Stage A's 16 referenced seeds (commit `65f24fb`, `docs/inputs/slice161-ee-entries-referenced.txt`) are all
untranslated in the case-label set. The strict rules find 14 of the 16 (`luipair`, one of them also
`dataword`+`decomp`). The other two (0x004D2620, 0x0058E378) are found only by the 40-word window. Note this
is not an independent check: stage A picked its seeds with the same kind of reference evidence.

## Name table at e0b70c4d6

`scripts/ee_symbols.py` run against the e0b70c4d6 clone, written to
`private/slice207-ee-ledger/ee-names-e0b70c4d6.csv`, is **identical** to the committed `docs/data/ee-names.csv`
(11,481 rows, 0 added, 0 removed, 0 changed). The 80 new decomp commits did not change the name set. The
committed table was not overwritten.

## False-positive notes

- Certain false positives: 1 (`rejected`). A `lui`/`ori` pair built from `0xFFFFFFFF`.
- Likely data: 6 (`low`). A `dataword` or `luipair` hit with no stack frame.
- Plausible but unproven: 36 new candidates with a decomp name and no stack frame (leaf functions). Not counted as false positives.
- The 1,055 fallthrough-only candidates have a stack frame and a fall-through structure. Their risk is
  reachability, not validity. The "high" label means structural evidence, not proof that code runs.
- Structural false-positive estimate: about 0.6% of new candidates (7 of 1,122). Dead-code share unknown.

## Corrections to older notes

- `docs/DECOMP-INTEGRATION.md` (2026-10-15) says only one address appears as a 32-bit word in the ELF, and
  it reports 5,676 decomp functions without an entry. Both disagree with this run. The `dataword` scan finds
  10,672 vtable-style words; the check at `0x00688450` (`CameraBase__vtable`) reads
  `0x0044F198` = `CameraBase__virtual_00`. The decomp unentried count is 83 of 7,740 unique addresses now.
  The older figures predate the current translated set. That document is not edited here.
- The translated count does not reconcile. The generated folder has 135,806 case labels, but its
  `generation.log` reports `entries 135093`. The 713 difference is unexplained and must be resolved before
  Phase 2 (see below).

## Not ported from stage A

Stage A's `scripts/ee_entries.py`, its test, and the slice161 pins are not on this branch. Its pin
`docs/inputs/slice161-ee-entries.json` records `expected_translator_entries: 135155`, which matches neither
the log (135,093) nor the labels (135,806). The pins also reference build directories that were never
created. Phase 2 can restore the generator from `65f24fb` with fresh pins. Stage A's
`docs/inputs/slice161-ee-entries-referenced.txt` (16 addresses, sha-pinned) is also left out. The seed
addresses are listed above and in the commit.

## Phase 2 recommendation

1. Resolve the 135,806 vs 135,093 gap first. Confirm which generated set is the production baseline, and
   that the seed counts below match the translator's own count.
2. Sources to trust: `high` new candidates (1,076), and `medium` new candidates with a decomp name. Drop the
   `rejected` and `low` ones (7). Expected addition: about 1,115 entries, giving about 136,900 translated.
3. Build approach: an extra-entries library first, as stage A designed it. Pass the accepted seeds through
   `--entries` to the translator, build a separate `entries` profile, and A/B it against production with
   the existing pins. The 1,115 seeds touch about 246 unit windows, so the changed-unit check will show the
   set. Only after owner acceptance, regenerate the production producer.
4. Qualify the translator's rejected-seed count is zero, as stage A's `validate_stats` already requires.

## Method limits

- Candidates come from static scans. Indirect calls through computed or stored pointers are only seen
  if a pointer sits in a data word or a `lui`/`addiu` pair.
- `jr` through a register other than `ra` is not an unconditional-jump source. A jump table after it would
  not be a fallthrough site.
- Branch targets come from conditional branches and direct `j` only. Indirect targets are not known.
