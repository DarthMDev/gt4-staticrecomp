# TASK-048 — Mod manager in the launcher (plan)

Date: 2026-10-16. Status: plan for owner review. Author: planning agent (PCSX2-tainted: read PCSX2 v2.8.2 in
`C:\Antigravity\gt4recomp-gs` at `08d394a87`, tag `gt4gs-v0.3.2`; never works on TASK-027).
Sources read: CLAUDE.md, decision 0070, GSDX-PLAN D.3, GS-LIVE, GS-SETTINGS, `docs/launcher/gs-settings.toml`,
LAUNCHER-PLAN sections 6, 13 and 15 on `origin/task/019-launcher` (`d8feaae`).
Labels: [C] Confirmed, [HC] High confidence, [H] Hypothesis, [U] Unknown. File:line refer to the fork tree.

Owner intent (2026-10-16): texture replacement is done by the PCSX2 GS, but it is **not a graphics setting**.
The launcher has a **Mods** page where users install and uninstall mods (drop a texture-pack zip; the launcher
installs it the way PCSX2 expects). Spirit: Assetto Corsa's Content Manager.

## 0. Summary

- PCSX2 reads loose `.png`/`.dds` files from `<Textures>/<serial>/replacements/` (recursive) and keys each file
  by a name made of GS hashes. **It does not read zip packs** [C]; GSDX-PLAN D.3's `SCUS-97328.zip` form is wrong
  and should be corrected. A zip is only our import format.
- gt4-gs already takes `[Folders] Textures` from `gt4-gs.ini` (relative to `[GT4GS] DataRoot`) [C]. So the
  launcher only has to build one **generated active folder** and point `Textures` at it; no fork change is
  required for the MVP.
- Mod model: one store folder per installed mod plus a generated, hardlinked active view, rebuilt atomically
  before Play, with explicit priority and conflict resolution (PCSX2 itself resolves duplicates by file
  enumeration order, which we must not rely on).
- Realistic now: texture packs. Later: GS "look" presets and a creator toolkit. Not now: code patches (our EE
  is AOT-translated, so pnach-style code patches cannot work), disc-data mods (cars, tracks), user shaders.
- Tasks TASK-050..057 (section 7); open questions in section 8.

## 1. How PCSX2 v2.8.2 loads texture replacements

### 1.1 Folder layout and serial [C]

- Game folder = `EmuFolders::Textures` + serial (`GSTextureReplacements.cpp:262-265`); subfolders
  `replacements` and `dumps` (`:39-40`). Dumping creates both (`:276-286`).
- The serial comes from the stream: `VMManager.cpp:1049-1052` takes `GSDumpReplayer::GetDumpSerial()`, which for
  our stream is `GSStreamReplayer::GetSerial()` (`GSDumpReplayer.cpp:168`, `GSStreamReplayer.cpp:698-700`); the
  handshake supplies `SCUS-97328` (`pcsx2-gt4gs/Main.cpp:11`). Empty serial = no replacements (`:392`).
- The map is built once, in `ReloadReplacementMap()` (`:376-452`), from `Initialize()` at GS start
  (`:333-341`, called from `GSRendererHW.cpp:27`), on serial change (`:343-352`) or when the option is turned
  on (`:468-469`). gt4-gs answers `reload_config` "unsupported" (GS-LIVE line 48), so **mod changes take effect
  at the next Play** [C].
- Scan: `FindFiles(replacement_dir, "*", FILES | HIDDEN | RECURSIVE)` (`:416`). Subfolders of any depth are
  allowed. Windows recursion is `FindFirstFileW` with no reparse-point filter (`common/FileSystem.cpp:1428-1452`),
  so it would follow junctions [HC]; we will not use junctions.
- Case: a wrongly cased `SCUS-97328` or `replacements` folder still works on Windows but raises an OSD warning
  (`:400-414`). Our generated view always uses the exact case.
- **Duplicates:** the map is filled with `emplace` (`:433`), so the **first file enumerated wins** and later files
  with the same name key are ignored silently [C]. Enumeration order is the file system's, not a priority. A
  merged view must therefore contain each key exactly once.
- **No archive support:** no zip/7z code in `pcsx2/GS` or the replacement path (grep) [C]. GSDX-PLAN D.3
  (`...\textures\SCUS-97328.zip`) is incorrect for v2.8.2; fix it in TASK-050.

