# 0072 — Progressive 480p menus and races, switchable at run time (TASK-086 step 1)

Date: 2026-10-21. Status: Accepted (owner decisions of 2026-10-21: progressive on by default with an off switch, CT16
menus as the Try pop-up, movies, photo develop and CRT row 2 out of step 1). Study:
[PROGRESSIVE-EVERYWHERE-STUDY](../PROGRESSIVE-EVERYWHERE-STUDY.md). Builds on
[0071](0071-native-aspect-ratios-runtime-hooks.md) (the ELF patch table and its guard rails).

**Context.** The game shows its menus at 480i (CRT row 1, 224-line field buffers) and its races at 480p when the
race display index is 1 (our launcher always holds it there, `GT4_FORCE_480P`). Every switch between a menu and a race
therefore changes the CRT mode, and the game blanks the screen for 60 frames. The 480p "Try..." pop-up of the Screen
options proves that the menus already draw correctly at 480p: it calls `MOption.applyRaceDisplayMode`
(`func_00436640`, index 1 → `func_001074D0(state, 2, 6, 640, 448)`) and the menu page renders at full height.

**Finding during the build (Confirmed, headless runs).** `applyMenuDisplayMode` (`func_00436740`) runs only when the
game leaves a scene (race, replay, demo, movie: `func_00108040` runScene, `func_0010CA50`, `func_0010BE20`,
`func_001103E0`, `func_001C8778`). The menus after boot keep the boot mode set by `func_00107730` → `func_001062E8` →
`func_001063A8` (480i field layout, CRT row 1). With the menu and race words alone (slice230), the boot menus stayed
480i (the menu schedule was 42/42 identical to the 480i run) and the CRT switched to 480p at the first race start
(VBlank 7518); after the race, exiting to the Arcade menus kept 480p with no further CRT change, and the menus were full
448-line frames (slice232 `x-on` runs). Three ways to make the boot itself 480p were tried and none works yet:
- a host-side request of the menu mode after boot: the rebuild it triggers resets the VRAM pool after the boot has
  uploaded its data (all dumps black from then on), or, when made earlier, the boot overwrites it;
- the boot rewrite below (slice231): the boot builds the 480p layout, but the display mode object
  (`func_00100BB8(mode)` → `func_00100A40`, a debug-menu applier run as a game object) re-applies mode 2 (640 field)
  during boot and brings 480i back;
- the boot rewrite plus mode 2 → mode 4 in `func_00100A40` (slice232 with `GT4_PROGRESSIVE_BOOT=1`): the boot stays
  480p but stops drawing (46,062 primitives in 4,200 frames instead of 1.2 million); the main game object waits in
  `GameObjectBase::sync` from `MenuGameObject::startProject` (thread dump in `private/slice232-scripts`). [U] why.
So the boot path stays in the library, dormant, behind its own experimental switch `GT4_PROGRESSIVE_BOOT` (off unless
`1`; the launcher never sets it), for TASK-086 step 2.

