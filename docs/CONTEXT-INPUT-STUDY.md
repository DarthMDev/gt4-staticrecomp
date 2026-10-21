# TASK-077 study: context-aware input mapping

Date: 2026-10-20. Read-only study (no build, no game launch, no PCSX2 source opened). TASK-077 stays open and is out of the 1.0.0 scope (owner, 2026-10-20); this document is kept for the work after 1.0.0.

Labels: **Confirmed** (read in code, ELF or repo docs), **High confidence** (decomp source plus header/vtable evidence, not runtime-tested), **Hypothesis** (plausible, needs a runtime probe), **Unknown**.

## 0. Sources and caveats

- Decomp: `private/slice151-research/gt4-decomp`, checked out at `e0b70c4d6`. The native-renderer study cites `80b81d10`; treat its addresses as the same ELF but re-check any cited file names.
- Repo docs read: `AGENTS.md`, `TASKS.md` (TASK-018, 075, 076, 077, 029), ai-memory page `decisions/input-layer-sdl3.md` (pinned, accepted 2026-10-21), `docs/native-renderer/STUDY.md` (branch `native-renderer/main`), `docs/LICENSE-DEMO-CIPHER.md`, `docs/data/ee-names.csv`, `docs/SCENE-OBSERVATION.md`, `native/race_display.hpp/.cpp`, `native/license_guard.cpp`, `native/host_io.cpp`.
- **Repo state:** the controls file schema (`docs/CONTROLS.md`, `native/controls.cpp`) is **not on `main`**. It is on `task/029-input-mapping`, merged into `task/019-launcher` (TASKS.md line 67). Any TASK-077 work starts after that merges.
- `task/075-sdl3-input` exists locally but its history is stacked on launcher commits (253 files differ from `main`). Do not treat it as a finished TASK-075 layer without checking.
- The ELF pin is `private/reconstructed/SCUS_973.28.elf`. Every address below is in that ELF's space.

## 1. Question 1: how to tell the contexts apart each frame

### 1.1 Mechanism that already exists

`native/race_display.cpp` (slice 164, GT4_FORCE_480P) already reads guest RAM from the host at VBlank: a fixed word at `0x622F4C` is an owner pointer, `owner + 0x38CB0` is the race option object, and a field there is read and written. It checks that every address lies inside guest RAM before reading. This is the template for a context probe: **a per-VBlank host read of a few guest words, with range checks, no guest code change, no translated-code hook.** It costs a few loads per frame.

A guest-side hook (as in the TASK-017 guard, `native/license_guard.cpp`, which sits on the translated call sites of the license loaders) is the other option. It needs a producer change, and the producer is pinned, so a host-side probe is the cheaper first route.

### 1.2 Candidates