### 1.2 File names and hashes [C]

Formats (`:33-38`), all lowercase hex from `PRIx64`, parsed with `sscanf` (`:192-260`):

| Form | Pattern |
|---|---|
| direct | `<TEX0Hash>-<bits8>.<ext>` |
| paletted | `<TEX0Hash>-<CLUTHash>-<bits8>.<ext>` |
| region | `<TEX0Hash>-r<W>x<H>-<bits8>.<ext>` |
| region + palette | `<TEX0Hash>-<CLUTHash>-r<W>x<H>-<bits8>.<ext>` |
| old region (still read) | `<TEX0Hash>-r<regionbits64>-<bits8>.<ext>` (with or without CLUT) |

- `bits8` packs PSM (6 bits), TW, TH (4 each), TEXA TA0/AEM/TA1 (`:51-64`); the unused TCC bit is cleared on
  load (`:76-80`). The key is the whole 32-byte `TextureName` (`:44-82`), mip level always 0 for lookups (`:508`).
- `TEX0Hash` hashes the texture's GS local-memory data (unswizzled) for the base level, **plus all mip levels when
  the draw is mipmapped** (`GSTextureCache.cpp:9147-9176`, lod at `:9164-9174`). `CLUTHash` hashes the palette
  (`:9154`) and is always computed when dumping or replacing (`:7018-7019`). Region width/height come from the
  draw's source region (`:9155-9156`).
- Files named `...-mipN.png` are **not** loaded as replacements: the parser expects `.` right after `bits8`, so
  they are skipped [C]. Dumped mip PNGs (`:293-324`) are for authors only.
- A replacement is looked up first; while it loads, the original texture is used (`:7062-7094`).

### 1.3 Supported formats [C]

- Loaders: `png`, `dds`, chosen by extension, case-insensitive (`GSTextureReplacementLoaders.cpp:25-28,31-44`).
  Other files in the folder are ignored (`GSTextureReplacements.cpp:424`).
- **PNG:** only 8-bit RGB and RGBA are decoded correctly. There is no `png_set_*` transform, and rows are copied
  only for `PNG_COLOR_TYPE_RGB` (alpha forced to 0x80, PS2 "opaque") or `RGBA` (raw memcpy)
  (`GSTextureReplacementLoaders.cpp:180-217`). A paletted, grey, 16-bit or Adam7-interlaced PNG loads "successfully"
  as garbage or zeros [C, by reading; not run]. Alpha in RGBA files is passed raw; PCSX2 dumps use the GS alpha
  scale, where 0x80 is opaque [HC]. Our importer must validate these properties.
- **DDS:** BC1, BC2, BC3, BC7 (DX10 header too) and uncompressed A8R8G8B8, X8R8G8B8, X8B8G8R8, R8G8B8, A8B8G8R8
  (`:446-530`). Mip chains are read from the file (`:431-442`, `:617-628`). For block formats the base size must be
  a multiple of 4 (`:575-582`).
- **Mipmaps:** PNG replacements get GPU-generated mips when the draw needs them; compressed DDS without mips
  disables mips for that texture with an OSD warning (`GSTextureReplacements.cpp:730-768`).

### 1.4 Async loading and precache [C]

- `LoadTextureReplacementsAsync` (default true, `Pcsx2Config.cpp:784`): a lookup queues the file on a worker
  thread and returns "pending" (`GSTextureReplacements.cpp:529-537`, `:650-698`); loaded textures are injected
  into the texture cache at the next VSync (`GSRendererHW.cpp:100-101`, `GSTextureReplacements.cpp:770-799`).
  Effect: no GS-thread stall, but the original texture shows for a frame or more (pop-in).
- Synchronous (`false`): the GS thread decodes the file during the draw (`:538-552`): exact first frame,
  stutter on first use. Useful for deterministic headless tests.
- `PrecacheTextureReplacements` (default false, `Pcsx2Config.cpp:785`): queues every file for decoding into the
  RAM cache at map load (`:440-443`, `:700-717`); requests come first (high priority, `:897-930`). It guesses mips
  from `HWMipmap`/`TriFilter` (`:706`).
- Defaults and INI keys: `DumpReplaceableTextures`, `DumpReplaceableMipmaps`, `DumpTexturesWithFMVActive`,
  `DumpDirectTextures`, `DumpPaletteTextures`, `LoadTextureReplacements` (false), `...Async` (true),
  `PrecacheTextureReplacements` (false) in `[EmuCore/GS]` (`Pcsx2Config.cpp:778-785`, `:1021-1028`;
  `Config.h:827-834`). Changing load or dump purges the hash cache (`GS.cpp:912-921`).
