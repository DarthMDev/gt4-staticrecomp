# GS settings table and gt4-gs.ini (TASK-039)

Status (2026-10-16): main-repo part done on `task/039-gs-settings`; all rows resolved from the PCSX2 v2.8.2 sources
(commit `fd9d310c`). Fork allow-list enforcement in `gt4-gs.exe` is pending (TASK-037/038 agent). Design source:
[GSDX-PLAN](GSDX-PLAN.md) sections D.1 and D.2 (see "Corrections to the plan" below).

## What it is

`docs/launcher/gs-settings.toml` is the single source for the PCSX2 GS options that GT4Recomp exposes. Each row
is one setting: launcher id, label, help, page (Graphics, Display, Advanced), INI section and key, type, range or
choices, GT4 default, visibility, when it is written, status, notes and sources (file:line at v2.8.2). The table has
50 rows.

`scripts/gs_settings.py` (stdlib and `tomllib`, Python 3.11 or newer) does four things:

- `load_table()` validates the table: schema version, unique ids and INI keys, pages, types, statuses and ranges.
  A broken table is refused with every error listed.
- `settings_from_assignments()` and `settings_from_json()` check user values. Unknown ids, unknown choices,
  values out of range, non-settable rows and launcher-only rows are refused with an error that names the row.
- `render_ini()` writes gt4-gs.ini text. Sections come in a fixed order (`EmuCore/GS`, `EmuCore`, `Folders`,
  `GT4GS`); rows follow the table order. Rows with `written = "always"` appear with their GT4 default. Rows with
  `when-set` appear only when a user sets them, so the default file stays the same as the TASK-036 file.
- `write_ini_atomic()` writes a temporary file in the same folder and then calls `os.replace`. A failed write
  leaves the old file in place and removes the temporary.

`allowlist()` emits the INI section and key pairs as `docs/launcher/gs-settings-allowlist.json`. The fork can
embed this file later and refuse keys outside it. Every row with a known key is included, whatever its status.
Launcher-only rows and rows without a key are not.

## Columns

| Column | Meaning |
|---|---|
| `id` | launcher identifier, used by `--gs-setting ID=VALUE` |
| `page`, `label`, `help` | where and how the setting is shown |
| `section`, `key` | INI section and key (empty for launcher-only rows) |
| `type` | `bool`, `int`, `float`, `enum`, `string`, or `path` (a folder the launcher supplies) |
| `min`, `max`, `choices` | range for numbers; `{name, ini, status}` entries for enums |
| `default` | GT4 default (launcher name for enums; `true` or `false` for bool) |
| `visibility` | `basic`, `advanced`, or `hidden` |
| `written` | `always`, `when-set`, `launcher` (supplied by the launcher), or `never` (launcher-only) |
| `status` | see below |
| `env` | launcher-only rows: the environment variable the game reads |
| `notes` | sources (file:line at v2.8.2) and open points |

Statuses:

- `ok`: settable. The key and values are confirmed in the sources and the owner accepts the row.
- `resolved`: the key and values are confirmed in the PCSX2 v2.8.2 sources, but the row is not settable yet. The
  owner checks it in the game menus or the GS, then it becomes `ok`. Until then `--gs-setting` refuses it and the
  launcher does not offer it.
- `pending-TASK-NNN`: depends on another task (a reader, a texture task or a launcher decision).

A value is settable only when its row and its choice both have status `ok`. The launcher offers only rows that are
basic or advanced, settable and have at least one settable choice (`gs_settings.offered()`).

## Defaults

With no options the generated file is the TASK-036 file:

```
[EmuCore/GS]
Renderer = 14                      (Vulkan)
upscale_multiplier = 3             (3x internal resolution)
accurate_blending_unit = 3         (blending accuracy High)
deinterlace_mode = 0              (deinterlace Automatic)
AspectRatio = 16:9
VsyncEnable = true
DisableMailboxPresentation = true
```

