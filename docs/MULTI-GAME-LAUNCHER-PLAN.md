# Multi-game launcher: Gran Turismo 4 and Spec II (TASK-101, release 1.3.0)

Owner brainstorm 2026-10-23. The launcher installs and runs two games side by side: Gran Turismo 4 v2.00 (retail,
SCUS-97328) and Gran Turismo 4 Spec II 1.10 (SCUS-97436 lineage, TASK-100). This is the product side of TASK-100
(its subtask S9) and ships with it in release 1.3.0, on GSdx.

## Owner decisions

| Topic | Decision |
|---|---|
| Executables | One per game: `game\gt4vulkan.exe` (v2.00) and `game\gt4vulkan-spec2.exe` (Spec II). The generated code of the two games shares symbol names, so they cannot share one executable (decision 0072). Both ship in every installer and update. |
| Settings | One shared set for both games: graphics, GSdx options, controls, textures, display. |
| Per game | The disc copy, the prepared files, the runtime card, the save library and the replays. |
| Saves | Never shared or converted. Spec II does not accept a v2.00 save and v2.00 does not accept a Spec II save (owner). Spec II writes under the same PS2 folder name as retail, `BASCUS-97328GAMEDATA` (found in both executables), so a shared card would overwrite the other game's save. |
| Replays | Separate per game. Cross-compatibility is open; test it later. |
| First install | The player picks an ISO; the launcher recognises the game from it and installs that game. |
| Selected game | The game installed first is selected at the start; after that, the game played last. |
| Accepted discs | Spec II 1.10 only for now, plus variants of 1.10 that keep the same executable (for example the Brazilian Portuguese translation of 1.10, which only replaces the Spanish language data). |
| Logo | With Spec II selected, the home screen shows the Spec II logo instead of the GT4 logo (the authors' logo: the GT4 logo with "Spec II" under it, in the GT4 font). |
| Shortcuts | One desktop and Start-menu shortcut, always to the launcher (as today). |

## User flows

1. **First start, nothing installed.** The setup asks for a disc image. The launcher identifies it:
   - v2.00 → installs Gran Turismo 4.
   - Spec II 1.10 or a known variant → installs Spec II.
   - Anything else → the existing "This disc is not supported" message, which names what it found (serial, title).

   The installed game becomes the selected game.
2. **Home screen.** A game selector, "Gran Turismo 4" / "Gran Turismo 4 Spec II", near Play, keyboard and gamepad
   friendly. Selecting a game swaps the logo.
   - Installed game: **Play**.
   - Game not installed: **Install** in place of Play. It opens the ISO picker and runs the normal setup for that game.
3. **Wrong disc for the chosen game** (for example the v2.00 ISO under "Install Spec II"): the launcher says which
   game the disc is and offers to install it as that game, or to pick another file.
4. **Both installed.** The selector switches between them; Play starts the selected game's executable with that
   game's disc, prepared files and card. The choice is stored as "last played" when the game starts.
5. **Settings > Games** lists both games: installed or not, disc identity, size on disk. Actions:
   - **Install**;
   - **Change disc image** (a new copy, checked again);
   - **Remove**: deletes the disc copy and the prepared files of that game, never its saves, and asks first.
6. **Saves page.** It shows the selected game's library, with the game named in the title. Import and export work per
   game. An imported `.psu` or card whose game cannot be told from its content asks which game it belongs to; it never
   guesses silently (see the open study below).
7. **Updates.** One launcher release updates both executables; gt4-gs is shared.

## Folder layout

| What | Today (1.2.x) | Multi-game |
|---|---|---|
| Disc copy | `data\disc\SCUS-97328.iso` | `data\games\gt4\disc\...`, `data\games\spec2\disc\...` |
| Prepared files | `data\prepared\` | `data\games\gt4\prepared\`, `data\games\spec2\prepared\` |
| Runtime card | `data\runtime\card.ps2` | `data\games\<id>\runtime\card.ps2` |
| Save library and backups | `%APPDATA%\GT4Recomp\Saves`, `\backups` | `%APPDATA%\GT4Recomp\games\<id>\Saves`, `\Replays`, `\backups` |
| Shared | `data\gs`, `data\Textures`, `data\logs`, `data\sessions`, `%APPDATA%\GT4Recomp\config` | unchanged |

- **Migration from 1.2.x:** at the first start, the v2.00 files move into `games\gt4`. On the same volume this is a
  rename, so it is instant. Use the TASK-091 relocation code: markers, resume, nothing deleted before it is verified.
  The existing save library becomes the v2.00 library.
- **The game ids** (`gt4`, `spec2`) are used in folder names, settings and logs; the display names are only in the UI.
- **The portable build (TASK-098) and the self-sufficient game (TASK-097)** use the same layout. `gt4vulkan-spec2.exe`
  started on its own finds `data\games\spec2\` from its own folder.

## Disc identification
- `gt4l-disc` gains a game table:
  - v2.00 is identified by its pinned full-image hash (as today).
  - Spec II is identified by the **executable**: the serial SCUS-97436 plus the pinned hash of its `SCUS_974.36`, which our derived ELF and plugin map depend on, plus a list of known ISO hashes with names (1.10; 1.10 PT-BR once pinned).
- A disc with a known Spec II executable but an unknown image hash is accepted as "Spec II 1.10 variant" and logged. A
  different executable is refused: it would need its own patch map and translation.
- The unmodified GT4 Online Public Beta is not offered as a game (same serial, different executable).
- `Unsupported::SpecII` is retired.

## Implementation pieces (in order)
1. **Game model** (`gt4l-core`): a `Game` table (id, display name, serial, executable name, prepared ELF name
   `SCUS_973.28` / `SCUS_974.36`, disc identity rules, logo asset). `DataDirs`/`UserDirs` take a game id. Settings gain
   `selected_game`, which is "last played" and set to the first installed game at the first setup.
2. **Disc and setup** (`gt4l-disc`): identification as above; the Spec II setup copies the ISO and prepares the derived
   ELF (TASK-100 S2 logic, ported to Rust or called as a pinned step), with no CORE step.
3. **Launch** (`launch.rs`, `game.rs`): the plan picks the executable, ELF, disc and card of the selected game. GSdx
   and all shared settings are unchanged.
4. **Saves** (`gt4saves`): one library per game, with separate harvest and build of each runtime card.
5. **Migration** of the 1.2.x layout into `games\gt4` (relocate.rs patterns).
6. **UI:** the home selector, Play/Install, the logo swap, Settings > Games, the game name on the Saves page.
7. **Installer and updates:** both executables in `game\`; the uninstaller removes `data\games\*`; settings, saves and
   the TASK-094 shortcut stay as today.
8. **Credits:** the Spec II authors in the About page, the README and the release notes; their permission before the
   public release (decision 0072).

## Open points
- **Save identification study:** is there a field or size in the GAMEDATA files that tells v2.00 from Spec II? If yes,
  imports are sorted automatically; if not, the import asks.
- **The Spec II logo:** get the authors' logo file and their permission; it ships as a launcher asset with credit.
- **Replays:** test whether either game plays the other's replays; until then they stay separate.
- **The PT-BR variant of 1.10:** pin its ISO hash once the owner provides it, and confirm the executable is identical.
- **GT mode in Spec II** needs game data on the card: confirm that a fresh Spec II card created in normal play works
  (TASK-100 S7b note).

## Checks
- Cargo tests for the game table, identification (v2.00, Spec II 1.10, an unknown variant with a known executable,
  a refused executable), the per-game folders, the migration (same volume, other volume, resume), last-played
  selection, and per-game save libraries that never mix.
- UI mock: the selector, Play/Install, the logo swap, Settings > Games.
- Owner test: a fresh install with each disc first, adding the second game, switching, saves kept apart, and an update
  from 1.2.x with the existing v2.00 data moved.