- With replacements present, GPU palette conversion without full preloading is disabled for paletted textures
  (`GSTextureReplacements.cpp:445-450`). gt4-gs defaults are compatible: `GPUPaletteConversion = false`
  (`Pcsx2Config.cpp:751`), `TexturePreloading = Full` (`Config.h:869`).

### 1.5 The texture folder in gt4-gs [C]

- `EmuFolders::LoadConfig` reads `[Folders] Textures`, default `textures`, **relative to `DataRoot`** unless
  absolute (`Pcsx2Config.cpp:2301-2322`, default at `:2294`).
- gt4-gs copies `[EmuCore/GS]` and `[Folders]` from `--config` (`pcsx2-gt4gs/Main.cpp:649-662`) and applies the
  folders in stream modes after setting `DataRoot` (default `%LOCALAPPDATA%\GT4Recomp\gt4-gs`, `:697-723`); it
  logs `textures <path>` at start (`:722-723`).
- So today the textures folder is `<DataRoot>\textures`. The settings table has no `Textures` row yet
  (`gs-settings.toml` notes of `cache_folder`). TASK-053 adds one (`written = "launcher"`, absolute path).

### 1.6 Memory, VRAM and load time

- **RAM:** decoded replacements stay in `s_replacement_texture_cache` until the map is reloaded or replacements
  are turned off (`GSTextureReplacements.cpp:136,380-389,719-728`) [C]. There is **no eviction**: RAM grows with
  every distinct replaced texture seen in the session; with precache it is the whole pack, decoded. PNG decodes to
  4 bytes/pixel (a 2048x2048 texture = 16 MiB); BC1 is 0.5 and BC7 1 byte/pixel [C, format arithmetic].
  Example: 3,000 textures at 1024x1024 PNG = 12 GiB decoded with precache [H, illustrative].
- **VRAM:** replacements enter the hash cache (`GSTextureCache.cpp:7073-7074`, accounted as
  `m_hash_cache_replacement_memory_usage`) and age out after 30 unused frames or above 800 entries
  (`:7179-7196`) [C]; re-creation from the RAM cache is an upload, not a decode. GPU-generated PNG mips add about a
  third.
- **Load time:** the map scan stats every file at GS start (tens of thousands of files: seconds, [H]); precache
  decodes everything in the background; synchronous loading stalls the GS thread per new texture.
- Recommendation: async on, precache off by default; recommend BC7 DDS with mips for large packs; TASK-050
  measures map scan time, RAM and VRAM on a synthetic 10k-file pack.

### 1.7 Dumping (creator tool) [C]

`DumpTexture` (`:801-845`) writes PNGs to `<Textures>/<serial>/dumps/` on the worker thread; already dumped or
already replaced names are skipped (`:808`). Dumping costs a hash and a PNG encode per new texture.

## 2. Mod model

### 2.1 Manifest `gt4mod.toml` (MIT schema, ours)

```toml
schema = 1
id = "hd-hud"                 # slug, unique; generated from the name if the pack has no manifest
name = "HD HUD"
version = "1.2.0"
author = "someone"
description = "..."
license = "..."               # free text, shown only
homepage = "https://..."      # shown as text; the launcher never fetches it
kind = "texture-pack"
preview = "extras/preview.png"

[target]
serial = "SCUS-97328"         # packs for other serials are refused or retargeted with a warning (Q5)

[requires]
renderer = "pcsx2-gs"         # CoreGS 1.1/2.0 ignore texture packs; the Mods page says so
gt4gs_min = "0.3.2"

[hash_settings]               # optional: GS settings under which the pack was dumped (section 6.2)
hw_mipmap = true
estimate_texture_region = false

[order]
conflicts = ["other-pack"]    # hard conflicts: both cannot be enabled
load_after = ["base-pack"]    # hint for the default priority
```

- The file list is generated by the launcher, not trusted from the pack: `files.lock.json` with, per file, the
  relative path, size, SHA-256, parsed texture key and image info (format, size, mips). Large packs (tens of
  thousands of files) keep this out of the TOML.
- Priority is not in the manifest; it is the user's order in the registry (`mods.toml`).