**Decision.**
- With progressive on, menus and the race "normal" index ask for exactly the pop-up's mode, (psm 2 = CT16, CRT row
  6 = 480p, 640, 448). The boot and the menus before the first scene stay 480i; the first race start switches to 480p
  once (the game's 60-frame blank, as in 1.1.0); from then on menu and race requests are equal, so `func_001074D0` never
  sees a change between them: no buffer rebuild and no 1-second black. Same VRAM footprint as the 480i menus (the
  study: 0x69000 words either way, FBP 0x00 and 0x8C).
- **Run-time switch, not a build-time one.** The changed instructions read host words (0x7F08..0x7F1F, after the
  TASK-080 HUD block 0x7F00..0x7F07) in the EE library profile `progressive` (`scripts/aspect_elf_patch.py`,
  `PROGRESSIVE_PATCHES` and `BOOT_REWRITES`; slice232). The host fills them at every guest VBlank from `GT4_PROGRESSIVE`
  (`native/progressive_display.*`): `1` (default, also when unset) the progressive values, `0` the original constants,
  so the frames are byte-identical to slice218. The boot layout word is 1 only with `GT4_PROGRESSIVE_BOOT=1` as well.
- The launcher's Settings > Graphics row "Progressive 480p" (default on, settings schema 6) sends `GT4_PROGRESSIVE`.
- Out of step 1 (TASK-086 step 2): the boot, CRT row 2 (photo pause, after movies), movies and photo develop
  (`func_00106A68`), and the photo-develop return (`func_001063A8` at 0x003C89F4). They keep their own modes; leaving
  them still changes the CRT once, and the next menu request brings 480p back.

**Patch table, menus and races (one `lw` of the same register per row).** Block words: +0x08 menu psm (off 1, on 2),
+0x0C menu CRT row (off 0 = default row 1, on 6), +0x10 field halving factor f32 (off 0x3F000000 = 0.5, on
0x3F800000 = 1.0), +0x14 draw-back viewport type (off 1, on 0), +0x1C the race monitor's explicit row (off 1, on 6).

| Address | Old word | New word | Block word | Function | Change |
|---|---|---|---|---|---|
| 0x00436750 | 0x24050001 `addiu a1,zero,1` | 0x8C057F08 `lw a1,0x7F08(zero)` | 0x00007F08 | `func_00436740` applyMenuDisplayMode | menu psm |
| 0x00436754 | 0x0000302D `daddu a2,zero,zero` | 0x8C067F0C `lw a2,0x7F0C(zero)` | 0x00007F0C | `func_00436740` | menu CRT row |
| 0x00436700 | 0x24050001 | 0x8C057F08 | 0x00007F08 | `func_00436640` applyRaceDisplayMode, index 0 | race psm (normal index) |
| 0x00436708 | 0x0000302D | 0x8C067F0C | 0x00007F0C | `func_00436640`, index 0 and indexes above 2 | race CRT row |
| 0x00436680 | 0x24050001 | 0x8C057F08 | 0x00007F08 | `func_00436640`, indexes above 2 (delay slot) | keeps that path equal to index 0 |
| 0x00218240 | 0x3C013F00 `lui at,0x3F00` | 0x8C017F10 `lw at,0x7F10(zero)` | 0x00007F10 | `func_00218170` (colour window backdrop, `mColorWindow__virtual_92`) | y/h halving for field buffers |
| 0x00217EA0 | 0x3C013F00 | 0x8C017F10 | 0x00007F10 | `mRenderContextPS2__virtual_67` capture pass | y halving of the captured rectangle |
| 0x00217EF8 | 0x24040001 `addiu a0,zero,1` | 0x8C047F14 `lw a0,0x7F14(zero)` | 0x00007F14 | `mRenderContextPS2__virtual_67` (delay slot) | viewport type of the draw-back (1 = half-height field viewport) |
| 0x003DA488 | 0x24050001 `addiu a1,zero,1` | 0x8C057F08 `lw a1,0x7F08(zero)` | 0x00007F08 | race monitor state change (RaceMonitor area, code after 0x003DA468) | psm of its explicit 480i field request `func_001074D0(state, 1, 1, 640, 448)` |
| 0x003DA490 | 0x24060001 `addiu a2,zero,1` | 0x8C067F1C `lw a2,0x7F1C(zero)` | 0x00007F1C | same | its explicit CRT row 1; its row-2 request at 0x003DA4EC stays (step 2) |

Index 1 (480p) and index 2 (1080i) of `func_00436640` are unchanged; with the switch on, index 0 behaves as index 1.

**Boot rewrite (`BOOT_REWRITES`, dormant: experimental `GT4_PROGRESSIVE_BOOT=1`).** Block word +0x18 boot layout
(0 unless both switches are on), read by `func_001062E8` and by the mode 2 case of `func_00100A40`. Zero is also the value before the
host's first fill, and zero takes the original path, so an early read can only fall back to the 480i boot. In the
headless runs the fill at VBlank 1 comes before the boot's SetGsCrt.

| Address | Old word | New word | Function | Change and why off is unchanged |
|---|---|---|---|---|
| 0x001062FC | 0x00A0802D `daddu s0,a1,zero` | 0x8C107F18 `lw s0,0x7F18(zero)` | `func_001062E8` | the layout choice reads the block word; the only caller (`func_00107730`, 0x00107748) passes a1 = 1 and s0 only feeds the branch below |
| 0x00106344 | 0x12000006 `beq s0,zero,0x00106360` | 0x16000006 `bne s0,zero,0x00106360` | `func_001062E8` | off (0) falls through to the original tail jump to `func_001063A8`, as a1 = 1 did |
| 0x00106360 | 0x0C0419C4 `jal 0x00106710` | 0x0C041B6A `jal 0x00106DA8` | `func_001062E8`, path never taken from its caller | the 480p initialiser instead of the 512-wide field layout; the return point 0x00106368 stays, so the translator's entries are unchanged |
| 0x00106364 | 0x0220202D `daddu a0,s1,zero` | 0x24050002 `addiu a1,zero,2` | same (delay slot) | psm 2; a0 already holds the state (0x00106310) |
| 0x0010631C | 0x44810000 `mtc1 at,f0` | 0x24060050 `addiu a2,zero,0x50` | `func_001062E8` | a2 = 0x50 for the 480p call; `func_001063A8` ignores a2 |
| 0x00106330 | 0xE6200014 `swc1 f0,20(s1)` | 0xAE210014 `sw at,20(s1)` | `func_001062E8` | the same 1.0f bits (at = 0x3F800000) stored to state+0x14 |
| 0x00106324 | 0x2403FFFF `addiu v1,zero,-1` | 0x2407FFFF `addiu a3,zero,-1` | `func_001062E8` | a3 = -1 (width 640) for the 480p call; `func_001063A8` ignores a3 |
| 0x0010632C | 0xAE230030 `sw v1,48(s1)` | 0xAE270030 `sw a3,48(s1)` | `func_001062E8` | the same -1 stored to state+0x30 |
| 0x001077A4 | 0x24050001 `addiu a1,zero,1` | 0x8E050000 `lw a1,0(s0)` | `func_00107788` (boot CRT set, s0 = 0x006184D0) | interlace from the state |
| 0x001077B4 | 0x24060002 `addiu a2,zero,2` | 0x8E060004 `lw a2,4(s0)` | `func_00107788` | mode from the state |
| 0x001077A8 | 0x24070001 `addiu a3,zero,1` | 0x8E070008 `lw a3,8(s0)` | `func_00107788` | ffmd from the state |

`func_00100A40` (display mode object at 0x006D6708, modes 1-5 through a jump table at 0x0068BB30) has no direct
caller; it applies the mode set by `func_00100BB8` (called with mode 2 during boot and by `func_003D8A60`). Its mode 2
case (six words, the last one alignment padding before the mode 3 case) is rewritten in place; the jump table and the
other cases are unchanged:

| Address | Old word | New word | Change and why off is unchanged |
|---|---|---|---|
| 0x00100AA0 | 0x3C040062 `lui a0,0x0062` | 0x8C027F18 `lw v0,0x7F18(zero)` | the boot layout word (v0 is dead here) |
| 0x00100AA4 | 0x0C0418EA `jal 0x001063A8` | 0x1440000A `bne v0,zero,0x00100AD0` | on: the mode 4 case, `func_00106DA8(state, 2, 0x50, -1, -1)` |
| 0x00100AA8 | 0x248484D0 `addiu a0,a0,-31536` | 0x3C040062 `lui a0,0x0062` | delay slot; the mode 4 case sets a0 itself |
| 0x00100AAC | 0x10000019 `beq zero,zero,0x00100B14` | 0x0C0418EA `jal 0x001063A8` | off: the original call |
| 0x00100AB0 | 0x00000000 `nop` | 0x248484D0 `addiu a0,a0,-31536` | its delay slot: a0 = 0x006184D0 as before |
| 0x00100AB4 | 0x00000000 (padding) | 0x10000017 `beq zero,zero,0x00100B14` | the original exit; its delay slot (`lui a0,0x0062`, the mode 3 case's first word) is overwritten on the exit path (`addiu a0,zero,-1` at 0x00100B18) |

`func_00107788` runs right after `func_00107730` (0x00101710) and set the CRT to (1, 2, 1), the values
`func_001063A8` had just stored, so `func_001060F8` returned early (one SetGsCrt per boot in every slice218 log). It
now re-applies the state's own values: the same call off, and the 480p mode kept on. f0 is not loaded any more on the
shared path; `func_001063A8` and its callees take no float argument [H; the off runs are byte-identical].

**Why the two UI halvings are in the table.** Both passes were written for 224-line field buffers: `func_00218170`
switches to the full-frame viewport (`func_004A2A10(0)`) and halves y and h by hand; `mRenderContextPS2__virtual_67`
halves the captured rectangle when `self+0x1D9C == 0` and draws the result back with the half-height field viewport
(`func_004A2A10(1)`) hard-coded. In 448-line frames both would land in the top half at half size. With the switch on,
the factor is 1.0 and the draw-back uses the full-frame viewport, which is how every other menu draw works at 480p
(the viewport type follows the buffer flag, 0 for frames). [H: reasoning from the code; the owner pass checks the
colour window.]

**The game's own Screen option (480i / 480p).** With the switch on, both choices give 480p: index 0 (480i) now asks
for the same 480p tuple, index 1 is 480p, and the menus are 480p. The Try pop-up still runs; it no longer changes the
picture. 1080i (index 2), if a card holds it, keeps its own mode (the launcher's `GT4_FORCE_480P` hold only rewrites
index 0). With the switch off the option works as in the original game, apart from the existing `GT4_FORCE_480P`
hold that keeps races at 480p.

**Guard rails (0071, unchanged).** No save-data writes (the words live in unused kernel RAM; nothing else is written
by the host). Off = byte-identical frames to slice218 (checked headless). Every changed word is listed here and checked
against the pinned ELF (`tests/python/test_progressive_display.py`). The SilentWarrior pnach was not used.

**Consequences.**
- Menus after the first race and races are 448-line frames in CT16 (16-bit colour with the game's dither, as the
  races and the Try pop-up); gradients may band slightly more than the CT24 field buffers. The boot menus stay 480i
  until the first race (step 2). CT24 480p menus (psm 1, row 6) stay an option
  for step 5 of the study.
- CoreGS 2.0 can treat menus as progressive frames; its interlaced path stays for the off switch and the step-2
  screens.
