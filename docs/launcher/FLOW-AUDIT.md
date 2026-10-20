# Launcher flow audit (TASK-019)

2026-10-20, branch `task/019-launcher`. Scope: the whole launcher flow before the installer build, end to end,
with the owner's real discs (read-only) and the real gt4-gs release in a sandbox data folder. The game itself
(`gt4vulkan.exe`) was never started: Play was checked up to its launch plan.

Agent status: not PCSX2-tainted (no PCSX2 sources or fork were opened; the gt4-gs release zip was listed, not run).

## 1. Flow map

```
GT4Recomp.exe start
  single instance? (no: bring the first window forward, exit)
  data folder = GT4RECOMP_DATA_DIR or %LOCALAPPDATA%\GT4Recomp
  AppState::load: settings.toml (migrations), controls.toml, save library, pins
  background thread check_on_launch: version.json; gt4-gs retry install (only when set up); update check
  page boots -> app_state
      |
      +-- setup.state != done --> #/setup
      |      needed ......... "Choose disc image"
      |      wrong_disc ..... reason + "Choose another file"
      |      failed ......... reason + "Try again" / "Choose another file"
      |      files_missing .. "Game files missing" + "Choose disc image"
      |      checking ....... copying -> checking -> preparing -> renderer (bars, bytes, Cancel)
      |      done ........... "Ready" + Play (note when CoreGS 1.1 is used)
      |
      +-- setup.state == done --> #/home (slideshow, logo, Play, gear)
             Play -> prepare_play -> game::start -> tray hides the window
             game ends -> card harvested -> window back on Home with any message
             Settings: Graphics, Advanced, Display, Controls, Saves, Replays, About
```

### Screens, commands and events

| Screen | Commands it calls | Events it hears |
|---|---|---|
| Boot (`app.js`) | `app_state` | `update-available`, `close-confirm`, `update-finished`, `renderer-progress` (toasts outside setup), `game-state` |
| Setup | `app_state` (polled every 400 ms), `setup_choose_disc`, `setup_start`, `setup_cancel`, `play` | (polls; `setup-progress`, `setup-finished` and `renderer-progress` are also emitted) |
| Home | `app_state` (game_running), `about_info` (version label), `play`, `stop_game` | `game-state` |
| Settings: Graphics, Advanced, Display | `get_settings_table`, `set_setting`, `reset_gs_settings_to_defaults` | |
| Settings: Controls | `get_controls`, `capture_binding`, `cancel_capture`, `reset_binding`, `set_setting` (triggers) | `gamepad` (optional) |
| Settings: Saves, Replays | `saves_list`, `saves_import`, `saves_activate`, `saves_delete`, `replays_list`, `replays_import`, `replays_delete` | |
| Settings: About | `about_info`, `check_updates`, `open_game_files_folder`, `open_log_folder` | |
| Update popup | `apply_update`, `skip_update` | `update-available`, `renderer-progress`, `update-finished` |
| Close confirmation | `confirm_close` | `close-confirm` |

`src/contract.rs` now checks both directions: every name a page calls is registered in `main.rs`, and every
registered command is called by a page (27 commands).

### Files written (all under the data folder unless noted)