### 2.2 Kinds: now and later

| Kind | What it is | Verdict |
|---|---|---|
| `texture-pack` | PCSX2 replacements | **Now** (TASK-050..055) |
| `gs-preset` | a named bundle of GS settings rows (e.g. "Sharp 4K") from `gs-settings.toml`, validated by `gs_settings.py` rules | Later, cheap (TASK-057 study) |
| post shaders | PCSX2 v2.8.2 has only built-in TV shaders (`TVShader`, `Config.h:875`) and CAS/FXAA; no user shader loader (grep: no ReShade/librashader) [C] | Built-in ones are already settings. User shaders would be a GPL fork feature: not planned |
| code patches (pnach, widescreen) | patch EE instructions in RAM | **Not possible as is**: our EE code is AOT-translated and does not execute guest RAM instructions, so a code write is silently ignored [HC]. Real support means re-translating with patches at build time (the release exe is prebuilt, 0069). Risks: silent no-ops, save corruption, identity digests. GT4 has a native 16:9 mode. Not planned |
| data patches (RAM values) | pnach writes to data addresses | Possible in principle via a runtime hook; risky (desync with saves, no validation). Not planned |
| cars, tracks, menus | edits of `GT4.VOL` content; OpenAdhoc targets the GT5/GT6 Adhoc scripts, not GT4 [HC] | Would need a read-only VOL overlay in our disc layer; out of scope (recorded idea; Spec II) |

## 3. Install and uninstall

### 3.1 Store layout (user data, never game data)

```
%LOCALAPPDATA%\GT4Recomp\mods\
  mods.toml                    # registry: schema, ordered list [{id, enabled}], top = highest priority
  store\<id>\                  # one folder per installed mod, read-only after install
    gt4mod.toml  files.lock.json
    files\...                  # .png/.dds only, original relative paths kept
    extras\...                 # readme, previews, license; never in the active view
  inbox\                       # drop zips here; imported at launcher start (controller-friendly)
  staging\                     # import work area (same volume), cleaned at start
  active\textures\SCUS-97328\replacements\<NNN>-<id>\...   # generated view, hardlinks
  creator\textures\SCUS-97328\{dumps,replacements}\        # creator workspace (section 4.3)
  journal.log                  # every operation, for recovery
```

