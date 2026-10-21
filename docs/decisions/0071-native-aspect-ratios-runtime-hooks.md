# 0071 — Native aspect ratios through runtime hooks (supersedes part of 0055)

Date: 2026-10-21. Status: Accepted (owner, 2026-10-21: "proceed with the plan"; scope change in AGENTS.md, PR #52).
Supersedes, in [0055](0055-native-widescreen-video.md), only the sentence "No translated game patch, guessed
memory write or forced camera projection." The rest of 0055 stays as written. Design:
[ULTRAWIDE-NATIVE-STUDY](../ULTRAWIDE-NATIVE-STUDY.md) (TASK-080).

**Context.** 0055 relied on the game's own Screen setting (4:3 or 16:9) and refused any host change to the game's
camera. The owner now wants other display ratios (16:10, phones near 19.5:9, 21:9, 32:9) in this repository, without
replacing the GS renderer. The study found where the race camera gets its aspect: `RaceBase::initMonitor`
(0x00388A80) builds the monitor matrix on its stack and hands it to the camera through a virtual call that our
runtime dispatches (return address 0x00388B38). TASK-079 (slice211) already writes guest RAM at VBlank (the game's own
Wide setting), so the "guessed memory write" line no longer described the code either.

**Decision.**
- Native aspect ratios are implemented by the host: a hook on the dispatched camera call scales the projection
  matrix (Hor+ at R >= 16:9, Vert+ below; geometry-correct, an optional hidden factor `GT4_ASPECT_FOV_EXTRA` for the
  owner's comparison), and the renderers stretch the race view to the screen while menus, FMVs and photo mode stay
  pillarboxed 16:9. Later phases (P2 HUD, P3 progressive menus) may add an ELF patch table re-translated into a new
  game library; each such patch falls under the same guard rails.
- **Guard rails (all phases):**
  1. **No save-data writes.** No hook writes the option object, the monitor record or anything else the game saves.
     The camera hook writes only the 64-byte matrix copy on the guest stack. (TASK-079's hold of the game's own Wide
     setting is the one existing exception, kept and hardened: it writes only after the game-data pointer settled.)
  2. **16:9 stays byte-identical.** At R = 16:9 with factor 1.0 no hook writes anything and the presentation does not
     change; headless frame dumps must equal the previous slice.
  3. **Every hook and patched word is listed, with tests.** Each hook names its guest address and what it reads and
     writes (table below); each future ELF patch lists address, old word and new word, and CTest checks the old
     words against the pinned ELF.
  4. **SilentWarrior112's patch file has no licence.** It is a reference for facts only (addresses and what they
     do); its text, tables and values are never copied, and nothing is applied by pnach.
- A setting that the player cannot see must not change the game: every switch has a documented default that
  reproduces today's behaviour (`GT4_DISPLAY_ASPECT=16:9`, `GT4_ASPECT_FOV_EXTRA=1.0`).

**Hooks and words (P1, slice212b).**

| Item | Where | Reads | Writes | Test |
|---|---|---|---|---|
| Race camera aspect hook | dispatch, call with return address 0x00388B38 (jalr 0x00388B30 in `RaceBase::initMonitor`; also reached from photo pause `func_003C49C8`) | a1 = sp (the 4x4 matrix), the window client size | the 16 stack words at a1, only when the scale is not 1 | `native_display_aspect`, `tests/python/test_display_aspect.py` (overlay harness) |
| Race-view marker | dispatch of `RaceDisplay::render` 0x0039F498 (race HUD; only reached through vtables) | nothing (pc only) | nothing | `native_display_aspect` (scene policy), overlay harness |
| Photo/movie signal | guest VBlank | display request struct 0x006184D0 (+0x24 pending, +0x2C mode, +0x30 row; row 2 = photo and movies) | nothing | `native_display_aspect` (scene policy) |
| TASK-079 Wide hold (existing) | guest VBlank | owner pointer 0x00622F4C (settled), option + 0x14/+0x24/+0x1388, presets 0x622E90..0x622F20 | option + 0x14, option + 0x24 (11 words), 0x006184F0 | `native_race_widescreen` |

**Consequences.** The P1 build changes only runtime code (a new license guard capsule `slice212c`, used by the slice212b game build, native host code,
pins); the game library is unchanged. A save made with any ratio is a plain save. PCSX2 GS presentation per scene
needs protocol 1.3 (`CONTROL display_aspect`) in gt4-gs; a 1.2 consumer keeps the 16:9 picture. P2/P3 ELF patches
need their own slices and pins.