| # | Candidate | Guest addresses | How to read it | Confidence | Risks |
|---|---|---|---|---|---|
| A | **Owner root** | word at `0x622F4C` (owner pointer); `owner + 0x38CB0` race option object | Host VBlank read, as in `race_display.cpp` | Pointer and option object: **Confirmed** (slice 164 doc, read in code). That it is the *game root* from which the race and menu objects hang: **Hypothesis** | Only a root. Its offsets to the race object are not known yet. |
| B | **Race object identity by vtable** | Race classes (vtable, Confirmed from headers / ELF): RaceSinglePlayer `0x682A20`; RaceArcade `0x67BFA8`; RaceArcadeSingle `0x67BAD8`; RaceArcadeDemo `0x67C520`; RaceGTmode `0x684678`; RaceFreeRun `0x680140`; RaceLicense `0x6807A8`; RaceSolitaire `0x683420`; RacePhotoDevelop `0x682310`; RacePhotoMode `0x681C08`; RacePS2Base `0x679378`. Vptr location varies by class (for PauseBase it is at +0x8). | Scan from the root to find the object whose vptr is one of these | **High confidence** that the vtables identify the race class. **Unknown** where the object pointer is stored. | Walk must be bounded (a few levels). A stale object during transitions still has a valid vptr, so the vptr alone does not prove the race is live (see C). |
| C | **Race running window** | `RaceBase::raceStart` slot 78 at `0x00389D48`; `raceEnd` slot 79 at `0x00389D98`; `RaceBase::update` slot 6 at `0x00389638`; `updateRace` slot 74 at `0x00389B08` | Hooks are virtual calls; a host read cannot see them. Use a per-frame probe that asks "race object present and its update ran this frame" | **High confidence** for the slots and addresses (decomp `src/race/RaceBase`, headers). The live-flag field is **Unknown**. | Race start/end run inside the guest, so the host can only infer them from the object appearing and disappearing. Expect a few frames of lag. |
| D | **Pause object** | `RaceBasic` field `+0xE418` (`unkE418`) = the active pause object pointer, set in `RaceBasic::pauseStart` `0x0038B428` and cleared in `pauseEnd` `0x0038B4B8`. The pause object's vptr is at `+0x8` (PauseBase). | Host read: `race + 0xE418`, then `*(obj + 0x8)` | **High confidence**: the write sites are in the decomp (`src/race/unit_0038B0E0/RaceBasic__pauseStart.cpp`, `RaceBasic__pauseEnd.cpp`); the vptr values are from headers. Runtime not tested. | The pointer is set before the pause flag, so a read between the two sees a half state. Stale only for one frame. |
| E | **Pause flag** | `RaceBase +0xCD8` (`unkCD8`), written by `RaceBase__setPause` (`src/race/RaceBase/RaceBase__setPause.cpp`, address `0x00389D30`). `RaceBasic__setPause` writes it only when `+0xE418 != 0`. | Host read `race + 0xCD8` | **High confidence** (decomp). Used together with D, not alone. | Same one-frame lag as D. Note `RaceBase__updatePause` (`0x00389D38`) is an empty function in the decomp. The pause logic lives in RaceBasic. |
| F | **Pause kind** | Pause vptrs: RacePause `0x681B28`; SimplePause `0x681B60`; PhotoPause `0x681B98` (PauseBase.h, vtable values from headers) | Read the vptr at `pause_obj + 8` | **High confidence** (headers generated from `config/classes.json`). | Needs the pause object (D). |
| G | **HUD drawn** (`render_panel`) | `RacePS2Base::render_panel` vt+0x358 at `0x0033FA40`; the 2D pass | Only from inside the guest render call | **High confidence** for the address (study). Not usable as a host read. | Drawn only while the race renders; a pause still draws the HUD under it, so it does not separate race from pause. Poor candidate. |
| H | **Replay** | `RaceBase::replayStart` slot 43 at `0x0038A090`; `RaceReplayInformation` (vtable `0x683080`); `RecordedSceneCamera` (camera vtable `0x6792D8`) | Unknown how to read it from the host. Possibly a replay flag in the race object | **Unknown**. The class and hook are known; the field is not. | Replays reuse race classes, so the vtable alone may say "race" when it is a replay. |
| I | **Menus, garage, showroom, loading** | Menus and the garage are **scripts** (`architecture.md`: "the game loop is in script"; `mManager.loadProject` switches menus). No race object is present. Showroom: `Concourse::render` `0x00408490`, `LicenseConcourse::render` `0x004096C0`. Loading: `GranTurismo4::LoadingPS2` (GameObject). | Absence of race object plus presence of a Concourse or Loading object, read from the GameObject list | Menus = no race object: **Hypothesis**. Concourse = showroom: **Hypothesis**. Loading object: **Hypothesis**. | The GameObject list head needs a root (A). Menu script state is not a simple pointer, so "menu" is a fallback ("not race, not pause, not concourse"), not a positive match. |
| J | **GameObject list** | `GranTurismo4::GameObjectManager::append` `0x00109478`; list is an `MTListManager` singleton (type info `0x0088D9A0`, instance **address Unknown**) | Walk `head` and read each `vptr`/`priority` under the list mutex | List structure **High confidence** (decomp `GranTurismo4__GameObjectManager__append.cpp`). Instance address **Unknown**. | Walking under the mutex in the host is unsafe unless the guest is paused; read without the lock, and tolerate a torn list. |
| K | **Input read point** | `RaceInput` vptr `0xD0`; `fetch` slot 1; `getButtons` `0x00426A00`; `getButtonDown` `0x00426A08`; `RaceInput` is the race's input object | Host side: the pad state that the guest reads is what `native/host_io.cpp` writes (`map_gamepad`, guest button bit index). | **High confidence** for the object and functions. | This is where the remap has to happen, not where context is read. |

### 1.3 Recommended detector (first candidate, to be proven)

1. At VBlank, read `owner = *(u32*)(ram + 0x622F4C)` with the existing range check (candidate A).
2. Find the race object by a bounded scan of the owner (level 1-2 pointers), accepting an object only when its vptr is in the race table above (candidate B). **This scan is the first experiment**: run it once at boot in a probe build, log the path and offsets, then hard-code them.
3. Race present = race object found. Pause = `race+0xE418 != 0` (D), with kind from F. Race class from B: RaceArcadeDemo = attract (treat as menu), RacePhotoMode = photo, RaceLicense/RaceSolitaire-family = license, RaceFreeRun / RaceGTmode / RaceArcade = race.
4. No race object and no pause = menu, showroom (Concourse object), or loading. Use the Concourse/Loading objects (I) when J is known; otherwise call it "not race".
5. Debounce: a context change is applied only after the same reading holds for N consecutive VBlanks (N = 3 at 60 Hz, a starting value to tune). Loading keeps the last context.

