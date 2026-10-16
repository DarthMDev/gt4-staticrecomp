# GS settings table, launcher pages and gt4-gs.ini (TASK-039, curated by TASK-049)

Status (2026-10-16, TASK-049): the table is curated for the launcher (owner decisions 1 to 6). Source: PCSX2 v2.8.2
(tag `v2.8.2` = commit `fd9d310c`). Design source: [GSDX-PLAN](GSDX-PLAN.md) sections D.1 and D.2 (see "Corrections").
Fork allow-list enforcement in `gt4-gs.exe` is still pending (TASK-037/038 agent).

## What it is

`docs/launcher/gs-settings.toml` is the single source for the PCSX2 GS options that GT4Recomp exposes. Each row is one
setting: launcher id, label, help, `page`, `order`, `control`, INI section and key, type, range or choices, GT4 default,
`written`, `status`, `env` (launcher-only rows), notes and sources (file:line at v2.8.2). The table has 58 rows and one
`[[profile]]`, `gt4-defaults`.

`scripts/gs_settings.py` (stdlib and `tomllib`, Python 3.11 or newer) does these things:

- `load_table()` validates the table: schema version, unique ids and INI keys, pages, orders, controls, types,
  statuses and ranges. A broken table is refused with every error listed.
- `settings_from_assignments()` and `settings_from_json()` check user values. Unknown ids, unknown choices, values out
  of range, non-settable rows and launcher-only rows are refused, with an error that names the row.
- `reset_to_defaults()` returns the `gt4-defaults` profile: the GT4 default of every settable row.
  `render_ini(rows, reset_to_defaults())` is the default gt4-gs.ini, byte for byte (tests prove it).
- `render_ini()` writes gt4-gs.ini text. Sections come in a fixed order (`EmuCore/GS`, `EmuCore`, `Folders`,
  `GT4GS`); rows follow the table order. `always` rows are written with their default. `when-set` rows are written
  only when the value differs from the row default, so an explicit default adds no line. `launcher` rows are written
  only when the launcher supplies them.
- `write_ini_atomic()` writes a temporary file in the same folder, then calls `os.replace`. A failed write leaves the
  old file in place and removes the temporary.
- `offered()` lists the rows the launcher may show, in page and order.

`allowlist()` emits the INI section and key pairs as `docs/launcher/gs-settings-allowlist.json`. Every row with a key is
included, whatever its status. Launcher-only rows and rows without a key are not.

## Columns

| Column | Meaning |
|---|---|
| `id` | launcher identifier, used by `--gs-setting ID=VALUE` |
| `page` | `basic` (launcher GRAPHICS page), `advanced` (ADVANCED page), `mods` (TASK-048 mod manager; neither page), `hidden` (never shown) |
| `order` | position on its page (unique per page) |
| `control` | `slider` (int range), `toggle` (bool), `dropdown` (enum), `text` (string), `none` (launcher-supplied, path) |
| `label`, `help` | the name and one-line help shown to the owner |
| `section`, `key` | INI section and key (empty for launcher-only rows) |
| `type` | `bool`, `int`, `float`, `enum`, `string`, or `path` (a folder the launcher supplies) |
| `min`, `max`, `choices` | range for numbers; `{name, ini, status, label}` entries for enums |
| `default` | GT4 default (launcher name for enums; `true` or `false` for bool) |
| `written` | `always`, `when-set`, `launcher` (supplied by the launcher), or `never` (launcher-only) |
| `status` | `ok` (settable), `resolved` (confirmed in the sources, not settable until the owner checks it in game), `pending-TASK-NNN` |
| `env` | launcher-only rows: the environment variable the launcher reads |
| `notes` | sources (file:line at v2.8.2) and open points |

A value is settable only when its row and its choice both have status `ok`. The launcher offers only rows on the
`basic` or `advanced` page that are settable (`gs_settings.offered()`).

## Launcher pages (owner decisions, 2026-10-16)

### GRAPHICS page (`page = basic`, 6 rows)

