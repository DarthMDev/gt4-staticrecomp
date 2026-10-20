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
  The older figures predate the current translated set. A dated correction is added to
  [DECOMP-INTEGRATION](DECOMP-INTEGRATION.md).
- The translated count does not reconcile. The generated folder has 135,806 case labels, but its
  `generation.log` reports `entries 135093`. The 713 difference is unexplained and must be resolved before
  Phase 2 (see below).

## Phase 2: accepted seeds and closure (2026-10-20)

Accepted seeds: 1,105 (high 1,076 plus medium with a decomp name, 29). Pinned by
`docs/inputs/slice208-ee-entries.txt` (sha256 6cf39fb7...) and `docs/inputs/slice208-ee-entries.json`. Dropped 17:
10 medium without a name (includes 0x0044EA58, the old callback seed), 6 low, 1 rejected (0x00616F24, word 0xFFFFFFFF).
The dropped list is in `private/slice207-ee-ledger/slice208-dropped-seeds.json`.

Translator with the seeds (`eerecomp --entries`, analysis level):

| measure | baseline | with 1,105 seeds | change |
|---|---:|---:|---:|
| analysis entries (stat line) | 135,093 | 138,813 | +3,720 |
| code instructions | 1,240,374 | 1,272,453 | +31,879 |
| functions | 14,660 | 15,907 | +1,247 |
| rejected seeds | 0 | 0 | 0 |
| dispatch ids (case labels) | 135,806 | 139,534 | +3,728, none lost |

Seeds resolve: 1,105 of 1,105 translated, 0 stop sites. Seeds explore their callees, so the
closure reaches code far from the seeds: **325 of the 326 generated units change** (plus `table.cpp`).
Phase 1 expected about 246 windows; that estimate covered the seeds alone and was wrong.
The changes are code changes, not only id renumbering (ids are sequential per unit).

Compiler: the production library was built with clang-cl 23.1.2. The installed system clang-cl is 22.1.3, and
22.1.3 changes every object (0 of 8 sampled units identical). The candidate uses the portable LLVM 23.1.2 at
`private/tooling/llvm-23.1.2/bin/clang-cl.exe`. With 23.1.2, the same 8 units are byte-identical to the
production archive members after timestamp normalisation (8 of 8).

## Phase 2 build and verification (2026-10-20)

- Library (`coverage`): 139,534 dispatch ids = 135,806 baseline + 3,728 new, none lost; all 1,105 seeds translated;
  325 objects differ from production and are exactly the 325 changed units (unit_00F8 and table.cpp are identical).
  Library sha256 `8f40b11e...`, 70,694,004 bytes.
- Executable: `slice208-build` (97,104,896 bytes, sha256 `b96c6d64...`), built with the slice206 compiler (22.1.3).
- CTest 33 of 33; Python 583 OK, 23 skipped.
- Headless, 4,200 frames, 42 dumps: identical to slice206 (42 of 42).
- Not shown: whether the 1,105 new entries are reached. The headless path does not reach them; the owner play-through is the check.

## Stage A tooling

`scripts/ee_entries.py`, `tests/python/test_ee_entries.py` and the slice208 seed pins are restored from stage A
(commit `65f24fb`) and adapted: the expected count is measured, not inherited (see above). The producer may now write
into the assets checkout's `private/` (`scripts/ee_producer.py`). The qualified compiler is the portable 23.1.2.
Stage A's slice161 pins are not used.