## 2. Question 2: which contexts can be told apart

| Context | Signal | Separable? | Confidence |
|---|---|---|---|
| Race (GT mode, arcade, free run / time trial, license) | race object present and not paused (B, C) | Yes, class-level: arcade (RaceArcade `0x67BFA8`), GT (RaceGTmode `0x684678`), free run (RaceFreeRun `0x680140`, time trial to be confirmed), license (RaceLicense `0x6807A8`) | Race vs non-race: **High confidence** once the root is found. Mode naming: **Hypothesis** (which class is which menu item needs in-game confirmation). |
| Time trial | probably RaceFreeRun | Hypothesis. Confirm by playing it (owner test). | Hypothesis |
| Pause menu (RacePause) | `+0xE418` and vptr `0x681B28` (D, F) | Yes | High confidence |
| Simple pause | vptr `0x681B60` | Yes | High confidence |
| Photo mode (in race, paused) | PhotoPause `0x681B98`, or RacePhotoMode `0x681C08` | Yes, but photo is also reached from pause; use the pause object kind | High confidence for the vtables, Hypothesis for the mapping |
| Replay | H | **Unknown** until the replay field is found. Replay can be recognised by the replayStart hook or by a replay object. | Unknown |
| Attract / arcade demo | RaceArcadeDemo `0x67C520` | Yes. Owner decision needed: treat as menu (recommended) or race. | High confidence that it is separable |
| Showroom | Concourse / LicenseConcourse object | Probably yes (Concourse render) | Hypothesis |
| Menus and garage | no race object, no Concourse, no pause | Only as "none of the above" | Hypothesis |
| Loading | LoadingPS2 object | Hypothesis | Hypothesis |

## 3. Question 3: proposed design

### 3.1 Context detector (host side, native)

- New `native/context_probe.{hpp,cpp}` (not in the guest). Reads guest RAM only through the existing range-checked helpers, at VBlank beside `force_race_480p_at_vblank`.
- Output: an enum `Context { Menu, Race, Pause, PhotoPause, Replay, Showroom, Loading, Unknown }`, with the debounce above.
- Pure functions for address math and the vptr table, so CTest can run fixtures with synthetic RAM (same pattern as `race_display_fixture.cpp`).
- Off-by-default logging: context transitions to the F8 probe (`perf_probe`), so the owner can check what the game sees.

### 3.2 `controls.toml` schema 2 (proposal)

Schema 1 (TASK-029, `docs/CONTROLS.md`) has one `[bindings]` table and `schema = 1` exactly. Schema 2 adds per-context tables; schema 1 files stay valid and mean "one context, all contexts use it".

```toml
schema = 2

[bindings]                       # default for every context; equals today's table
cross = ["xinput:A", "key:Z"]

[bindings.race]                  # overrides only the inputs listed; others fall back to [bindings]
start = ["key:Esc", "xinput:Start"]

[bindings.pause]
triangle = ["key:Esc", "xinput:B"]   # back in pause menus; Esc = Start in race

[bindings.menu]
triangle = ["key:Esc", "key:Backspace", "xinput:Y"]
```

Rules:
1. Context tables are optional. Missing inputs fall back to `[bindings]`, so **defaults equal today's behaviour** (no context table exists by default).
2. An empty list unbinds an input in that context only.
3. `Unknown` and `Loading` use `[bindings]`.
4. The parser must accept `[bindings.<context>]`. The current subset forbids dotted keys and extra tables, so the TOML rule set in `docs/CONTROLS.md` changes in schema 2. This is the main parser change.
5. The TASK-076 fixed keyboard (Esc = Triangle, Backspace = Triangle) is the default in `[bindings]`; TASK-077 only overrides Esc in `race`.

### 3.3 Held keys across a context switch (no stuck buttons)

- Output is recomputed each frame from the **current** context's table, as an OR of physically held sources.
- **Edge rule:** a logical button produces a press edge only when a physical source for it becomes held under the active context. When the context changes, every logical button that was pressed only through the old context's mapping is released that frame. This prevents a button held in race from staying stuck in the menu.
- **Re-arm rule:** a physical source held across a switch does not fire in the new context until it is released and pressed again. Without this, holding Esc from race into the menu would fire Triangle at once.
- **Pause hold:** on the frame a pause opens, all buttons are released (the guest sees a clean pad) and held sources are re-armed.
- **Loading:** no context change while loading; buttons are released to avoid a stuck input across a screen change.
- **Debounce interaction:** the debounced context is what selects the table, so a single bad frame does not change mapping.