Owner decisions in the table: 3x default; readbacks accurate by default with Unsynchronized as a speed option
(`hw_download_mode`, shortcut `--gs-readback`, settable since TASK-037); blending accuracy High; deinterlace Automatic; 16:9; Vulkan; the
launcher shows only options that really work; FXAA is offered; CAS is offered once the owner has checked it in game.

## Values resolved from PCSX2 v2.8.2

The INI facts below are interface facts (key names and value numbers), not code. Each row's `notes` has the
file:line citations.

| Launcher row | INI key | Values (INI) | Default |
|---|---|---|---|
| `renderer` | `Renderer` | Vulkan 14, D3D12 15, D3D11 3 (D3D resolved, not offered) | Vulkan 14 |
| `filter` | `filter` | Nearest 0, Forced 1, PS2 2, Forced but sprite excluded 3 | PS2 (2) |
| `trilinear` | `TriFilter` | Automatic -1, Off 0, PS2 1, Forced 2 | Automatic |
| `anisotropic` | `MaxAnisotropy` | 0, 2, 4, 8, 16 | 0 |
| `blending` | `accurate_blending_unit` | Minimum 0, Basic 1, Medium 2, High 3, Full 4, Maximum 5 | High (3) |
| `dithering` | `dithering_ps2` | Off 0, Scaled 1, Unscaled 2, Force 32 bit 3 | Unscaled (2) |
| `texture_preloading` | `texture_preloading` | None 0, Partial 1, Full 2 | Full |
| `deinterlace` | `deinterlace_mode` | Automatic 0, Off 1, Weave TFF/BFF 2/3, Bob TFF/BFF 4/5, Blend TFF/BFF 6/7, Adaptive TFF/BFF 8/9 | Automatic |
| `linear_present` | `linear_present_mode` | Off 0, Smooth 1, Sharp 2 | Smooth |
| `cas_mode` | `CASMode` | Off 0, Sharpen only 1, Sharpen and resize 2 | Off |
| `cas_sharpness` | `CASSharpness` | 0 to 100 | 50 |
| `tv_shader` | `TVShader` | None 0, Scanline 1, Diagonal 2, Triangular 3, Wave 4, Lottes CRT 5, 4xRGSS 6, NxAGSS 7 | None |
| `shade_boost` | `ShadeBoost` | bool | off |
| `shade_boost_*` | `ShadeBoost_Brightness`, `_Contrast`, `_Gamma`, `_Saturation` | 1 to 100 | 50 |
| `hw_download_mode` | `HWDownloadMode` | Accurate 0 (Enabled), EnabledForceFull 1, NoReadbacks 2, Unsynchronized 3, Disabled 4 | Accurate (0) |
| `osd_messages_pos` | `OsdMessagesPos` | None 0, Top left 1 ... Bottom right 9 | Top left (1) |
| `osd_scale` | `OsdScale` | 25 to 500 (percent) | 100 |
| `fmv_aspect` | `FMVAspectRatioSwitch` | Off, Auto 4:3/3:2, 4:3, 16:9, 10:7 (names, not numbers) | Off |
| `adapter` | `Adapter` | empty (default) or a name from `GSGetAdapterInfo` | empty |
| `user_hacks` | `UserHacks_*` (names only) | see the row notes | off |

Readback mapping for the launcher: `--gs-readback` (`GT4_GS_READBACK`) is a shortcut for `hw_download_mode`:
accurate = `HWDownloadMode` Enabled (0, written by default); unsynchronized = Unsynchronized (3). A shortcut and
`--gs-setting hw_download_mode=...` for the same row conflict.

Adapter: the launcher cannot list adapter names until the fork exposes them at run time, so `adapter` stays
`resolved` (string rows cannot be `ok`).

`UserHacks_*`: the names are recorded in the `user_hacks` row notes. Each name is a hidden per-key row for
TASK-043. They take effect only with `manual_hacks` (`UserHacks = true`).

## Corrections to the plan