| Order | Row | Control | GT4 default | Status | Why it is here |
|---|---|---|---|---|---|
| 1 | `gs_engine` "Renderer" | dropdown | PCSX2 GS | ok (launcher-only) | Decision 1: PCSX2 GS, or CoreGS 1.1 as the secondary renderer. Not written to the INI. |
| 2 | `upscale` "Internal resolution" | slider 1 to 12 | 3x | ok | Decisions 1 and 6. Biggest visual effect. Cannot break the renderer. |
| 3 | `aspect` "Aspect ratio" | dropdown | 16:9 | ok | Visible and safe. Moved here from Display: 16:9 is the GT4 setting (TASK-009). |
| 4 | `fxaa` "Anti-aliasing (FXAA)" | toggle | off | ok | Decision 1. |
| 5 | `cas_mode` "Sharpening (CAS)" | dropdown | off | resolved | Decision 1: FidelityFX CAS mode. Settable after the owner checks it in game. |
| 6 | `cas_sharpness` "Sharpening amount" | slider 0 to 100 | 50 | resolved | Decision 1: CAS sharpness. Used with CAS on. |

Rows with status `resolved` are shown in the layout but not offered until the owner's in-game check turns them `ok`.
Today the launcher offers four Graphics rows (`gs_engine`, `upscale`, `aspect`, `fxaa`).

CoreGS 1.1 options (upscale, FSR, FXAA, deinterlace) are hidden for now (decision 5). They are CoreGS controls, not rows of
this table. Only the renderer choice `coregs-1.1` remains.

### ADVANCED page (`page = advanced`, 36 rows, 15 offered today)

Pre-configured to the GT4 ideal. Groups in order:

- Renderer and presentation: `renderer` (Graphics API, Vulkan; D3D12 and D3D11 after the owner checks them), `adapter`
  (empty, resolved), `vsync` (always set by `GT4_PRESENT_MODE`), `mailbox`, `linear_present` (present filter, Smooth),
  `integer_scaling` (off).
- Display: `deinterlace` (Automatic, always; TASK-047), `disable_interlace_offset` (off), `antiblur` (on),
  `offsets` (off), `overscan` (off), `fmv_aspect` (off), `tv_shader` (None).