The launcher never writes under the install folder, the ISO, the ELF, `memcard\`, `saves\` or `private\`.

### 3.2 Import

1. Input: a `.zip` (drag and drop, file picker, or `inbox\`), or a folder. 7z/rar: refused in v1 ("extract it
   first, then import the folder") (Q8).
2. Detection, in order: a `gt4mod.toml` at the root or one level down; otherwise a raw PCSX2 layout:
   `<anything>/SCUS-97328/replacements/**`, `textures/SCUS-97328/**`, a bare `replacements/**`, or a folder whose
   files mostly parse as texture names. A `dumps` folder inside a pack is ignored. Another serial
   (e.g. PAL `SCES-51719`) is reported; see Q5.
3. Validation (section 5.2), then extraction into `staging\<random>\`, hashing, `files.lock.json`, generated
   manifest (name from the zip name, version "unknown").
4. Within-pack duplicates (same key twice, e.g. `.png` and `.dds`, or different folders): kept once by a
   documented rule (DDS over PNG, then shortest path) and listed as warnings.
5. Commit: `rename(staging\<random>, store\<id>)` (same volume, atomic), then the registry update (write temp,
   `ReplaceFileW`). A crash before the rename leaves only staging, cleaned next start.
6. Same `id` already installed: offered as an update; it keeps the enabled state and position, and the old
   folder goes to the Recycle Bin after the new one is committed.

### 3.3 Active view: options

| Option | Disk | Speed | Conflicts | Verdict |
|---|---|---|---|---|
| Point PCSX2 at several folders | — | — | PCSX2 has one `Textures` folder [C] | not possible |
| Junctions per mod into `replacements` | 0 | instant | duplicates resolved by enumeration order (`:433`) | **no** |
| Copy winners into a generated folder | 1x pack size | slow for GBs | exact | fallback only |
| **Hardlinks of winners into a generated folder** | ~0 (MFT entries) | ~10k links/s [H] | exact | **chosen** |
| Generated "index" file read by a forked loader | 0 | instant | exact | needs a GPL fork feature; later at most |
| Zip mode | — | — | PCSX2 reads no zips [C] | no |

- Hardlinks need the same NTFS volume; `store\` and `active\` are siblings, so they are. If `CreateHardLinkW`
  fails (FAT/exFAT, policy), the builder copies and says so.
- The view is **generated**: rebuilt from the registry before each Play when its digest (ordered list of
  enabled ids + their lock digests) changed. Build `active.new\`, verify (count, keys), then rename `active\` to
  `active.old\`, `active.new\` to `active\`, and delete `active.old\` (plain delete: it holds only links; the
  store keeps the data). Never while the game or gt4-gs runs (the existing process lock).
- Users must not edit `active\` (a hardlink edit would change the store); it is marked with a `README.txt` and
  recreated if tampered with (lock digests checked at build).
- Empty view (no enabled texture mod): `LoadTextureReplacements` is written false and the folder is left
  empty, so no hashing cost (`GSTextureCache.cpp:7012-7015`).

### 3.4 Enable, disable, priority, conflicts

- Enable/disable flips `enabled` in `mods.toml`; nothing is reinstalled; the view rebuild happens at Play.
- Priority: the list order; **top wins** (Q2). For each texture key, the highest enabled mod provides the file.
- Conflict report per mod: "overrides N textures of X", "N of its textures are hidden by Y". Hard conflicts
  from the manifest block enabling both. Load-after hints set the default position at install only.
- A key collision between files with identical SHA-256 is not shown as a conflict.

### 3.5 Uninstall

- Remove = disable, then move `store\<id>\` to the Recycle Bin (existing `recycle.rs`, `IFileOperation` with
  `FOFX_RECYCLEONDELETE`), then registry update. If the folder exceeds the bin quota (`SHQueryRecycleBinW`),
  Windows would delete it permanently: the launcher asks first (Q10).
- Disk use is shown per mod (store size) and in total, with free space checked before import (needs about the
  uncompressed size; the user's zip is never modified or deleted).

### 3.6 Safety of operations

Journal every step; on start, finish or roll back an interrupted operation (staging cleanup, `active.new\` or
`active.old\` left over, registry `.tmp`). All writes go through temp + rename. Long paths use `\\?\`.
Non-ASCII user folders (e.g. "João") are tested: `gt4-gs.ini` is written as UTF-8 and PCSX2 converts paths with
`GetWin32Path` (`FileSystem.cpp:1452`) [HC].

## 4. Launcher integration

### 4.1 Mods page (Tauri 2 HTML, section 12 tokens of LAUNCHER-PLAN)

- New top-level page **Mods**, next to Saves and Replays (not under Graphics). Mockup `mockup-mods.html` first,
  for owner approval (states: empty, list, details, import progress, conflict, remove confirm, wrong renderer).
- List rows: enable toggle, name, version, author, kind badge, size, conflict marker; empty state: "No mods yet /
  Drop a texture pack (.zip) here or put it in the Inbox folder."
- Details pane: preview, description, texture count, formats (PNG/DDS, mips), conflicts, install date, license
  text, "Open folder", Remove.
- Header: "Install from file", "Open Inbox folder", total size. A notice when the renderer is not PCSX2 GS:
  "Texture packs need the PCSX2 GS renderer."
- Controller (XInput, same focus model as the settings pages): D-pad moves the focus; Cross toggles enable;
  Triangle opens details; L1/R1 move the mod up/down; Square asks to remove; Start = Install (opens the Inbox
  explanation, since the Windows file dialog is not controller-friendly); Circle = back.
- Tauri: commands `mods_list`, `mods_import(path)`, `mods_set_enabled`, `mods_move`, `mods_remove`,
  `mods_details`, progress events, cancel. File drop gives a path to Rust; the page gets no file system access.

### 4.2 Passing the active set to gt4-gs

- Before Play: rebuild the view if needed, then write in `gt4-gs.ini`: `[Folders] Textures = <abs>\mods\active\textures`,
  `[EmuCore/GS] LoadTextureReplacements = true|false`, `LoadTextureReplacementsAsync = true`,
  `PrecacheTextureReplacements` from the Mods advanced toggle (Q4).
- In `gs-settings.toml` the three texture rows move off the Graphics page: `page = "Mods"`,
  `visibility = "hidden"`, `written = "launcher"`; a new `textures_folder` row (`[Folders] Textures`, `path`,
  launcher). The dump rows move to the creator section. `launch.json` records the view digest and mod ids.
- Dev launcher: `play_performance.py --textures-root <dir>` (or the `gt4mods` CLI view) so the owner can test
  packs before the Tauri launcher resumes.

### 4.3 Creator tools (Advanced, collapsed "Creator" section on the Mods page)

- "Dump textures": next Play points `Textures` at `mods\creator\textures`, sets `DumpReplaceableTextures = true`
  (optional mips, FMV off), and loads the creator `replacements\` as a work-in-progress pack on top of nothing
  (or of the enabled mods, Q9). A banner says dumping lowers performance.
- "Open dumps folder", "Open work folder", "Package as mod" (writes a zip with `gt4mod.toml`, validated by the
  same importer), "Clear dumps" (Recycle Bin).
- Later (TASK-056): live reload of replacements while the game runs (a gt4-gs control message).

## 5. Legal and safety

### 5.1 Content

- Users bring their own packs. The launcher ships no packs and downloads none; a catalogue is only possible
  after a separate owner decision (Q7), with its own legal review.
- Packs are derivative works of the game's textures: GT4Recomp never hosts, mirrors or links them. Docs explain
  the format and point to the creator tools only.
- `homepage` is shown as text, not fetched. No telemetry of installed mods.
- Pack import code is MIT, written from the format facts above (names, PNG/DDS headers); no PCSX2 code is
  copied (decision 0070 limits).

### 5.2 Input validation (all in Rust, before anything is written outside staging)

- Zip-slip: reject absolute paths, drive letters, `..`, leading `\`/`/`, colons (NTFS streams), device names
  (`CON`, `NUL`, `COM1`...), trailing dots/spaces, control characters; normalize to NFC; reject case-insensitive
  path collisions; final path must stay under the staging root (checked after joining).
- Zip bombs: limits on entries (200,000), per-file size (256 MiB), total uncompressed (20 GiB default, Q8),
  ratio per entry (1:200); sizes checked while streaming, not trusted from the header. No nested archives,
  symlink entries or encrypted entries.
- Only `.png`, `.dds` go to `files\`; text and images for `extras\` are allow-listed (`.txt .md .pdf .png .jpg`);
  executables and scripts are dropped and listed.
- Images: PNG signature, IHDR (8-bit, RGB or RGBA, non-interlaced) checked, full decode on a worker pool to catch
  truncated files; DDS header, supported FourCC/DX10 formats, block-size rule, data length for the declared mip
  chain. Dimensions above 8192 warn, above 16384 refused. Unsupported PNG types: refused or converted (Q6).
- Names that do not parse as texture keys are counted and ignored (PCSX2 would ignore them too).

## 6. Compatibility

### 6.1 Expectation

Our GS stream is the game's own GS traffic (GIF packets, privileged writes, vsync), replayed by the same PCSX2
GS code with the same GameDB entry (serial from the stream). Hashes depend only on GS local memory, TEX0/TEXA,
CLUT and the draw's region and mip use (`GSTextureCache.cpp:9147-9176`). So **existing PCSX2 GT4 packs for
SCUS-97328 should load unchanged: High confidence**, under these conditions:
- the hash-relevant settings match the pack author's: `HWMipmap`/`TriFilter` (mipmapped draws hash all levels),
  `UserHacks_EstimateTextureRegion` (changes region names); defaults match PCSX2's defaults [HC];
- the pack targets the same disc (NTSC-U `SCUS-97328`); PAL packs may match partially where texture data is
  identical [H];
- the pack was made with a PCSX2 version using the same hash function [U for very old packs].

### 6.2 Verification plan (headless, TASK-050)

1. Determinism: replay the pinned capture (`gt4-gs.exe --replay-gscap <capture> -dumpdir <frames>`, config with
   `DumpReplaceableTextures = true`, `[Folders] Textures = <tmp>`) twice; the dump name sets are identical.
2. Positive: pick a dumped texture used in frame K; write a solid-magenta replacement (2x size) with the same
   name into `replacements\`; replay with `LoadTextureReplacements = true`, `Async = false`; frame K has magenta
   pixels where the texture is drawn, other frames before first use are byte-identical to the baseline.
3. Negative controls: the same file with one hex digit changed, with a `-mip1` suffix, as a 16-bit PNG
   (expected: garbage, documented), and an empty view (all frames byte-identical to the baseline).
4. Async: with `Async = true`, the replacement appears within 2 vsyncs of first use.
5. Duplicate rule: two files with the same key in two subfolders; record which wins (documents `:433`).
6. Cost: a synthetic 10,000-file pack: map scan time (log), RAM with and without precache, VRAM report.
7. Cross-check with real PCSX2 (optional, owner or agent): dump the same menu scene in PCSX2 v2.8.2 with the disc
   and compare name sets with gt4-gs dumps; acceptance ≥ 95% overlap of the scene's names [H target].
8. Community pack, if the owner has one: count matched names in the tested scenes and a visual check in game.

Python test (skipped when the private capture is absent) plus a pure fixture for the name parser using real dump
names.

## 7. Phased tasks

| Id | Title | Repo / branch | Model, sessions |
|---|---|---|---|
| TASK-050 | Texture replacement contract and headless round trip (absorbs TASK-040) | main `task/050-texture-contract`; fork `gt4gs/textures` only if a log line is needed | Haiku, 1-2 |
| TASK-051 | `gt4mods` core: manifest, detection, validation, import, store | launcher `task/051-mods-core` (from `task/019-launcher`) | Haiku, 3 (Opus review of 5.2) |
| TASK-052 | Active view, priority, conflicts, uninstall, `gt4mods` CLI | launcher `task/052-mods-view` | Haiku, 2 |
| TASK-053 | GS settings and dev launcher wiring (texture rows off Graphics, `textures_folder`) | main `task/053-mods-gs-wiring` | Haiku, 1 |
| TASK-054 | Mods page (mockup, then Tauri) | launcher, after TASK-019 resumes | Haiku, 2-3 |
| TASK-055 | Creator tools: dump workspace, package as mod | launcher + main docs | Haiku, 1-2 |
| TASK-056 | Live texture reload in gt4-gs (later) | fork + main | Haiku, 1-2 |
| TASK-057 | Study: other mod kinds (GS presets, VOL overlay, data patches) (later) | main docs only | Opus, 1 |

Order: 050 (now, unblocks everything and corrects D.3) → 051 → 052 → 053 (owner can test packs with the CLI and
the dev launcher) → 054 and 055 when the launcher resumes. Details, acceptance and risks in `TASKS-rows.md`.

Main risks: unbounded RAM with big PNG packs (mitigate: async default, precache off, DDS advice, F8/log);
hash mismatch from settings (mitigate: `hash_settings` warning); hardlink unavailability (copy fallback);
Recycle Bin quota (ask); users editing the view (digest check).

## 8. Open questions for the owner

1. Mods page in release 1.0.0, or right after it? (Recommend: TASK-050..053 before 1.0.0, page in 1.0.0 if the
   launcher schedule allows.)
2. Priority rule: top of the list wins? (Recommend yes, like Content Manager and mod organizers.)
3. Fold TASK-040 into TASK-050? (Recommend yes.)
4. "Preload all textures" toggle: show it in the Mods advanced area, or keep it hidden off?
5. Packs made for another serial (PAL): refuse, or install with a "may partly work" warning?
6. PNGs PCSX2 cannot read (16-bit, paletted, interlaced): refuse the pack, or convert them at import?
7. Third-party catalogue: never, or possibly later with a separate review?
8. Import limits: 20 GiB per pack and zip only (no 7z/rar) acceptable?
9. Creator mode: dump with the enabled mods active, or with no mods?
10. Remove when a pack is too big for the Recycle Bin: ask and delete permanently, or refuse?

## Owner decisions (2026-10-16)

1. The mod manager is **not in release 1.0.0**; it is planned for **1.1.0**. It is tracked in TASKS.md only
   (not in the README task table). The launcher's Mods tab stays hidden in 1.0.0.
2. Priority: the mod higher in the list wins a texture conflict.
3. TASK-040 is folded into TASK-050.
4. The "precache all textures" toggle is hidden (off by default; RAM risk).
5. PAL-version packs are allowed with a warning.
6. PNGs in an unsupported format: the launcher shows a warning and asks whether to convert them to 8-bit.
7. Third-party pack catalogue: not now, but planned later (we never host packs ourselves).
8. Limits: 20 GiB per pack; zip and loose-folder import.
9. Creator texture dumps are taken with mods off, from a creator option in the Advanced settings.
10. A pack too big for the Recycle Bin: ask clearly, then delete permanently only after on-screen confirmation.