Reading the sources showed that some key names in [GSDX-PLAN](GSDX-PLAN.md) (D.1) are not the names PCSX2 loads.
The table uses the loader names (pcsx2/Pcsx2Config.cpp, v2.8.2). The plan itself is not edited.

| Plan name | Loader name | Note |
|---|---|---|
| `Dithering` | `dithering_ps2` | |
| `HWMipmap` | `mipmap` | |
| `FXAA` | `fxaa` | |
| `ManualUserHacks` | `UserHacks` | |
| `PCRTCAntiBlur`, `PCRTCOffsets`, `PCRTCOverscan` | `pcrtc_antiblur`, `pcrtc_offsets`, `pcrtc_overscan` | |
| `FMVAspectRatioSwitch` | enum of aspect names | was a bool in the table |
| `ShadeBoost*` | `ShadeBoost` (bool) and four level keys | the table now has one row per key |

The correction changes the allow-list. A key that is wrong in the table would be refused or ignored by the fork.

## Using it

Launcher (dev script now, TASK-019/043 later): `scripts/play_performance.py --mode pcsx2`

- `--gs-scale N` sets the upscale row (1 to 8, default 3; the same as `GT4_GS_SCALE`).
- `--gs-setting ID=VALUE` (repeatable) sets any other settable row, for example
  `--gs-setting fxaa=true --gs-setting aspect=4:3`. Only with `--mode pcsx2`. The values go to the probe as
  `GT4_GS_SETTINGS` (JSON). The probe checks them again against the table. Resolved rows (for example
  `--gs-setting filter=...`) are refused with "not settable yet" until the owner checks them in game.
- `GT4_PRESENT_MODE` still sets `VsyncEnable` and `DisableMailboxPresentation`. Giving `vsync` or `mailbox` through
  `--gs-setting` together with a non-default present mode is refused.
- `--gs-scale` and `--gs-setting upscale=...` together are refused.

The probe (`scripts/external_runtime_probe.py`) builds the settings and writes `gt4-gs.ini` in the session folder
with `gs_settings.write_ini_atomic()`. Headless runs add `[Folders] Cache` and `[GT4GS]` lines through the
launcher rows.

Command line checks (repository root):

```
python scripts/gs_settings.py check
python scripts/gs_settings.py allowlist            # print; --write updates docs/launcher/gs-settings-allowlist.json
python scripts/gs_settings.py ini --set upscale=2 --out <file>
```

## Adding or changing a setting

1. Add or edit a row in the table. Use `status = "resolved"` once the key and values are confirmed in the sources
   (with file:line in the notes). Use `pending-TASK-NNN` when another task decides the value.
2. Run `scripts/gs_settings.py check` and `scripts/gs_settings.py allowlist --write`.
3. Run `tests/python/test_gs_settings.py`. The committed allow-list must match the table.
4. A row becomes settable (`status = "ok"`) only after the owner checks it in the game and the change is recorded.

## Open items

- Fork enforcement: `gt4-gs.exe` must refuse keys outside the allow-list (TASK-037/038 agent).
- Owner in-game checks to turn `resolved` rows into `ok`: texture filtering, trilinear, anisotropic, dithering,
  texture preloading, present filter, CAS mode and sharpness, TV shader, shade boost, OSD position
  and scale, deinterlace and blending levels other than Automatic and High, and the D3D renderers (not offered in the
  Vulkan build).
- Texture replacement and dump rows wait for TASK-040.
- Per-key `UserHacks_*` rows, the adapter name list and the `EnableGameFixes` decision wait for TASK-043. D.2 also
  names the `Textures` and `Snapshots` folders (not in the table yet).
- Launcher detail: the PCSX2 UI shows Bilinear (Sharp) as the present-filter default, while the loader defaults to
  Smooth. The table keeps Smooth (plan D.1).
- Aspect ratio also has `Stretch` and `10:7` in PCSX2 (pcsx2/Pcsx2Config.cpp:639-645). Only the TASK-036 names are
  offered.

Bool rows take their range from the On/Off wording in the plan. Their behaviour in the fork is checked by the owner
in the game menus.
