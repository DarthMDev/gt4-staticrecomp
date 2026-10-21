# Native ultrawide and other aspect ratios: study (GT4Recomp, read-only)

Date: 2026-10-21. Author: research agent (not PCSX2-tainted: no PCSX2 sources, no `gt4recomp-gs` fork, no
`private/tooling/pcsx2-*`, no `private/slice189-pcsx2-ref` were opened; only our own docs that cite them).
Scope: owner request of 2026-10-21 (16:10, 19.5:9 phones, 21:9, 32:9 and others, without replacing the GS renderer).
Builds on `docs/ULTRAWIDE-STUDY.md` (2026-10-15) and does not repeat it. This is the study for TASK-080. The scope
change it asks for was made in AGENTS.md (PR #52, 2026-10-21); the decision superseding the 0055 lines is still to be
written (phase P0). The SilentWarrior112 patch file has no licence: section 1 describes it in our own words only.

Labels: **Confirmed** (read in the ELF, the decomp at identical addresses, our code, or measured in a headless run),
**High confidence**, **Hypothesis**, **Unknown**.

Inputs used (no file was changed in any repository or worktree; no slice was built):

- ELF `private/reconstructed/SCUS_973.28.elf`, sha256 `10f82e22…7c935c` (matches `docs/inputs`).
- Decomp `private/slice151-research/gt4-decomp` at `e0b70c4d6`; `docs/data/ee-names.csv`.
- Reference pnach `private/references/silentwarrior-gt4-patches/77E61C8A.pnach` (sha256 `a0e7dbec…78fa`, no licence,
  used for facts only; its text and tables are not reproduced here).
- Production translation `private/slice208-ee-entries-producer/generated/` (to check how immediates are translated).
- Donor translator `private/references/ps2-web-recomp/recomp/codegen.cpp` (MIT).
- TASK-079 worktree `C:\Antigravity\gt4-wt-079` (read-only), launcher branch `origin/task/019-launcher` (via `git show`).
- Two headless runs of `private/slice211-build/build/gt4vulkan.exe` (section 0).

Scratch outputs: `private/uw-study-01/` (`seed/`, `card43/`: stderr, 42 frame dumps, `ram.bin` = full guest RAM
dump via `PS2_DUMP`; the run script is `private/uw-study-01/scripts/run.sh`). The ELF helper scripts are in the
session scratchpad, not in the repo.

---

## Summary

1. **The pnach targets our exact game.** The PCSX2 CRC (XOR of the 32-bit words of the boot ELF) of
   `SCUS_973.28` read from our ISO is `77E61C8A` [Confirmed]. The boot ELF is only a 273,020-byte loader; the code the
   patches touch is `CORE.GT4`, i.e. our reconstructed ELF. All 19 patched code addresses hold an instruction of the
   kind the patch expects (same opcode and register) [Confirmed]. Of the 12 "original word" comments, 6 match our ELF
   exactly, 1 matches its immediate with a byte-order typo, and 5 do not match; those 5 hold the same instruction kind
   with the expected value (±320, 448), so the comments are stale or typos, not a different binary [High confidence].
2. **The 3D aspect source the 2026-10-15 study could not find is now located** [Confirmed]:
   `RaceBase::initMonitor` (0x00388A80) builds the race camera's monitor matrix through
   `GranTurismo4::OptionMonitorMatrix` (0x00439288 → 0x004392E0) from the 11-word "monitor record" at option+0x24
   (= owner+0x38CD4), then hands it to the camera through a **virtual call** (`jalr` at 0x00388B30, return address
   0x00388B38). That virtual call goes through our `dispatch`, so a host hook there can change the 3D aspect
   **without touching guest code or save data**.
3. **Formula** [Confirmed from the disassembly]: horizontal/vertical frustum ratio = `Width × Aspect / Height`.
   Width and Aspect are interchangeable for the picture; vertical FOV depends only on Height, Distance, Zoom and
   Overscan. The game's own presets are calibrated to `Width × Aspect / Height = R / 1.0244` (16:9 and 4:3 both).
   Correct Hor+ for a display ratio R: multiply the stock horizontal term by `s = R / (16/9)`
   (Width = 425·s with Aspect 0.98, or Width 425 with Aspect = 0.98·s). The owner's **Width** values match
   425·s within 0.5% (S23: 512.7 vs 515; 3440x1440: 571.1 vs 573), but the owner **also** set Aspect ≈ s
   (1.20, 1.34), so by the game's math their picture is ~s times too wide horizontally (objects ~17% / ~25% narrower).
   SilentWarrior keeps Width 425 and sets Aspect = s (2% wider than exact). This discrepancy is the first owner test.
4. **Code patches cannot apply as-is** [Confirmed]: the translator turns `lui at,imm` into a C++ constant
   (e.g. `unit_00A6.cpp:4670` `0xC2D50000u`), and direct `jal` calls compile to direct unit calls that bypass
   `dispatch` (`codegen.cpp:274-283`). Three usable routes: (a) dispatch hook at `jalr` sites (runtime only),
   (b) guest RAM data writes, (c) an ELF patch table before translation (new library and pins).
5. **Recommended design (better than the pnach):** compute everything from R at run time; 3D by the dispatch hook
   (Hor+ for R ≥ 16:9, Vert+ below), HUD canvas by a hook on the virtual `RaceDisplay::render`, the remaining HUD
   constants by one ELF patch slice that turns each hard-coded 16:9 constant into a load from a host-filled parameter
   block; menus, FMVs and photo mode **pillarboxed at 16:9** by the renderer instead of stretched (no menu text hack);
   races stretched full width. No save-data writes at all, which removes the "Load failed!" class of bug.
6. **Rule change needed:** AGENTS.md "Out of scope here: … ultrawide and remaster features will be a separate project,
   possibly a fork" must be amended, and decision 0055 ("No translated game patch, guessed memory write or forced
   camera projection") must be superseded by a new decision (section 7).

---

## 0. Tests run (headless, no window, copies of non-owner cards)

| Run | Card source (copied) | Env | Result |
|---|---|---|---|
| `uw-study-01/seed` | `private/slice210-smoke/cand-210/card.ps2` (16:9 seed) | `GT4_FORCE_WIDESCREEN=0`, `PS2_DUMP` | exit 0, 4,200 frames, 42 dumps |
| `uw-study-01/card43` | `private/slice211-smoke/c-force0-card43/card.ps2` (4:3 card copy) | same | exit 0, 42 dumps |

Arguments: the slice211 list (`--ntsc --disc <ISO> --frames 4200 --memcard <copy> --frame-every 100`, 12 presses).
The headless run stays at the title (as recorded for slice211), so races, HUD and photo mode were not reached.

RAM read at the end of each run [Confirmed]:

| Item | seed (16:9) | card43 (4:3) |
|---|---|---|
| owner = word at 0x622F4C | 0x00A0CC10 | 0x00A0CC10 |
| option = owner+0x38CB0 | 0x00A458C0 | 0x00A458C0 |
| wide flag option+0x14 | 1 | 0 |
| published 0x6184F0 | 1 | 0 |
| record option+0x24 (0xA458E4): Flag, Zoom, Distance, Width, Height, Aspect, Overscan, 4 borders | 0, 1.0, 276.05, 425, 240, 0.98, 1.0, 0,0,0,0 | 0, 1.0, 238.16, 275, 205, 0.97, 1.0, 0,0,0,0 |
| presets 0x622E90/0x622EF0 (4:3) | 0, 1, 238.16, 275, 205, 0.97, 1, 0… | same |
| presets 0x622EC0/0x622F20 (wide) | 0, 1, 276.05, 425, 240, 0.98, 1, 0… | same |

Notes:

- The presets are zero in the ELF file (BSS); a static initializer fills them at boot [Confirmed: ELF bytes 0, RAM
  values above].
- The pnach's "3D aspect" address 0xA458F8 = 0xA458E4 + 0x14 = **Aspect**, and 0xA458FC = **Overscan** of the live
  record, because the live owner object is the static BSS object at 0xA0CC10 [Confirmed]. The pnach hard-codes that.
- The owner remembered Aspect 0.97 for the 16:9 defaults; in RAM the wide preset holds 0.98 (`0x3F7AE147`) and the
  4:3 preset 0.97 [Confirmed]. Minor; the menu may round.
- **New gotcha (for TASK-079's "Load failed!" fix):** the slice211 logs (`private/slice211-smoke/b-…` and `d3-…`
  stderr) show the TASK-079 hold's first write at owner **0x0096E360**, while my end-of-run dumps show owner
  **0x00A0CC10**. So 0x622F4C points at two different game-data objects during boot. Both have the MGame vtable
  0x00687E50 and 98.3% equal bytes; 0x96E360 still holds 4:3 defaults at the end [Confirmed]. **Hypothesis:** one is
  the load/scratch copy used while the save is read and checked; a hold that writes into it during the load changes
  the loaded data and makes the load fail. Section 3.4 has the design lesson.

---

## 1. Pnach map (question A)

### 1.1 Target check

- PCSX2 CRC = XOR of all 32-bit little-endian words of the boot ELF. Our ISO's `SCUS_973.28` (273,020 bytes) gives
  `77E61C8A` [Confirmed]; `SYSTEM.CNF` says `VER = 2.00`. `CORE.GT4` (2,020,861 bytes) gives `F2D46E8B` and our
  reconstructed ELF `D94D9746` (they are not boot ELFs; PCSX2 keys on the boot ELF).
- The patches write guest RAM continuously, so they hit `CORE.GT4` code after it is loaded, which is our
  reconstructed ELF image [High confidence].
- Instruction kinds at all 19 patched code addresses match (lui at / addiu / lwc1 / lb, same registers) [Confirmed].
- Comment words vs ELF: exact 6 (0x4A2A2C written big-endian, 0x39BA14, 0x39F554, 0x10732C, 0x102BE0, 0x33EF98);
  partial 1 (0x39BA20: the immediate 0x443A matches, byte order garbled); mismatch 5 (0x39BA60, 0x33EF78, 0x102C00,
  0x103090, 0x103098: the ELF holds 448 / ±320, the comments show other numbers) [Confirmed]. The cave at 0x492A30
  re-executes the original `lwc1 $f26,0x24(s2)`, which is exactly our word there [Confirmed]. The cave area
  0x4955C0..0x4955E4 is zero padding in our ELF (the zero run is 0x495574..0x496000, 2,700 bytes) and zero in RAM at
  frame 4200 [Confirmed].

### 1.2 Map (described in our words; values as decoded numbers, not the pnach's words)

Function names: decomp name when the decomp has the file, otherwise the nearest `ee-names.csv` entry; several of those
nearest names are wrong (marked "name wrong") and the function start is given instead.

| # | Section(s) | Address | Kind | Function (start) | What it changes |
|---|---|---|---|---|---|
| 1 | Deinterlace | 0x004A2A2C | code: `lb v0,0x2C0(a1)` → load 0 | `func_004A2A10` (decomp `src/network/unit_00494578/func_004A2A10.cpp`; ee-names "pglFrontFace", name wrong) | The per-field vertical half-pixel offset for field rendering. `a1` = scratchpad 0x70002000; byte spr+0x2C0 is the field parity. Forcing it to 0 makes every field use the same offset (no up/down jitter). **Not real progressive output**; it relies on the emulator showing one field without weave ("No interlacing"). [Confirmed code; effect High confidence] |
| 2 | 16:10 | 0x0039BB54 | code: `lui at,1.0` | `RaceDisplay::setProjectionMatrix` (0x0039BB38) | The HUD ortho matrix's w term (stored at m[15], +0x3C). A value above 1 shrinks the whole HUD around the centre ("safe zone", 90% for 1.117). Also affects split-screen HUDs (same setter). [Confirmed] |
| 3 | 16:10, 21:9, 32:9, 48:9 | 0x0039BA14 / 0x0039BA20 | code: `lui at` of l = -106.667 and r = 746.667 | `RaceDisplay::initialize` (0x0039B3E8), wide branch only (`D_006184F0 != 0`) | Left/right of the HUD ortho canvas (top 480, bottom 0). The game's 16:9 canvas is 853.33 = 480·16/9 wide, centred on 320. The pnach sets l/r to about `320 ∓ 238.5·R` (only the upper 16 bits; the low bits of the original stay). Exact rule: **l = 320 − 240·R, r = 320 + 240·R**. [Confirmed] |
| 4 | 16:10 | 0x0039BA60 | code: `addiu v0,zero,448` (half) | `RaceDisplay::initialize` | HUD height stored at RaceDisplay+0xB4 (width 640 at +0xB0). Raised for 16:10 (taller than 16:9). Use: Unknown. [Confirmed site; effect Hypothesis] |
| 5 | 16:10 | 0x0033EF78 | code: `addiu t1,zero,448` (half) | `RacePS2Base` vtable fn at 0x0033E860 ("virtual_121") | Divisor for the rear-view mirror rectangle's vertical maths. 448·(16/9)/1.6 = 497.8 → the 16:10 value exactly. [Confirmed site; effect High confidence] |
| 6 | 21:9 (commented out), 32:9, 48:9 | 0x0033EF98 | code: `addiu t0,zero,640` (half) | same function | Divisor for the mirror's horizontal maths; larger = narrower mirror. Exact undistorted value would be 640·s; the pnach uses smaller, tuned values for 32:9 and 48:9. [Confirmed site; effect High confidence] |
| 7 | 16:10, 21:9, 32:9, 48:9 | 0x00A458F8 | data | record Aspect (option+0x38) | 3D aspect. The pnach sets Aspect = s = R/(16/9) (0.90, 1.3145, 2.0, 3.0) with Width left at 425. The script menu's slider stops at 2.00; RAM accepts more. [Confirmed] |
| 8 | 16:10 | 0x00A458FC | data | record Overscan (option+0x3C) | Overscan 0.90 widens both FOV axes by 1/0.9 (Vert+ for a taller screen). [Confirmed math] |
| 9 | 16:10, 21:9, 32:9, 48:9 | 0x0010732C | code: `lui at,0.75` | `func_00107328` (decomp) | x scale 0.75 when the object's +0x20 flag is set, else 1.0, via `func_004A7890` (a scale). Pnach "ModelFaces": becomes (4/3)/R. Callers and what they draw: Unknown (startup/menu unit). [Confirmed code; use Hypothesis] |
| 10 | 21:9, 32:9, 48:9 | 0x0039F554 | code: `lui at,0.75` | `RaceDisplay::render` (0x0039F498) | Race HUD font x scale in wide mode: `func_0044E390(D_00623838, scale, 1.0)`; 0.75 = (4/3)/(16/9). Becomes (4/3)/R. [Confirmed] |
| 11 | 21:9, 32:9, 48:9 | 0x00492A30 | code: `lwc1 $f26,0x24(s2)` → jump to cave | PGL text glyph loop (function start 0x00492978; decomp `func_004924C0` shows the `State` layout; ee-names "strobe__ActionInstance__doAction", name wrong) | `s2` = text state, +0x24 = glyph x scale `sx`. The cave reloads it and, when the wide byte at 0x6184F0 is set, multiplies by a constant ((4/3)/R) → narrower glyphs in all PGL text (menus and anything else that uses it). [Confirmed] |
| 12 | 21:9, 32:9, 48:9 | 0x004955C0..0x004955E4 | code cave (10 words in zero padding) | padding before 0x496000 | The new code for #11; ends with a jump back to 0x00492A34. [Confirmed] |
| 13 | 21:9, 32:9, 48:9 | 0x0044DBD8 | code: `lwc1 $f12,0x18(v0)` → nop | `func_0044DBC0` (decomp) | Font object position/scale setter: skipping the load of `sx` (+0x18) makes it use the x argument instead, i.e. it drops the font object's own x scale (the 0.75 of #10) when x = 1. Purpose: avoid double squeezing with #11. [Confirmed code; interplay Hypothesis] |
| 14 | 21:9, 32:9, 48:9 | 0x00102BE0 / 0x00102C00 | code: `lui at,±320` | `func_00102B30` (decomp; loading screen with a 3D model) | Ortho half-width of the loading spinner/model (`func_004A5A90(-320,320,-224,224,…)`), then ×0.75 when wide. Pnach widens it by s (420 for 21:9, 640 for 32:9, 896 for 48:9; 960 would be exact) so the object keeps its shape. [Confirmed code] |
| 15 | 21:9, 32:9, 48:9 | 0x00103090 / 0x00103098 | code: `lui at,±320` | `func_00103018` (decomp; loading image) | Same for the loading image. [Confirmed code] |
| 16 | 10 Billion Credits | 0x00A20A28, 0x00A20A2C | data | owner+0x13E18 (game data, saved) | Money value (cheat). Out of scope; never implement. [Confirmed address] |

Wide-flag readers the pnach does **not** touch (every load of 0x6184F0 in the ELF; scan of `lw …,-0x7B10(lui 0x62)`)
[Confirmed sites]:

| Site | Function | Wide behaviour |
|---|---|---|
| 0x0032FD38 | `RaceSplitBattleBase::render` | split-screen (Unknown details) |
| 0x0033E30C | `RacePS2Base::render_world` | passes the flag to `func_00330AE8` (screen-space effect; Unknown) |
| 0x00393798 | `func_00393790` (ee-names "RaceSoundStop", name wrong) | scale ×0.75 for a textured HUD element |
| 0x003AFD24/0x003AFD90 | `RaceDisplayUtil::fillRectangle` area (`func_003AFD18`, `func_003AFD78`) | ±96 px edge shift |
| 0x003AFDF4 | `RaceDisplayUtil::wideOffset` | ±96 px edge shift (5 callers: count, lap times, gas, music, general info) |
| 0x003B81A8 | `func_003B80E8` (ee-names "RaceEventQueue::check") | a factor 0.911 only when NOT wide |
| 0x003D86DC | `func_003D8518` (ee-names "RaceMonitor::unload") | mini-map/monitor width 854 (= 480·16/9) when wide, 640 otherwise |
| 0x00102C94, 0x001030D0 | loading screen functions above | ×0.75 when wide |
| 0x004365F4 | `func_004365B8` (MOption.apply) | the only store: publishes `W/H ≥ 1.5` of the record |

So the pnach's ultrawide HUD keeps the game's ±96 px offsets: edge items stay near the 16:9 positions and the HUD looks
like a centred 16:9 HUD in the wider canvas [High confidence]. Photo mode and the result view use their own
perspective with a hard 4/3 factor (`func_003E13E8`, `func_003C5430`, from the 2026-10-15 study) and are untouched.

---

## 2. How our runtime could apply each patch (question B)

### 2.1 What is possible in our runtime [Confirmed]

- **Constants are compiled in.** `lui at,imm` becomes `c.r[1].sd[0] = static_cast<std::int32_t>(0xC2D50000u);`
  (`slice208-ee-entries-producer/generated/unit_00A6.cpp:4670`). Writing guest code RAM changes nothing for it.
  (Only relocation-linked hi/lo address fields are read from RAM at run time, `codegen.cpp:108-115`; float constants are
  not relocations.) This confirms the 2026-10-16 journal note (line 238).
- **Direct calls bypass dispatch.** `jal` to code becomes a direct `unit_XXXX(c, id, depth+1)` call
  (`codegen.cpp:274-283`). Only `jalr` (virtual and function-pointer calls) goes through `call → dispatch`, where the
  license guard hooks on the return address (`scripts/license_guard_overlay.inc:50`).
- **Data writes work** for any field the game reads from RAM (race_display, race_widescreen pattern at guest VBlank).

### 2.2 Routes per patch

| # | Patch | Best route | Why / risk / effort |
|---|---|---|---|
| 7, 8 | 3D Aspect / Overscan | **Dispatch hook** at the `jalr` in `RaceBase::initMonitor` (return 0x00388B38): `a1` = guest pointer to the 16-float matrix on the stack, `f12` = fovy. Multiply m[0] by `(16/9)/R` (Hor+, R ≥ 16:9) or m[5] by `R/(16/9)` (Vert+, R < 16:9); 4:3 game state uses 4/3 in place of 16/9. | No save data touched, no code change, runs once per race camera init (and photo pause, which also calls initMonitor). Same hook covers 1080i (initMonitor sets m[5] = m[0]·1.8 before the call). At R = 16:9 the factor is exactly 1.0f, so frames stay byte-identical (CTest/headless check). Risk low; effort 1 runtime slice (2-4 days with fixtures). Alternative "write the record at VBlank" is rejected: the record is save data (section 3.4). |
| 3 | HUD canvas l/r | **Dispatch hook at the entry of `RaceDisplay::render`** (0x0039F498; only reached through vtables 0x67E294/0x680D1C, no `jal` callers [Confirmed]): rewrite the ortho matrix at `this+0x70` (m[0] = 2/(r−l), m[12] = (l+r)/(l−r) with l/r from R) before render loads it (`func_004A74B4(self->x70)`). | Runtime only. Risk medium: the matrix must be rewritten for every RaceDisplay instance (split screen too), and `RaceDisplay::initialize` is also called directly from `RaceLanBattle` and `RaceLicenseDisplay` (no hook needed there because render is what reads it). Effort small once P1's hook machinery exists. Fallback: ELF patch (below). |
| 2, 4 | HUD safe-zone w, HUD height | Same render-entry hook (write m[15] = 1/zone; +0xB4) or the ELF patch table. | Only needed for R < 16:9 or a user "HUD size" option. Low priority. |
| 10, 9, 5, 6, 14, 15, plus the untouched readers (±96 offsets, 854 mini-map width, 0.75 in `func_00393790`) | inline constants | **ELF patch table → re-translation** (one producer slice): replace each constant load by a load from a **host-filled parameter block**, keeping the instruction count: `lui at,imm` (+ its `ori` when present) → `lw at,OFF(zero)` (+ `nop`). The host writes the float bit patterns for the chosen R into the block at boot. | One translation for all ratios (parameterised, no per-aspect binaries). Needs a reviewed patch table (address, old word, new word), new `ps2game.lib`, new pins, CTest that the table matches the ELF. Block location: low guest RAM 0x7000..0x7FFF is zero at frame 4200 [Confirmed] but its permanence is **Hypothesis** (our HLE kernel owns 0..0xFFFFF; it uses 0x800-0x848 and 0x1950+); reserve it explicitly in our kernel overlay. Risk medium (translation slice, pins, entry ledger unchanged because no new code). Effort 1 producer slice + tests (1-2 weeks incl. owner pass). |
| 11, 12, 13 | menu/PGL text width cave | **Not needed** in the recommended design: menus stay pillarboxed at 16:9 (section 4.3), so menu text is never stretched. If the owner wants stretched menus, use the ELF patch table: the cave needs new code in the padding (new entries: TASK-014 ledger work) or a same-length rewrite of the `lwc1` into a block-driven scale. | Avoids the pnach's known side effect (text narrower than its boxes, off-centre speedometer). |
| 1 | deinterlace (field offset) | **Prefer TASK-078 P5** (true 480p menus: ELF patch of `func_00436740` to request CRT row 6 with mode 2). The pnach-style single-word ELF patch at 0x4A2A2C is a fallback only. Not a data write: the parity byte is game-managed per field, a VBlank write would race. | P5 gives real progressive menus; the field-offset patch only stabilises a half-height picture and is redundant with the gt4-gs "Automatic" deinterlace (TASK-047). |
| 16 | credits | none | Cheat; out of scope. |

---

## 3. Width and Aspect: the formula (question C)

### 3.1 How the record feeds the projection [Confirmed, disassembly of 0x004392E0..0x004395E0]

Record at option+0x24 (owner+0x38CD4), 11 words: +0x00 Flag (int), +0x04 Zoom, +0x08 Distance (mm), +0x0C Width (mm),
+0x10 Height (mm), +0x14 Aspect, +0x18 Overscan, +0x1C/+0x20 left/right border (mm), +0x24/+0x28 top/bottom border.
The script native `MOption.physical_monitor_size` (0x00189410, decomp `MOption__get_physical_monitor_size.cpp`)
reads and writes exactly this array (1 int + 10 floats), so the hidden "Detail Monitor Settings" page edits it
[Confirmed native; that the hidden page uses this native: High confidence].

`OptionMonitorMatrix_2(out, record, a, b, c, d)` (a/b = LAN monitor position, c/d = Flag bits 1/0) scales mm by 0.001
and, for a single centred screen (offsets 0, no rotation), writes:

- m[0] = 2·Overscan·Distance / Width × zx
- m[5] = 2·Overscan·Distance / Height × zy
- if Aspect ≥ 1: zx = Zoom / Aspect, zy = Zoom; else zx = Zoom, zy = Zoom × Aspect
- m[8], m[9] = off-axis terms from the monitor offset (LAN multi-screen); m[10] = m[15] = 1.

Hence tan(half hFOV) / tan(half vFOV) = m[5]/m[0] = **Width × Aspect / Height**, in both Aspect branches.
`OptionMonitorFovy` (0x00439240) = atan2(Height/2, Distance·Zoom) is passed to the camera as fovy (vertical; ignores
Width, Aspect, Overscan). Borders only enter the LAN multi-monitor offset/angle maths (Flag bit 1 = angled side
screens): GT4 has a native multi-screen model with bezel compensation [Confirmed code; real use Unknown].

Readers of the record: only `OptionMonitorFovy`, `OptionMonitorMatrix` (scan of all `ori/addiu 0x8CD4..0x8D00`
after `lui 0x3`) and `func_004390D0` (W/H ≥ 1.5 → publishes 0x6184F0) [Confirmed]. Callers of `RaceBase::initMonitor`:
`RaceBase::initializeCamera+0x84` and the photo pause (0x003C4A24) [Confirmed]. So the values apply at race camera init,
not per frame; a change mid-race shows at the next race or camera re-init [High confidence].

### 3.2 Check against the defaults and the data points

The game's calibration: 425·0.98/240 = 1.7354 for a 1.7778 screen and 275·0.97/205 = 1.3012 for 1.3333; both are
`R / 1.0244` [Confirmed numbers]. So a geometry-correct setting for display ratio R satisfies
`Width × Aspect = 240 × R / 1.0244 = 234.3 × R` (Height 240), i.e. **s = R / (16/9) applied once** to either value.

| Case | R | Width × Aspect used | Correct product | Used / correct |
|---|---|---|---|---|
| Owner S23 Ultra 3088x1440, Width 515, Aspect 1.20 | 2.1444 | 618.0 | 502.4 | 1.23 |
| Owner 3440x1440, Width 573, Aspect 1.34 | 2.3889 | 767.8 | 559.7 | 1.37 |
| Pnach 16:10 (Width 425, Aspect 0.90) | 1.6 | 382.5 | 374.8 | 1.02 |
| Pnach 21:9 (Aspect 1.3145) | 2.3333 | 558.7 | 546.7 | 1.02 |
| Pnach 32:9 (Aspect 2.0) | 3.5556 | 850 | 833 | 1.02 |
| Pnach 48:9 (Aspect 3.0) | 5.3333 | 1275 | 1249.5 | 1.02 |

- **Width formula reproduces the owner's Width**: Width = 425·R/(16/9) gives 512.7 (owner 515) and 571.1 (owner 573),
  and the owner's Aspect equals s itself (1.206 → 1.20, 1.344 → 1.34) [Confirmed arithmetic].
- By the decomp math the owner applied s twice, so their horizontal FOV is about s larger than geometry-correct:
  S23 hFOV 96° instead of 86°, 3440x1440 109° instead of 92° (vFOV ≈ 47° unchanged). Round objects would look
  17% / 25% narrow. **Hypothesis** for why it looked right: a renderer setting that did not stretch the full frame,
  or a taste for extra FOV. Owner test T1 settles it.
- The pnach is 2% wider than exact because it sets Aspect = s instead of 0.98·s.

### 3.3 Proposed formula (parameterised)

Let R = width/height of the final picture on screen (window client area or monitor), and Rg = 16/9 when the game's
wide flag is set, 4/3 otherwise.

- **3D, Hor+ (R ≥ Rg):** m[0] ← m[0] × Rg / R. Vertical FOV, Zoom, Distance, Height and Overscan stay the user's.
- **3D, Vert+ (R < Rg, e.g. 16:10, 4:3 on a 16:9 card):** m[5] ← m[5] × R / Rg (horizontal FOV stays).
- Optional owner "extra FOV" factor f ≥ 1 (default 1): m[0] ← m[0] / f (this is what the owner's values do with f = s).
- Equivalent record values (for the hidden menu or documentation): Width 425, Aspect = 0.98 × R / (16/9); or
  Width = 425 × R/(16/9), Aspect 0.98.
- HUD canvas: l = 320 − 240·R, r = 320 + 240·R (equals the game's 16:9 values at R = 16/9).
- HUD font / model x scale: (4/3)/R (equals 0.75 at 16:9).
- Edge offset (wideOffset, fillRectangle): 96 at 16:9; "HUD at screen edges" = 96 + 240·(R − 16/9); "centred 16:9
  HUD" = 96 (pnach behaviour). Mini-map/monitor width: 480·R. Mirror divisors: 640·max(1, R/(16/9)),
  448·max(1, (16/9)/R). Loading ortho half-width: 320·max(1, R/(16/9)).

### 3.4 Where the values live, and the "Load failed!" risk

- The record is inside the option object (owner+0x38CB0), whose serialized header says 0x11C0 bytes from option+8
  (`func_004363A0`), which covers option+0x24: the record is **saved with the game save** [High confidence]. The
  pnach writes it every frame, so a save made with the pnach stores the patched values.
- TASK-079 lesson [High confidence for the mechanism, Hypothesis for the exact failure]: 0x622F4C points at
  0x96E360 early and at 0xA0CC10 later (section 0); a VBlank hold writes into whichever object it finds, including
  while the card is being loaded and checked. Design rules for any future hold: (1) never write save-data fields
  when a host-side override can do the job; (2) if a save field must be held, write only after the boot load has
  finished, only into the live object (owner stable for N frames and equal to the object the game renders with), and
  never while a memory-card operation is in flight; (3) prefer one write at a defined game event over a per-VBlank
  hold.
- **The recommended design writes no save data**: the 3D change is applied to the matrix copy on the stack at the
  camera call; the HUD changes go to the RaceDisplay object (runtime state) and a parameter block outside the save
  object. A save made while ultrawide is on stays a plain 16:9 save (or whatever the user picked in Options).

---

## 4. Renderer and launcher design (question D)

### 4.1 Renderers

- **gt4-gs (PCSX2 GS, separate process)** [Confirmed from our docs]: `AspectRatio` offered values are 16:9, 4:3 and
  "Auto 4:3/3:2" (`docs/launcher/gs-settings.toml:165-181`). PCSX2 also has "Stretch" and "10:7", not offered
  (`docs/GS-SETTINGS.md:359`). gt4-gs 1.0.0 **embeds** the allow-list (`docs/GS-SETTINGS.md:171-176`), so "Stretch"
  needs a table row change plus a new gt4-gs release (fork work, PCSX2-tainted agents only). There is no custom-ratio
  value: Stretch fills the child window, which covers the game window's client area (protocol section 6.6), so the
  picture ratio is the window's ratio.
- **Per-scene switching** (races full width, menus/FMV/photo pillarboxed 16:9) needs a run-time message:
  a new `CONTROL` kind in the MIT stream protocol (e.g. kind 7 "display aspect": mode stretch/16:9/4:3), protocol 1.3,
  implemented in the fork by a tainted agent. Without it, gt4-gs can only be "always stretch" (menus stretched, the
  pnach situation) or "always 16:9".
- **CoreGS 1.1 (built in)** [Confirmed]: `native_vulkan.cpp:31-40` `presentation_rect` letterboxes to an enum
  (16:9 or 4:3); `window.cpp:445` calls `set_widescreen(true)` once ("fixed 16:9"); the blit uses `VK_FILTER_NEAREST`
  (2026-10-15 study). Changes: a numeric target ratio or "fill", linear filter when the scale is not an integer, and the
  same per-scene switch driven host-side.
- **Scene signal** (host side, MIT): the game already announces display requests in the struct at 0x006184D0
  (`func_001074D0`: race `func_00436640` asks for rows 6/7, menus `func_00436740` row 0) [Confirmed in
  `docs/INGAME-SETTINGS-STUDY.md`]. Hypothesis: "race view active" = the race request row; photo pause and movies
  request row 2. Needs a headless/owner trace to confirm the timing (loading screens sit between).
- **Upscale preset:** `upscale_preset_for_monitor()` picks by vertical lines only. A stretched 640-px picture on a
  3440-px monitor needs about 5.4x horizontally; suggest picking by max(height/448, width/640·Rg/R) and the GPU
  texture limit. Unknown cost at 6x-8x on the owner's RX 9060 XT.

### 4.2 Learning the monitor and the window

- Launcher (Tauri 2): `Window::current_monitor()`, `primary_monitor()` and `available_monitors()` give each monitor's
  name, physical size in pixels, position and scale factor [High confidence; API not exercised here]. The launcher
  branch has **no** monitor code today (grep of all `.rs/.js/.html` on `origin/task/019-launcher` for monitor,
  resolution and aspect finds only settings rows) [Confirmed].
- Game (Win32): `window.cpp:116-135` already uses `MonitorFromWindow(MONITOR_DEFAULTTONEAREST)` +
  `GetMonitorInfoW(rcMonitor)` for borderless fullscreen; the window is created at `CW_USEDEFAULT` with a 960x540
  client area (`window.cpp:340-350`), i.e. normally on the primary monitor [High confidence].
- **Where R should come from:** the game, not the launcher. At the hook time (camera init, HUD render) read the
  current client rect (fullscreen = the monitor rect; windowed = the window), so Alt+Enter, a moved window or a second
  monitor are all right. The launcher only passes the policy: `GT4_DISPLAY_ASPECT=auto|16:9|4:3|<w>:<h>` and shows the
  detected monitor ("3440x1440, 21.5:9").
- **Multi-monitor:** add `GT4_DISPLAY_MONITOR=<device name>` (from Tauri's list) so the game opens and goes fullscreen
  on the chosen monitor. A "span all monitors" mode (window = virtual-screen rect) gives 48:9 triple screens as one
  stretched picture (the pnach's approach). GT4's own angled multi-screen model (Flag bits, borders, LAN positions)
  would need one render per screen: out of scope.
- **Windowed mode:** with "auto", resizing the window changes R at the next camera init; optionally lock the window
  ratio to the monitor ratio.

### 4.3 Why menus should be pillarboxed (doing better than the pnach)

Menus, FMVs and photo mode are 4:3-designed pages that the game stretches to 16:9. Stretching them to 21:9 or 32:9
distorts images and boxes; the pnach only narrows text glyphs and accepts the side effects. A per-scene presentation
switch keeps menus exactly as today (16:9) and uses the full width only where the game draws a wider world. It also
removes four of the pnach's code patches (#11-#13 and, if loading screens are pillarboxed, #14-#15).

---

## 5. Building on the 2026-10-15 study (question E)

New facts versus `docs/ULTRAWIDE-STUDY.md`:

- A GT4 NTSC-U widescreen/ultrawide pnach exists (SilentWarrior112, CRC 77E61C8A) and targets our binary (section 1.1).
- The race camera aspect source is found: the monitor record → `OptionMonitorMatrix` → `RaceBase::initMonitor` →
  camera virtual +0xC0 slot (vtable entry 24) (section 3.1). The 2026-10-15 "Hypothesis: a 4:3 constant in the camera"
  is wrong for races; the decomp's `func_004A40B0` is fog (ai-memory, 2026-10-17), not a projection.
- The writer of 0x6184F0 is `func_004365B8` (MOption.apply) at 0x004365F4, from `W/H ≥ 1.5` of the record
  [Confirmed]; the 2026-10-15 "Unknown" is closed.
- Culling: still **Unknown**. ai-memory (decisions/native-renderer-branch) records "track culling is by position only",
  which would suit ultrawide, but it is history, not checked here. Owner test T3 looks for pop-in at the edges.
- The 2026-10-15 recommendation (A: RAM write of a camera aspect) is replaced by the dispatch hook, which avoids save
  data.

### Rule that would have to change

- **AGENTS.md, "Mission and direction", bullet "Out of scope here":** it lists "ultrawide and remaster features will be
  a separate project, possibly a fork". The owner's 2026-10-21 request puts native aspect ratios in this repository.
  Proposed wording: aspect-ratio support (Hor+/Vert+ 3D, HUD and presentation parameterised by the screen ratio) is in
  scope after 1.0.0; remaster features (new assets, native renderer, ultrawide beyond the game's own projection model)
  stay out of scope.
- **Decision 0055** says "No translated game patch, guessed memory write or forced camera projection". TASK-079 already
  departs from "memory write"; this design adds a forced camera projection (hook) and, for P2, a translated game patch
  (ELF patch table). A new numbered decision should supersede those lines and state the guard rails (no save-data
  writes, default byte-identical, every patched word listed with old/new values and checked by CTest).
- `docs/GS-SETTINGS.md` "Widescreen patches (finding)" stays true for pnach files; the new decision should say that
  the equivalent behaviour is implemented natively, not by pnach.

---

## 6. Phased plan

Task numbers start at TASK-080 (ai-memory current-state). All phases after 1.0.0 unless the owner says otherwise.
Each phase: own branch from `main`, fresh `private/sliceNNN-*` dirs, new pins, CTest + Python, owner in-game test,
owner approval before merge.

| Phase | Content | Route | Slices | Automated checks | Owner tests | Risks |
|---|---|---|---|---|---|---|
| P0 | Rule change (AGENTS.md bullet), new decision superseding 0055 lines, TASK entries | docs | 0 | none | approve wording | none |
| P1 | Auto 3D aspect: `GT4_DISPLAY_ASPECT` parse (auto default, invalid refuses start), R from client rect, dispatch hook at return 0x00388B38 (Hor+/Vert+), optional FOV factor; CoreGS 1.1 fill presentation with linear filter; launcher row "Aspect ratio: Auto / 16:9 / 4:3 / custom" + detected monitor | runtime overlay (dispatch capsule like the license guard) + native + launcher | 1 runtime slice (+ launcher build) | fixtures for factor math and parse; hook unit test with a synthetic matrix; headless 4,200-frame run at R = 16:9 byte-identical to slice211; `--mode` pin tests | **T1**: geometry check (a tyre or a round sign in a straight replay: compare roundness) with three settings: auto Hor+, the owner's 515/1.20-style factor, pnach-style; **T2**: 3440x1440 and phone-like window, Time Trial Nordschleife Supra; **T3**: edge pop-in at 32:9 (window 32:9 on any monitor) | culling at wide FOV (Unknown); hook must find the matrix on the guest stack (a1) every time (also photo pause path); gt4-gs still 16:9 until P1b |
| P1b | gt4-gs "Stretch" (allow-list row + gt4-gs release) and protocol 1.3 CONTROL "display aspect" (per-scene) | fork (PCSX2-tainted agent) + MIT protocol doc | gt4-gs release | gt4-gs allow-list tests; protocol fixture | T2 on gt4-gs | fork release cadence; per-scene timing |
| P2 | HUD/UI: (a) render-entry hook for the HUD canvas; (b) ELF patch table + parameter block for font scale, edge offsets, mini-map width, mirror, `func_00393790`, `func_00107328`, loading ortho; owner choice "HUD at edges / centred 16:9" | (a) runtime; (b) producer slice: patched ELF derivative, re-translation, new `ps2game.lib` and pins | 1 runtime + 1 producer slice | CTest: patch table matches the ELF (old words), parameter block values per R, 16:9 values reproduce the original constants exactly (so 16:9 frames stay byte-identical); headless identity run | **T4**: race HUD at 21:9 and 32:9 (speedometer, lap times, mini-map, mirror, countdown, results); **T5**: license test and LAN-less split screen if reachable | low-RAM block permanence (Hypothesis; reserve it in the kernel overlay); the 0x44DBD8-type font interplay; split screen |
| P3 | Progressive menus = TASK-078 P5 (ELF patch of `func_00436740`, maybe FMV/photo rows) | producer slice (can share P2's patch table) | 1 | same as P2 | **T6**: every menu, FMVs, photo mode, license tests at 480p | movies/photo request row 2 separately (stay 480i unless patched) |
| P4 (optional) | "True FOV" mode: physical monitor size (EDID) + seat distance → GT4's own Width/Height/Distance model; span-all-monitors | runtime + launcher | 1 | fixtures | sim-rig owner test | EDID reliability |

Mobile (phase 2/3): the same parameterised design applies (19.5:9, 20:9 landscape); the ARMSX2 GS pipeline needs the
same fill/pillarbox switch.

---

## 7. Open unknowns

1. The owner's data points vs the decomp math (section 3.2): which renderer and aspect setting were used. [Unknown]
2. Culling and LOD at very wide FOV (edge pop-in). [Unknown]
3. Low guest RAM 0x7000-0x7FFF free for the whole session (races, saves). [Hypothesis: free at frame 4200]
4. The exact "Load failed!" mechanism and the role of the second MGame object at 0x96E360. [Hypothesis]
5. Photo mode, result view and replays: photo pause calls `initMonitor` (covered by the hook) but also uses
   hard-coded 4/3 perspectives (`func_003C5430`, `func_003E13E8`); which views use which. [Unknown]
6. Split-screen (`RaceSplitBattleBase`, `RaceSplitDisplayBase` 320/640-wide viewports). [Unknown]
7. Meaning of RaceDisplay+0xB4 (448) and the exact mirror maths. [Hypothesis]
8. `func_00107328` callers (pnach "ModelFaces"). [Unknown]
9. The scene signal for per-scene presentation (display request row at 0x6184D0) and its timing around loading
   screens. [Hypothesis]
10. gt4-gs cost of 6x-8x internal resolution for ultrawide sharpness on the owner's GPU. [Unknown]
11. Whether `RaceDisplay::render` is ever reached by a direct call in a build other than slice208's
    translation (scan found none in the ELF). [High confidence: none]

## 8. Owner decisions needed

1. Approve the scope change (AGENTS.md bullet) and a new decision superseding the 0055 lines (section 5).
2. FOV policy: geometry-correct Hor+ (recommended; matches the pnach within 2%) or the owner's wider values (an
   "extra FOV" slider, default off).
3. Menus/FMV/photo: pillarboxed 16:9 (recommended) or stretched full width with the text fix.
4. HUD: centred 16:9 block (pnach style, recommended for 32:9 and wider) or spread to the screen edges (nicer at 21:9);
   possibly a launcher option.
5. Accept an ELF patch table with re-translation for P2/P3 (new library and pins), or keep HUD changes to runtime
   hooks only (canvas yes; font and edge constants then stay 16:9).
6. gt4-gs: add "Stretch" and the per-scene CONTROL message in a new gt4-gs release (fork work by a PCSX2-tainted
   agent).
7. Placement: after 1.0.0 (recommended) and phase order P1 → P1b → P2 → P3.