| File | Written by | When |
|---|---|---|
| `disc\SCUS-97328.iso` (`.iso.tmp` while copying) | setup | once; the game reads only this copy |
| `prepared\CORE.GT4`, `prepared\SCUS_973.28`, `prepared\prepare.json` (`prepared.tmp` while staging) | setup | once; the receipt is the commit point |
| `gs\<version>\...`, `gs\current.txt` (`gs\.staging-<v>` while unpacking) | setup, launch retry, update | first install and updates |
| `version.json` | launcher | next to `GT4Recomp.exe` (or the data folder when that is read-only) |
| `config\settings.toml` (+ `.bak-schema<N>-<t>`) | settings, setup (`disc_path`, skipped updates) | on change |
| `controls.toml` | controls | on change |
| `library\...`, `runtime\card.ps2` | saves | import, activate, delete, Play, game end |
| `sessions\session-<t>\` (`game.log`, `misses.txt`, `gt4-gs.ini`) | Play | per session, 20 kept |
| `logs\launcher.log` | everything | appended |

Protected by the data-path guard (installs and updates refuse to write there): `saves`, `cards`, `library`,
`replays`, `runtime` and now `disc`, plus card and save extensions. The uninstaller removes the data folder only when
the player ticks its unchecked "also delete" box.

## 2. Disc rule (what is accepted)

**Rule:** the game code decides. A disc is accepted when

1. it is a plain 2048-byte-sector `.iso` that can be opened;
2. its `SYSTEM.CNF` boots `cdrom0:\SCUS_973.28;1` (serial SCUS-97328);
3. its `CORE.GT4` is byte-identical to the pinned one (size 2,020,861, SHA-256 `85d26aa8...2ff9`), so the rebuilt ELF
   is exactly the code the game executable was compiled from (the rebuild is verified again against the ELF pin,
   SHA-256 `10f82e22...935c`);
4. the image is not shorter than its own volume size;
5. **if** it claims the qualified version (`VER = 2.00`), its size and full SHA-256 must also equal the v2.00 disc
   pin (5,314,478,080 bytes, `67b6c007...824f`). A v2.00 image with any other byte is a damaged or patched dump and is
   refused.

Another revision whose `VER` is not 2.00 but whose `CORE.GT4` is identical passes; its receipt records
`qualified_dump: false`. A SCUS-97328 disc with different game code is refused as "not supported yet".

**What the check compares (before this audit):** the extension, `SYSTEM.CNF` serial and boot path, `VER == 2.00`,
the exact disc size, then the whole-image SHA-256, then CORE and ELF. Any other revision was refused by `VER`.

**Why it is safe:** the executable is compiled from the EE code that `CORE.GT4` holds; the launcher rebuilds the ELF
from the disc's own CORE and checks it against the pin, so a passing disc runs exactly the code we ship. The rest of
the disc (GT4.VOL, IRX modules, IOPRP) is data the same code shipped with on that revision; the IRX modules are
interpreted, not compiled.

**Evidence:**

- Confirmed: the owner's `Gran Turismo 4 (USA) (v2.00).iso` passes (`SYSTEM.CNF` `VER = 2.00`, volume `GRANTURISMO4`,
  CORE SHA-256 equal to the pin, full image hash equal to the pin; real run below).
- Confirmed: gt4-decomp labels its target "SCUS-97328 v1.01" and publishes the same `CORE.GT4` SHA-1
  `30031137ce49...37d2` as ours (`docs/DECOMP-IDENTITY.md`). So a v1.01 disc with identical game code exists.
  High confidence that such a disc passes this rule; Unknown until a real v1.01 image is tried (none is on this
  machine).
- Confirmed: `Gran Turismo 4 Spec II v1.10.iso` boots `SCUS_974.36` (GT4 Online's serial), `VER = 1.00`, volume
  `GRANTURISMO4`, and has no `CORE.GT4`. It is refused with "This disc is not supported yet. GT4Recomp supports Gran
  Turismo 4 (USA, SCUS-97328). This looks like Gran Turismo 4 Spec II or GT4 Online (SCUS-97436)." (The old check
  looked for "SPEC" in the volume name, which Spec II does not have, so it said "This is not Gran Turismo 4 USA".)
- Note: the v2.00 image is dual layer; its primary volume descriptor covers layer 0 only (2,697,330,688 bytes). The
  volume-size check is therefore a lower bound, and the exact size check applies to the qualified version only.

Messages (all refusals start with the same sentence when the disc is a PS2 game but not ours):

| Case | Message |
|---|---|
| Spec II / GT4 Online | This disc is not supported yet. GT4Recomp supports Gran Turismo 4 (USA, SCUS-97328). This looks like Gran Turismo 4 Spec II or GT4 Online (SCUS-97436). |
| PAL / Asian | ... This disc is SCES-50001, another region. |
| SCUS-97328 with other game code | ... This SCUS-97328 disc (version X) has different game code. |
| Another game | ... This disc is SCUS-97100. |
| Not an ISO | This file is not a plain 2048-byte-sector .iso image. |
| .bin / .chd | Please use a plain .iso file ... / This image is compressed (.chd) ... |
| Missing file | That file could not be opened. Check that it is still there and try again. |
| Truncated | This copy is incomplete or modified. Please re-dump your disc. |
| v2.00 with a changed byte | This dump differs from the known good v2.00 disc (damaged or patched). Please re-dump your disc. |
| Low space | Not enough free space to copy the game files: X GB needed, Y GB free. Free up space and try again. |

## 3. The copy of the disc image (owner, 2026-10-20)

The game reads the disc at run time (`--disc <iso>`; GT4.VOL streaming), so setup now copies the image into
`<data>\disc\SCUS-97328.iso`. The copy is always an independent file (never a hard link). Free space is checked
first (image size plus 512 MiB); the copy goes to `SCUS-97328.iso.tmp` and is hashed while it is written (that pass is
the full check); on a match it is renamed into place, then the prepared folder is swapped in with a schema 2 receipt
that records the copy's size, modified time and SHA-256. Cancel or any failure removes the temporary copy and the
staging folder. Every later launch compares only size and modified time (no hash): the second launch took 1-2 ms.
If the copy is missing or changed, the launcher opens Setup with "Game files missing. Choose your disc image again."
and never starts setup by itself. About shows the folder with an "Open folder" button.

## 4. Checklist

PASS = worked as specified when tested. FIXED = a bug found in this audit, fixed and tested. FAIL = still broken.
NEEDS-OWNER = needs a person (real pad, touch screen, Play, native file picker).

| # | Step | Result | Evidence |
|---|---|---|---|
| 1 | First launch opens Setup, not Home | PASS | real exe with an empty sandbox: `audit-tauri-first-launch-setup.png`; `synthetic_flow_end_to_end` |
| 2 | Setup cannot be skipped (no Home until done) | PASS | `app_state` says `done` only when the receipt checks; Home is routed only on `done` |
| 3 | Setup page offers "Choose disc image" | FIXED | before: the page called `setup_start` at once with no disc chosen (error toast) and had no button to choose a disc in the first state; the backend never reported `wrong_disc` |
| 4 | Not an ISO refused with a clear message | PASS | real run: "This file is not a plain 2048-byte-sector .iso image." |
| 5 | Missing file refused with a clear message | FIXED | before: "Preparing the game files failed ... report this with the log"; now "That file could not be opened ..." |
| 6 | Truncated copy refused | PASS | real 64 MiB truncation: "This copy is incomplete or modified." |
| 7 | Spec II refused with "not supported yet" | FIXED | before: "This is not Gran Turismo 4 USA (SCUS-97328). The disc says SCUS-97436." (volume name has no "SPEC") |
| 8 | v2.00 accepted | PASS | real run, `qualified_dump: true` |
| 9 | Disc rule: game code decides (other revision with identical CORE passes; other code refused) | FIXED | before: any `VER` other than 2.00 refused. Tests `another_revision_with_the_same_game_code_is_accepted`, `..._different_game_code_is_refused` |
| 10 | Damaged v2.00 refused (CORE byte; other byte after the copy) | PASS | `truncated_copy_..._changed_core_...`, `a_hash_mismatch_after_the_copy_leaves_nothing_behind` |
| 11 | Image copied into the data folder, independent file | FIXED (new) | real copy 5,314,478,080 bytes in about 35-75 s; `the_copy_is_an_independent_file_not_a_link` |
| 12 | Free space checked before the copy | FIXED (new) | `low_free_space_is_refused_before_copying`; `gt4l-platform::disk::free_space` (GetDiskFreeSpaceExW) |
| 13 | Cancel leaves nothing half-written | PASS | `cancel_leaves_nothing_half_written`; flow test cancel |
| 14 | Setup progress drawn (copy bytes, percent) | FIXED | before: `app_state.progress` was always 0, the page never listened to `setup-progress`, and the `.bar` was an inline span with no height (never visible). Screenshots `audit-setup-copying.png` |
| 15 | Renderer download drawn on the setup page | FIXED | known gap: now a setup step with listing / downloading (bytes) / installing; byte progress via `ReleaseSource::download_reporting`. `audit-setup-renderer.png` |
| 16 | Real gt4-gs 1.0.1 installs | FIXED | before: "the package has no gt4-gs.exe" (the real zip wraps everything in `gt4-gs-1.0.1-windows-x64/`). Now the wrapping folder is removed on unpack; test `a_package_wrapped_in_one_folder_installs_like_the_real_release`. Real run: `gs\current.txt` = 1.0.1, `gs\1.0.1\gt4-gs.exe` + DLLs + `resources\` |
| 17 | SHA256SUMS verified | PASS | the package line is checked (the real SHA256SUMS has no `gt4-gs.exe` line; the exe hash is recorded at install) |
| 18 | version.json records gt4-gs | PASS | `{"schema":1,"gt4recomp":"1.0.0","gt4gs":"1.0.1","settings_schema":1}` next to the exe |
| 19 | Renderer install at launch did not race setup | FIXED | before: the launch thread installed gt4-gs before setup, unseen, and `setup_start` started a second install in parallel; now setup owns the first install and launch only retries after an offline setup |
| 20 | Offline renderer: setup still finishes, CoreGS 1.1 note | PASS | flow test `offline_renderer`; `audit-setup-done-offline-960x600.png` |
| 21 | Setup ends on "Ready" with Play | PASS | real run `state: done` |
| 22 | Setup screen not replaced by Home before the renderer step ends | FIXED | before: `app_state` said `done` as soon as the prepared folder existed, while the renderer still downloaded |
| 23 | Later launch opens Home directly, no re-verification | PASS | real exe second launch: `audit-tauri-second-launch-home.png`; `app_state` done in 1-2 ms |
| 24 | Picked disc moved or deleted after setup: still Home | FIXED (new) | flow test deletes the picked file; Play uses `disc\SCUS-97328.iso` |
| 25 | Copy missing later: clear way back | FIXED (new) | real exe: `audit-tauri-files-missing.png`; Play refuses with "Game files missing" and Home routes to Setup |
| 26 | Old schema 1 receipt | PASS | treated as missing game files, one copy; `docs/launcher/MIGRATIONS.md` |
| 27 | Settings table, set, read back, reset | PASS | real run: 35 rows, fxaa round trip, reset |
| 28 | Unknown setting refused | PASS | flow test |
| 29 | Saves: import a test card, no duplicate on re-import | PASS | real run: 0 -> 1 ("Added Save 01."), second import keeps 1 |
| 30 | Replays list | PASS | real run: 0 items |
| 31 | Controls list and reset | PASS | real run: 16 inputs, cross reset |
| 32 | About: versions, game files folder and "Open folder" | FIXED (new) | `audit-settings-about.png`; the stale "Disc image" path row was removed |
| 33 | check_updates | PASS | real run: 0 offers, no errors, gt4-gs 1.0.1 installed (GT4Recomp has no release yet) |
| 34 | Play command line and environment | PASS | plan: `<data>\prepared\SCUS_973.28 --ntsc --disc <data>\disc\SCUS-97328.iso --frames 0 --memcard <data>\runtime\card.ps2 --misses <session>\misses.txt --window`; `GT4_RENDERER=pcsx2`, `GT4_GS_PROCESS`, `GT4_GS_PROCESS_SHA256`, `GT4_GS_CONFIG`, inherited `GT4_*` removed |
| 35 | UI command contract (both directions) | FIXED | new test; it found `open_game_files_folder` unused until About called it |
| 36 | nav-test | PASS | 55 of 55 in headless Edge |
| 37 | CSP: no inline style or script | PASS | progress widths use CSSOM (`el.style.width`), allowed by `style-src 'self'` |
| 38 | 48 px targets | FIXED | the Home "Stop" button was 40 px high; now 48 px |
| 39 | Setup fits 960 x 600 without scrolling to its buttons | FIXED | a 960 x 600 rule hides the duplicate wordmark and tightens rows; problem states show only the Disc row |
| 40 | Real Tauri exe builds and closes normally | PASS | `cargo tauri build --no-bundle`; WM_CLOSE, exit code 0, three starts |
| 41 | Tray hide on Play, restore on exit | NEEDS-OWNER | state machine tests pass (8); live check needs Play |
| 42 | Update popup live (Update / Not now / Skip) | NEEDS-OWNER | mock screenshot only; no newer release exists |
| 43 | Native file picker | NEEDS-OWNER | the harness injects the path into the same function |
| 44 | Real pad (A select, B back), L2/R2, touch | NEEDS-OWNER | |
| 45 | Play (the game itself) | NEEDS-OWNER | never started here |

Counts: PASS 23, FIXED 17 (5 of them new behaviour asked for during the audit), FAIL 0, NEEDS-OWNER 5.

## 5. How it was tested

- `cargo test --workspace` (186 passed, 3 ignored).
- `gt4l-tauri/src/flow_audit.rs::synthetic_flow_end_to_end` (every run): the command bodies in the page's order with a
  synthetic disc: refusals, accept, setup with recorded events, second launch, settings, saves, controls, about, the
  Play plan, the copy deleted, an offline renderer, cancel.
- `real_flow_end_to_end` (ignored; run once with the real discs, a real 5.3 GB copy and the real gt4-gs download in
  `%TEMP%\...\scratchpad\launcher-audit\data-real`). The command lines are in the module comment.
- `gt4l-disc/tests/real_disc.rs::real_discs_pass_or_fail_the_precheck` (ignored): v2.00 and Spec II.
- The real launcher: `cargo tauri build --no-bundle`, started three times with `GT4RECOMP_DATA_DIR` set to a sandbox
  folder (empty: Setup; after setup: Home; copy renamed away: Game files missing), window captured with PrintWindow,
  closed with WM_CLOSE.
- The dev mock in headless Edge with `--force-prefers-reduced-motion` for every setup state and the settings pages.

Screenshots (`docs/launcher/shots/`): `audit-tauri-first-launch-setup.png`, `audit-tauri-second-launch-home.png`,
`audit-tauri-files-missing.png`, `audit-setup-needed.png`, `audit-setup-not-supported.png`, `audit-setup-copying.png`,
`audit-setup-copying-960x600.png`, `audit-setup-preparing.png`, `audit-setup-renderer.png`,
`audit-setup-failed-space.png`, `audit-setup-files-missing.png`, `audit-setup-done-offline-960x600.png`,
`audit-setup-needed-844x390.png`, `audit-home.png`, `audit-settings-graphics.png`, `audit-settings-about.png`,
`audit-settings-saves.png`, `audit-settings-controls.png`.

## 6. Limits and open items

- No real v1.01 (or other revision) image was available, so rule 3's acceptance of another revision is tested with
  synthetic images only.
- The full hash of the copy is the hash of the bytes written; the copy is not read back a second time.
- At 844 x 390 (phone landscape) the setup steps scroll; the A hint and the scroll reach the button.
- The installer was not built (NSIS is not installed); its uninstall text now names the disc copy.
- The native file picker, tray, update popup, pad, touch and Play need the owner.
