# GS settings table and gt4-gs.ini (TASK-039)

Status (2026-10-16): main-repo part done on `task/039-gs-settings`. Fork allow-list enforcement in `gt4-gs.exe` is
pending (TASK-037/038 agent). Design source: [GSDX-PLAN](GSDX-PLAN.md) sections D.1 and D.2.

## What it is

`docs/launcher/gs-settings.toml` is the single source for the PCSX2 GS options that GT4Recomp exposes. Each row
is one setting: launcher id, label, help, page (Graphics, Display, Advanced), INI section and key, type, range or
choices, GT4 default, visibility, when it is written, status and notes. The table has 46 rows.

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
embed this file later and refuse keys outside it. Rows with a known key are included, whatever their status.
Launcher-only rows and rows without a known key are not.

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
| `status` | `ok` (settable), `TBD-fork` (value or key not in the plan), `pending-TASK-NNN` (waits for another task) |
| `env` | launcher-only rows: the environment variable the game reads |

A value is settable only when its row and its choice both have status `ok`. The launcher offers only rows that are
basic or advanced, settable and have at least one settable choice (`gs_settings.offered()`).

## Defaults

With no options the generated file is the TASK-036 file:

```
[EmuCore/GS]
Renderer = 14                      (Vulkan)
upscale_multiplier = 3             (3x internal resolution)
accurate_blending_unit = 3         (blending accuracy High)
deinterlace_mode = 0               (deinterlace Automatic)
AspectRatio = 16:9
VsyncEnable = true
DisableMailboxPresentation = true
```

Owner decisions in the table: 3x default; readbacks accurate by default with Unsynchronized as a speed option
(`gt4_readback`, launcher-only, pending TASK-037); blending accuracy High; deinterlace Automatic; the launcher shows
only options that really work; FXAA is offered.

Renderer: Vulkan is the only settable choice. D3D12 and D3D11 are listed with status `TBD-fork` because the plan
does not give their values.

## Using it

Launcher (dev script now, TASK-019/043 later): `scripts/play_performance.py --mode pcsx2`

- `--gs-scale N` sets the upscale row (1 to 8, default 3; the same as `GT4_GS_SCALE`).
- `--gs-setting ID=VALUE` (repeatable) sets any other settable row, for example
  `--gs-setting fxaa=true --gs-setting aspect=4:3`. Only with `--mode pcsx2`. The values go to the probe as
  `GT4_GS_SETTINGS` (JSON). The probe checks them again against the table.
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

1. Add or edit a row in the table. Keep `status = "TBD-fork"` until the value list is verified in the fork.
2. Run `scripts/gs_settings.py check` and `scripts/gs_settings.py allowlist --write`.
3. Run `tests/python/test_gs_settings.py`. The committed allow-list must match the table.
4. A row becomes settable (`status = "ok"`) only after the fork accepts the value in-game.

## Open items (TBD-fork)

- Renderer D3D12 and D3D11 integer values.
- Value lists for texture filtering, trilinear, anisotropic, dithering, texture preloading, present filter, CAS
  mode and sharpness, TV shader, shade boost (key names too), readback mode (`HWDownloadMode`), OSD position and
  scale, UserHacks_* names, adapter names, and blending or deinterlace levels other than High and Automatic.
- Texture replacement and dump rows wait for TASK-040. Readback unsynchronized waits for TASK-037.
- Fork enforcement: `gt4-gs.exe` must refuse keys outside the allow-list (TASK-037/038 agent).
- D.2 also names `[EmuCore] EnableGameFixes` (written as a when-set row, not by default) and the `Textures` and
  `Snapshots` folders (not in the table yet). Decide in TASK-043.

Bool rows take their range from the On/Off wording in the plan. Their behaviour in the fork is checked by the owner
in the game menus.