Unit tests (CTest + Python fixture): release on switch, re-arm after switch, unbound-in-context, OR across sources, schema 1 unchanged, debounce N frames.

### 3.4 Launcher

- Controls page (TASK-019, `task/019-launcher`) gets a context selector: Default, Race, Pause, Menus. Each shows the same input list as today with the per-context override highlighted.
- Edits write `[bindings.<context>]`; unchanged inputs are not written (keeps files small and schema 1 readable).
- Live input uses the same SDL3 layer as the game (TASK-075).
- "What the game sees" (optional, later): the game writes its current context to a status line that the launcher reads. Low priority; the launcher cannot detect the context itself while hidden in the tray.
- The fixed keyboard layout (TASK-076) stays read-only in Keyboard view; context overrides apply on top of it.

## 4. Question 4: effort, risks, and what the owner must test

### 4.1 Effort (estimates, Hypothesis)

| Step | What | Size |
|---|---|---|
| S1 | Probe build: read `0x622F4C`, scan owner for race vtables, log the path and the pause object (no mapping change). Decide which candidate holds. | 1 slice, with the owner's race and pause tests. The main unknown. |
| S2 | `native/context_probe` with pure functions, fixtures, debounce | 1 slice |
| S3 | Schema 2 parser and per-context table in `controls.cpp`, release/re-arm rules, tests | 1 to 2 slices (depends on TASK-029 merge) |
| S4 | Launcher context editor | 1 slice (after TASK-019 is merged) |
| S5 | Owner in-game validation | owner time |

Depends on: TASK-029 merged to `main` (today it is on `task/019-launcher`), TASK-075 (SDL3 input layer, owner-approved 2026-10-21, not started), TASK-076 (fixed keyboard).

### 4.2 Risks

- **Root unknown:** candidate A (`0x622F4C`) is confirmed as a pointer but not yet as the root of the race object. If the scan fails, the next option is the guest-side hook (producer change, costly under pins).
- **Replay and loading:** no reliable host signal yet for replay; loading is inferred. Mis-detected replay would give the race mapping in a replay (low harm, but visible).
- **Torn reads:** the GameObject list is walked under a guest mutex. The host must not block on it; a torn list may show a missing object for one frame, which the debounce should absorb.
- **Thread safety:** the probe reads EE RAM while MTVU runs on its own thread. The reads are only of EE RAM; MTVU thread ownership must be confirmed in `native/mtvu_*` before the probe ships.
- **Pause timing:** the pause object is set before the pause flag and cleared at pause end. A VBlank read can see one half state; debounce covers it.
- **Esc conflict:** Esc is Triangle in menus (TASK-076) and Start in race (TASK-077). A player used to the fixed keyboard may be surprised, so the launcher must show the per-context mapping, not only the file.
- **Headless frame comparison:** the probe must not change frame output; TASK-075's acceptance ("headless frame dumps unchanged") applies here too.
- **No stuck buttons:** rules in 3.3 must be tested; a wrong rule is noticed only by owner testing.

### 4.3 What must be validated with the owner in game

1. Start a GT race, press Esc: pause opens (Start in race). Esc or Backspace in the pause: returns to the race.
2. Pause menu Triangle backs out; in menus, Esc = Triangle.
3. Enter photo mode from pause; confirm the context is photo (and the mapping of the photo controls).
4. Time trial (RaceFreeRun?), license test, arcade: confirm which vtable is which, and that all are race.
5. Replay: play a replay; check whether the race mapping applies (decides whether replay needs its own table).
6. Attract screen: is Esc Start there or Triangle? (owner decides; recommend menu).
7. Showroom and garage: confirm menu behaviour.
8. Loading screen: no stuck buttons when a race starts or ends while a key is held.
9. Hold Esc through a pause open/close and a race start: no double press, no stuck Start.
10. Launcher: edit a race override, save, start the game, check the new key works only in race.

## 5. Open unknowns (in priority order)

1. Which field under `0x622F4C` reaches the race object (S1 scan).
2. Whether there is a race "running" flag or only the object's presence.
3. Replay field.
4. Address of the `GameObjectManager` instance (needed to walk the list instead of the owner scan).
5. Whether the host read is safe while MTVU runs (thread model).
6. Which vtable is time trial.

## 6. Recommendation

Do S1 first, as a probe build with no mapping change and logging only. It is cheap, it uses the proven `race_display` pattern, and it answers the root question. Hold TASK-077 implementation until TASK-029 is merged to `main` and TASK-075 lands. The owner decides the attract-screen rule and whether replays get their own table.