- Filtering and accuracy: `filter` (texture filtering, PS2 bilinear), `trilinear` (Automatic), `anisotropic` (off, the
  60 FPS budget), `mipmap` (hardware mipmapping on, `hw_mipmap`), `blending` (High, always), `dithering` (Unscaled),
  `texture_preloading` (Full), `hw_download_mode` "Readbacks" (Accurate, always; Unsynchronized is labelled "faster,
  may drop effects"), `accurate_alpha_test` (off), `aa1` (off), `rov` (off, experimental).
- Colour: `shade_boost` (off) and the four levels (50 each).
- OSD: `osd_fps`, `osd_gpu`, `osd_resolution` (off), `osd_messages_pos` (top left), `osd_performance_pos` (top right),
  `osd_scale` (100 percent).
- Other: `disable_shader_cache` (off).

Rows marked `resolved` (for example `filter`, `trilinear`, `dithering`, `texture_preloading`, `linear_present`,
`tv_shader`, the shade-boost levels) are on the page but not offered until the owner checks them in game.

Blending High, Readbacks Accurate and deinterlace Automatic are the owner's values. The GT4 default for the rest is the
PCSX2 default, unchanged, except where the table says otherwise.

### MODS (`page = mods`, 6 rows, TASK-048)

Texture replacement and dumping: `texture_load`, `texture_load_async`, `texture_precache`, `texture_dump`,
`texture_dump_mipmaps`, `texture_dump_fmv`. Decision 4: they are on neither page. The future mod manager (TASK-048,
Content Manager style) manages them. Status stays `pending-TASK-040`.

### HIDDEN (`page = hidden`, 10 rows)

Never shown to the owner:

- `manual_hacks` and `user_hacks`: UserHacks switches bypass the GameDB fixes that GT4 relies on; per-key rows wait for
  TASK-043.
- `game_fixes` (`EnableGameFixes`, on; the GameDB fixes for GT4).
- `extended_upscale` (`ExtendedUpscalingMultipliers`, off): not needed up to 12x (section 12x below). Never written.
- `cache_folder`, `data_root`, `defer_swap_chain_acquire`, `headless_surfaceless`, `headless_dump_dir`,
  `headless_dump_every`: supplied by the launcher or the headless checks, not by the owner.

## Reset to defaults

`scripts/gs_settings.py` `reset_to_defaults()` returns the `gt4-defaults` profile: the GT4 default of every settable row
(`status = ok`, `written` `always` or `when-set`, not a path). Launcher-only and launcher-supplied rows are left out.
The launcher writes the reset file with `write_ini_atomic(path, render_ini(rows, reset_to_defaults(rows)))`.
`tests/python/test_gs_settings.py` (`ResetToDefaultsTests`) proves that this reproduces the default gt4-gs.ini
byte for byte.

## 12x internal resolution (decision 6)

Answer from the PCSX2 v2.8.2 sources:

- The GPU limit: `GSGetMaxUpscaleMultiplier(max_texture_size) = max(max_texture_size / 1280, 1)`
  (`pcsx2/GS/GS.cpp:677-681`). A GPU with 16384-texel textures allows 12x; with 8192 it allows 6x.
- The loader does not refuse a value above the limit. `GSClampUpscaleMultiplier` (`pcsx2/GS/GS.cpp:225-240`) lowers it
  to the device limit and shows an OSD warning.
- `ExtendedUpscalingMultipliers` is NOT needed for 12x and is not needed for any value the table offers. It only lifts
  the PCSX2 UI list and the hotkey cap from 12 to 25 (`pcsx2/GS/GS.cpp:1176`;
  `pcsx2/ImGui/FullscreenUI_Settings.cpp:2975`). The table keeps the row (`extended_upscale`, hidden) and the launcher
  never writes it, so the INI has no extended key at 12x.
- The table range is 1 to 12 (`upscale`, `min = 1`, `max = 12`). `--gs-scale` accepts 1 to 12.
- The device clamp is not yet in the protocol. The owner asked for the launcher to clamp to the limit that gt4-gs reports
  in READY (`max upscale`). The READY payload (`3rdparty/gt4recomp-gs-stream/gs_stream_protocol.h:177-187`) has no such
  field: it has only a reserved `u32`. Until gt4-gs reports the limit (a protocol change in the fork, an open item), the
  launcher offers 1 to 12 and PCSX2 clamps any value above the device limit with an OSD warning. Nothing crashes.

## FSR (decision 1)

FSR 1.0 does NOT exist in PCSX2 v2.8.2. Searching the v2.8.2 sources for `FSR` and `FidelityFX` finds only the CAS
option (`pcsx2/ImGui/FullscreenUI_Settings.cpp:3317`, `pcsx2-qt/Settings/GraphicsSettingsWidget.cpp:685`) and the MIPS
`QFSRV` opcode. No FSR shader or code is in the tree. The only sharpening filter is FidelityFX CAS (`CASMode`,
`CASSharpness`). FSR is not in the table and is not invented.

## Widescreen patches (finding)

PCSX2 has "Apply Widescreen Patches" (`EmuCore/EnableWideScreenPatches`, `pcsx2/Pcsx2Config.cpp:1987`; UI
`pcsx2/ImGui/FullscreenUI_Settings.cpp:3045`). The patches are pnach files: EE memory patches applied to the PS2 program
(`pcsx2/Patch.cpp`). With "Auto" aspect, PCSX2 switches to 16:9 only when a widescreen patch is enabled
(`pcsx2/Patch.cpp:822`).

GT4Recomp runs the game natively: there is no PS2 program memory in `gt4-gs.exe`, which only receives the GS stream.
So a pnach cannot apply. GT4 already runs in 16:9 through the game's own widescreen option and the fixed 16:9 of
TASK-009. The option is therefore NOT exposed (no row, no broken switch). The Aspect ratio row only sets the GS display
aspect. `EnableNoInterlacingPatches` (`pcsx2/Pcsx2Config.cpp:1988`) is also a pnach switch and is not exposed; GT4's
deinterlace is the GS deinterlace (TASK-047).

## Other PCSX2 GS keys (not in the table)

Each key the PCSX2 UI shows, or the loader reads, that has no row. GT4 default: the PCSX2 default, not written, unless
noted. "Not exposed" means no launcher control. Nothing here is written by the launcher.

| PCSX2 key (UI label) | GT4 default | Note |
|---|---|---|
| `EnableWideScreenPatches`, `EnableNoInterlacingPatches`, `EnablePatches` | off | pnach patches cannot apply to the native game (see the widescreen finding) |
| `ExclusiveFullscreenControl` (Allow Exclusive Fullscreen) | PCSX2 default | GT4 runs borderless fullscreen (TASK-038); hidden |
| `SkipDuplicateFrames` (Skip Presenting Duplicate Frames) | PCSX2 default | presentation and pacing belong to the launcher (single clock, TASK-006); not exposed |
| `UseBlitSwapChain` (Use Blit Swap Chain) | off (`pcsx2/Pcsx2Config.cpp:721`) | the present path is gt4-gs's; not exposed |
| `HWSpinCPUForReadbacks`, `HWSpinGPUForReadbacks` (Spin ... During Readbacks) | off (UI default) | latency trade-off; the readback work is TASK-037; not exposed |
| `DisableFramebufferFetch`, `DisableVertexShaderExpand` | PCSX2 default | compatibility switches; not exposed |
| `OverrideTextureBarriers` (Override Texture Barriers) | PCSX2 default | Vulkan barrier workaround; not exposed |
| `GSDumpCompression`, `DumpGSData`, `Save*` (RT, Frame, Texture, Depth, Alpha, Info, Transfer images, Draw stats, Frame stats, HW config), `SaveDraw*`, `PNGCompressionLevel`, `DumpDirectTextures`, `DumpPaletteTextures` | off | debugging and dump tools; hidden |
| `UseDebugDevice`, `UseDebugBlend`, `HWROVLogging`, `HWROVBarriersVK` | off | debug switches; hidden |
| `ScreenshotSize`, `ScreenshotFormat`, `ScreenshotQuality`, `OrganizeScreenshotsByGame`, `OrganizeVideoCaptureByGame` | UI defaults (window size, PNG, 90) | not exposed; the launcher owns screenshots |
| `EnableVideoCapture*`, `VideoCaptureAutoResolution`, `EnableAudioCapture*` | off | not exposed |
| `StretchY` (Vertical Stretch), `CropLeft`, `CropTop`, `CropRight`, `CropBottom` | 100, 0 | the picture stays uncropped; not exposed |
| `FramerateNTSC`, `FrameratePAL` (Frame rate) | PCSX2 default | the launcher's frame pacing decides the rate (TASK-006); not exposed |
| `VsyncQueueSize` | PCSX2 default | not exposed |
| `OsdShowSpeed`, `OsdShowVPS`, `OsdShowCPU`, `OsdShowGPUDebug`, `OsdShowGPUStats`, `OsdShowGSStats`, `OsdShowIndicators`, `OsdShowSettings`, `OsdshowPatches`, `OsdShowInputs`, `OsdShowFrameTimes`, `OsdShowVersion`, `OsdShowHardwareInfo`, `OsdShowVideoCapture`, `OsdShowInputRec`, `OsdShowTextureReplacements`, `OsdBoldText`, `OsdMargin`, `OsdFontPath` | PCSX2 default | OSD extras; not written (the table keeps FPS, GPU, resolution, messages, performance position and scale) |
| `OsdPerformancePos` | top right | in the table (`osd_performance_pos`) |
| `SWExtraThreads`, `SWExtraThreadsHeight`, `autoflush_sw`, `extrathreads` | not used | software renderer only; GT4 runs the hardware renderer |
| `paltex` (GPU Palette Conversion) | off (UI default) | GameDB-level fix; hidden until TASK-043 |
| `preload_frame_with_gs_data` (Preload Frame Data) | PCSX2 default | GameDB-level fix; hidden until TASK-043 |
| `UserHacks_*` (about 30 keys, the Hardware Fixes and Upscaling Fixes sections) | off | take effect only with `UserHacks = true`; names in the `user_hacks` row notes; per-key rows in TASK-043 |
| `EnableGameFixes` | on | in the table (`game_fixes`, hidden) |
| `HWMipmap` / `Mipmap` | on | in the table as `mipmap` (`hw_mipmap`, the hardware key); `mipmap` is the software toggle |
| `ExtendedUpscalingMultipliers` | off | in the table (`extended_upscale`, hidden); not needed up to 12x |
| `Adapter` | empty | in the table (`adapter`, resolved): names exist only at run time |

## Defaults and changes in TASK-049

The default gt4-gs.ini did not change. `scripts/gs_settings.py ini` (no options) still produces the TASK-036 plus
`HWDownloadMode = 0` file of main, byte for byte. No owner-ideal default changed a value: the GT4 ideal is the default
the table already had, or the PCSX2 default where the owner did not ask for a change.

Behaviour changes that need a note:

- The `when-set` rule: a `when-set` row is written only when its value differs from its default (an explicit default
  adds no line). Before, any value was written.
- `mipmap` now writes `hw_mipmap` (the hardware key). Its default (on) is the same, so no line appears by default. The
  previous key `mipmap` is the software renderer's toggle.
- `upscale` is 1 to 12 (was 1 to 8). `--gs-scale` and `GT4_GS_SCALE` accept 1 to 12.
- New rows (all `resolved` or launcher-only, none written by default): `gs_engine`, `disable_interlace_offset`,
  `accurate_alpha_test`, `aa1`, `rov`, `osd_performance_pos`, `extended_upscale`.
- Rows moved: `aspect` to Graphics; `renderer` (the API) to Advanced; the texture rows to `mods`. `visibility` was
  replaced by `page` (`basic`, `advanced`, `mods`, `hidden`).

## PCSX2 values resolved from v2.8.2

The INI facts below are interface facts (key names and value numbers), not code. Each row's `notes` has the file:line
citations.

| Launcher row | INI key | Values (INI) | GT4 default |
|---|---|---|---|
| `renderer` | `Renderer` | Vulkan 14, D3D12 15, D3D11 3 (D3D resolved, not offered) | Vulkan 14 |
| `filter` | `filter` | Nearest 0, Forced 1, PS2 2, Forced but sprite excluded 3 | PS2 (2) |
| `trilinear` | `TriFilter` | Automatic -1, Off 0, PS2 1, Forced 2 | Automatic |
| `anisotropic` | `MaxAnisotropy` | 0, 2, 4, 8, 16 | 0 |
| `blending` | `accurate_blending_unit` | Minimum 0 ... High 3 ... Maximum 5 | High (3) |
| `dithering` | `dithering_ps2` | Off 0, Scaled 1, Unscaled 2, Force 32 bit 3 | Unscaled (2) |
| `texture_preloading` | `texture_preloading` | None 0, Partial 1, Full 2 | Full |
| `deinterlace` | `deinterlace_mode` | Automatic 0, Off 1, Weave, Bob, Blend, Adaptive (TFF/BFF) 2 to 9 | Automatic |
| `linear_present` | `linear_present_mode` | Off 0, Smooth 1, Sharp 2 | Smooth |
| `cas_mode` | `CASMode` | Off 0, Sharpen only 1, Sharpen and resize 2 | Off |
| `cas_sharpness` | `CASSharpness` | 0 to 100 | 50 |
| `tv_shader` | `TVShader` | None 0 ... Lottes CRT 5, 4xRGSS 6, NxAGSS 7 | None |
| `shade_boost` | `ShadeBoost` | bool | off |
| `shade_boost_*` | `ShadeBoost_Brightness`, `_Contrast`, `_Gamma`, `_Saturation` | 1 to 100 | 50 |
| `hw_download_mode` | `HWDownloadMode` | Accurate 0, EnabledForceFull 1, NoReadbacks 2, Unsynchronized 3, Disabled 4 | Accurate (0) |
| `osd_messages_pos`, `osd_performance_pos` | `OsdMessagesPos`, `OsdPerformancePos` | None 0, Top left 1 ... Bottom right 9 | Top left (1); Top right (3) |
| `osd_scale` | `OsdScale` | 25 to 500 (percent) | 100 |
| `adapter` | `Adapter` | empty or a name from `GSGetAdapterInfo` | empty |
| `mipmap` | `hw_mipmap` | bool | on |

Readback mapping for the launcher: `--gs-readback` (`GT4_GS_READBACK`) is a shortcut for `hw_download_mode`: accurate =
`HWDownloadMode` Enabled (0, written by default); unsynchronized = Unsynchronized (3). A shortcut and
`--gs-setting hw_download_mode=...` for the same row conflict.

`UserHacks_*`: the names are recorded in the `user_hacks` row notes. Each name is a hidden per-key row for TASK-043.

## Corrections to the plan

| Plan name | Loader name | Note |
|---|---|---|
| `Dithering` | `dithering_ps2` | |
| `HWMipmap` | `hw_mipmap` | TASK-039 wrote `mipmap`, the software toggle. Corrected in TASK-049. |
| `FXAA` | `fxaa` | |
| `ManualUserHacks` | `UserHacks` | |
| `PCRTCAntiBlur`, `PCRTCOffsets`, `PCRTCOverscan` | `pcrtc_antiblur`, `pcrtc_offsets`, `pcrtc_overscan` | |
| `FMVAspectRatioSwitch` | enum of aspect names | was a bool in the table |
| `ShadeBoost*` | `ShadeBoost` (bool) and four level keys | one row per key |

## Using it

Launcher (dev script now, TASK-019/043 later): `scripts/play_performance.py --mode pcsx2`

- `--gs-scale N` sets the upscale row (1 to 12, default 3; the same as `GT4_GS_SCALE`).
- `--gs-setting ID=VALUE` (repeatable) sets any other settable row, for example `--gs-setting fxaa=true`.
  Only with `--mode pcsx2`. The values go to the probe as `GT4_GS_SETTINGS` (JSON). The probe checks them again.
  Resolved rows are refused with "not settable yet" until the owner checks them in game.
- `GT4_PRESENT_MODE` still sets `VsyncEnable` and `DisableMailboxPresentation`.
- `--gs-scale` and `--gs-setting upscale=...` together are refused.

Command line checks (repository root):

```
python scripts/gs_settings.py check
python scripts/gs_settings.py allowlist            # print; --write updates docs/launcher/gs-settings-allowlist.json
python scripts/gs_settings.py ini --set upscale=12 --out <file>
```

## Adding or changing a setting

1. Add or edit a row in the table: `page`, `order` (unique on its page), `control` (the type decides it), `default`,
   `status`, notes with file:line.
2. Run `scripts/gs_settings.py check` and `scripts/gs_settings.py allowlist --write`.
3. Run `tests/python/test_gs_settings.py`. The committed allow-list must match the table.
4. A row becomes settable (`status = "ok"`) only after the owner checks it in the game and the change is recorded.

## Open items

- Fork enforcement: `gt4-gs.exe` must refuse keys outside the allow-list (TASK-037/038 agent).
- READY max-upscale field: gt4-gs must report the device limit (a protocol change in the fork), so the launcher can clamp
  the slider to it. Until then PCSX2 clamps (see 12x).
- Owner in-game checks to turn `resolved` rows into `ok`: CAS mode and sharpness (decision 1), texture filtering,
  trilinear, anisotropic, dithering, texture preloading, present filter, TV shader, shade boost, OSD position and
  scale, deinterlace and blending levels other than Automatic and High, disable interlace offset, accurate alpha
  test, AA1, ROV, and the D3D renderers (not offered in the Vulkan build).
- Texture replacement and dump rows (`mods`) wait for TASK-040 and the mod manager (TASK-048).
- Per-key `UserHacks_*` rows, the adapter name list and the `EnableGameFixes` decision wait for TASK-043. D.2 also names
  the `Textures` and `Snapshots` folders (not in the table yet).
- Launcher detail: the PCSX2 UI shows Bilinear (Sharp) as the present-filter default, while the loader defaults to
  Smooth. The table keeps Smooth (plan D.1).
- Aspect ratio also has `Stretch` and `10:7` in PCSX2 (`pcsx2/Pcsx2Config.cpp:639-645`). Only the TASK-036 names are
  offered.

Bool rows take their range from the On/Off wording in the plan. Their behaviour in the fork is checked by the owner in
the game menus.
