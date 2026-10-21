# In-game settings in GT4's own Options menu: feasibility study

Date: 2026-10-20. Read-only research (no build, no game run). This is the study for TASK-078, planned after 1.0.0 (owner, 2026-10-20). Agent status: **clean room**
(no PCSX2 sources, no gt4recomp-gs fork, no `private/tooling/pcsx2-*`, no `private/slice189-pcsx2-ref`). Repo docs
about the GS protocol were read.

Owner idea (2026-10-20): remove GT4's own video-output option (Normal 480i / 480p / 1080i), keep the game fixed at
480p internally, and show an "Output resolution" option (480p to 8K, the renderer's internal resolution) plus other
graphics settings (fullscreen, anti-aliasing, maybe texture filtering) inside the game's native Options menu.

Confidence labels: **[C]** Confirmed, **[H]** High confidence, **[Hy]** Hypothesis, **[U]** Unknown.

Sources used:

- gt4-decomp clone `private/slice151-research/gt4-decomp` (commit e0b70c4d6): `knowledge/script-engine.md`,
  `config/adhoc_methods.txt`, `src/gamedata/unit_0017FCB0/MOption__*.cpp`, `src/race/unit_004271E8/func_0043*.c*`,
  `src/startup/unit_00101400/func_001074D0.cpp`, `func_00106058.cpp`, `func_001103E0.c`, `func_0010BE20.cpp`.
- Nenkai references under `private/slice151-research/nenkai/`: `gt-modding-hub/docs/builds/file_lists/SCUS-97328_2.00.txt`
  (public file list of our exact disc), `OpenAdhoc/src/projects/gt4/option/*.ad` (GPL-3.0 reconstructed scripts; read
  for facts only, nothing copied), `GTAdhocToolchain/README.md`, `GT4Hooks/README.md`,
  `GT-File-Specifications-Documentation/Formats/PS2/GT4/GT_RT03_RText03.bt`.
- Earlier private extraction `private/slice155-spec2-480p/retail_adc/` (decoded retail `option.adc` and
  `option.mproject`) and `option_strings.txt` (string scan of the retail `option.adc`).
- Generated EE code of slice208: `private/slice208-ee-entries-producer/generated/unit_*.cpp`.
- Donor runtime `private/references/ps2-web-recomp/runtime/src/dispatch.cpp`, `disc.cpp`, `iop/cdvd.cpp`.
- Repo: `native/race_display.*`, `native/window.cpp`, `native/gpu_init.inc`, `docs/BUILD.md` (slice158, slice164),
  `docs/STATUS.md`, `docs/GS-SETTINGS.md`, `docs/GS-STREAM-PROTOCOL.md`, `docs/GS-LIVE.md`, `docs/MENUS-AND-WHEEL.md`,
  `docs/DECOMP-INTEGRATION.md`, `docs/VOLUME-READER.md`, `docs/journal/2026-10-13.md`.
- Launcher branch `origin/task/019-launcher`: `launcher/crates/gt4l-core/src/settings.rs`, `launch.rs`, `config.rs`,
  `paths.rs`, `docs/launcher/LAUNCHER-PLAN.md`.
- ai-memory: `concepts/current-state`, decisions on launcher and renderer (read as history only).

---

## 1. How GT4's menu system works

### 1.1 Layers

| Layer | What it is | Where | Confidence |
|---|---|---|---|
| Adhoc VM | Polyphony's script language; GT4 retail uses bytecode **version 5** (`ADCH005` magic). Menus, game modes and option pages are scripts; the executable provides the VM and about a thousand natives. | `CORE.GT4`; VM classes 0x2EA548-0x32E4A0 (`knowledge/script-engine.md`) | [C] magic `ADCH005` read at the start of the decoded retail `option.adc` |
| Projects | One folder per menu area in `GT4.VOL`: `projects/<name>/<name>.adc` (compiled script) + `<name>.mproject` (widget tree) + `<lang>/*.gpb` (images) + `<lang>/rtext.rt2` (localized text). | disc file list `SCUS-97328_2.00.txt` | [C] |
| `.mproject` | **Plain text** widget tree: `Project { name string {"OptionProject"} ... children [20] { ... } }`. Widgets: `Composite`, `VBox`, `TextFace`, `ColorFace`, `ImageFace`, `FrameImageFace`, `OptionMenu`, sliders, with `name`, `geometry`, `visible`, `key` (text key) and `tooltip_key`. | decoded retail `option.mproject` (681,977 bytes) | [C] |
| Natives | C++ classes exposed as `M<Class>` (methods, functions, attributes with getter cb1 / setter cb2) registered at static init through `func_002F3818` (method), `func_003068A8` (function), `func_002F3860` (attribute). | `config/adhoc_methods.txt` | [C] (decomp matched) |
| Text | `TextFace` widgets carry a `key`; the label is looked up per page in `projects/option/US/rtext.rt2` (RText, Nenkai's `GT_RT03_RText03.bt`: header, pages, label/value string pairs). | disc list; template | [H] (the exact RT magic of the retail `.rt2` was not read) |

`MManager` natives (script `main::manager`): `loadProject` 0x00211CE0, `unloadProject` 0x00211E48, `loadWidget` /
`loadPrototype` 0x00211B08, `finishProject` 0x00211F90 [C, adhoc_methods.txt]. Scripts load a project by path, for
example `main::manager.loadProject("/projects/message/message")` [C, OpenAdhoc network.ad; same idiom expected in
retail, H]. The option project is entered by setting `main::game.next_menu_project = "option"` (arcade/event top
menus) or `exit_kick_project(context, "option")` (GT mode home) [H, OpenAdhoc; consistent with retail strings].
`GranTurismo4::MenuGameObject::startProject` is in decomp `src/startup/unit_00101400/` [C name].

### 1.2 The option project on the disc (NTSC-U v2.00)

From `SCUS-97328_2.00.txt` [C]:

| File | Size | Role |
|---|---|---|
| `projects/option/option.adc` | 245.8 KB | all option pages' scripts (OptionRoot, ScreenRoot, SoundRoot, ...) |
| `projects/option/option.mproject` | 666 KB | widget trees of all option pages |
| `projects/option/US/rtext.rt2` | 39.4 KB | English text of the option pages |
| `projects/option/US/ScreenRoot.gpb`, `OptionRoot.gpb`, ... | | page images |
| `projects/option2/*`, `projects/option3/*` | | the smaller option menus (arcade quick options and similar) |

Only a `US` language folder exists for `projects/option` on this disc [C, file list]. Languages other than English
on NTSC-U are [U] (not needed for this study).

### 1.3 The option page list

`OptionRoot` builds a table of `OptionPage(name, root, ...)` entries in groups: General (Race, Replay, **Screen**,
Color, Sound), Arcade, Photo mode (Album plus **four `Dummy` slots**), Controller, Misc [H: OpenAdhoc `OptionRoot.ad`;
the retail strings contain the same root names]. The "Screen" page is `ScreenRoot`.

### 1.4 The video-output option ("HiVision" row on the Screen page)

Widget [C, retail `option.mproject` lines 5813-6540]: `ScreenRoot` → `Pane` → `Composite "HiVision"` with
`visible digit {0}` (hidden by default), a `TextFace "label"` (`key "LabelHiVision"`), and an `OptionMenu`
(`tooltip_key "HiVision"`) whose `popup/vbox` holds three items `Normal`, `Mode480p`, `Mode1080i` (each a `Composite`
with a `TextFace` keyed `Normal`, `Mode480p`, `Mode1080i`). The other rows on the page are `Wide` (4:3/16:9),
`Vertical` and `Horizontal` sliders (screen position).

Script behaviour (`ScreenRoot` module) [H: OpenAdhoc `ScreenRoot.ad`; every identifier below is present in the
retail `option.adc` string scan, `option_strings.txt` offsets 144413-149950]:

- `ModeType` module (`M_normal = 0`, `M_480p = 1`, `M_1080i = 2`) and `ModeString = ["normal", "480p", "1080i"]`.
- `onInitialize`: if `MSystem::GetVideoSystem() == "NTSC"` the `HiVision` row is made visible. The string `"NTSC"`
  occurs **once** in the decoded retail `option.adc` (offset 149499) [C].
- `reflect`: selects `ModeType["M_" + option.race_display_mode]`.
- `apply`: `option.race_display_mode = ModeString[index]`, then `option.apply()`, and `main::game.setNeedSave()`.
- Picking 480p or 1080i opens a "Try..." confirmation; `TryModeChange` sets the mode, calls
  `option.applyRaceDisplayMode()` (the screen switches to the new mode as a preview), waits up to 15 s for the
  confirmation, then `option.applyMenuDisplayMode()` returns the menu to its mode. Choosing 1080i also sets Wide to
  16:9. Our own journal (2026-10-13) saw exactly this: the 480p confirmation issues SetGsCrt 0/0x50/1 with a 640x448
  non-halved picture, then menus return to 1/2/1 [C, `docs/journal/2026-10-13.md:80`].

Natives behind it [C, `config/adhoc_methods.txt` and matched decomp sources]:

| Script name | Callback | C++ | What it does |
|---|---|---|---|
| attribute `MOption.race_display_mode` (get and set) | 0x001805D0 (one callback, `argc > 0` = set) | `MOption__get_race_display_mode` | set: string → index through `func_00437380`; get: index → string through `func_004373F0` |
| `MOption.applyRaceDisplayMode` | 0x001899F0 | → `func_00436640(option)` | requests the race video mode from the index |
| `MOption.applyMenuDisplayMode` | 0x00189A28 | → `func_00436740(option)` | requests the menu video mode |
| `MOption.apply` | 0x001899B8 | → `func_004365B8` | applies screen offsets (`func_004A0BE0`), sound and other values |
| `MOption.setDefault` | 0x00189A60 | → `func_004363A0` | defaults; stores 0 at `+0x16B0` |
| `MOption.load` / `save` | 0x00189A98 / 0x00189BD0 | | |
| `MSystem.GetVideoSystem` | 0x001AC598 | | returns "NTSC" on this disc [H] |

Data [C, BUILD.md slice164 and decomp]:

- `0x00622F4C`: owner pointer = the game-data object (garage at +0xBC28, calendar at +0x568, options at +0x38CB0).
  It is the save-data object, so **the option struct, including the race display index, is saved in the GT4 game save
  on the memory card** [H: the same object owns the garage and calendar; the save path itself was not traced].
- `owner + 0x38CB0`: option object (`func_00436278` constructor, `func_004363A0` defaults, vtable `0x00687B60`).
- `option + 0x16B0`: race display index (0 normal, 1 480p, 2 1080i).
- `0x00622DF0`: names table "normal", "480p", "1080i"; `func_00437380` loops exactly 3 entries, so **an unknown name
  is ignored (index unchanged)** [C].
- `func_00436640` (race mode, decomp matched): index 1 → `func_001074D0(&D_006184D0, 2, 6, 640, 448)` (CRT row 6 =
  480p); index 2 → `(2, 7, 576, 960)` plus extra state (1080i); default/0 → `(1, 0, 640, 448)`.
- `func_00436740` (menu mode, decomp matched): **always** `func_001074D0(&D_006184D0, 1, 0, 640, 448)` and returns
  `index != 0`; callers use that return to run `GranTurismo4::LoadingMode("black fade in")` (`func_001103E0`,
  `func_0010BE20`) [C]. So menus are 480i whatever the option says.
- `func_001074D0(state, a1, row, w, h)`: stores a pending display request in the struct at `0x006184D0` (fields
  `+0x24` pending, `+0x2C` a1, `+0x30` row, `+0x34/+0x38` size, `+0/+4/+8` from the CRT table `0x00618648` via
  `func_00106058`) [C]. The code that consumes the pending request and issues SetGsCrt is not identified [U]
  (BUILD.md slice158: reached through a function table from 0x4A03C0, wrappers 0x57FD08/0x57FE00).
- Other callers request interlaced rows too: photo pause `func_003C4928`/`func_003C5430` and media `func_001C89F0`
  call `(1, 2, 640, 448)` [C].

### 1.5 How our runtime calls these natives

- Script natives are invoked indirectly: `hBuiltinMethod` virtual 14 (`func_0032DA40`) loads the callback and `jalr`s
  it [C, script-engine.md]. In our translated code every `jalr` becomes `call(c, t, depth+1)` → `ps2::dispatch`
  [C, e.g. `unit_00CB.cpp:570`]. **No direct call site to 0x001805D0 exists in the slice208 generated code** [C, grep].
- Direct `jal` calls are compiled to direct unit calls that bypass `dispatch`, e.g. `func_00436640` is called as
  `unit_00CD(c, 231u, ...)` from five places and `func_00436740` from seven [C, slice208 `unit_0002/0003/0004/0022/0032.cpp`].
  So a host override in `dispatch` can intercept native callbacks but **cannot** intercept `func_00436640/740`.

---

## 2. Ways to inject our own options

### (a) Edit menu files at load time (host-side substitution of disc data)

How the disc is read [C]: the donor runtime reads sectors through `disc::read`, which calls a `Reader` callback;
`disc::open(name, size, Reader, Probe)` already accepts a custom reader (`disc.cpp:86`). The game reads `GT4.VOL`
itself (its TOC lives in the volume, DEFLATE + XOR 0x55 segments; files are kind 1 raw or kind 2 PS2ZIP)
[C, `docs/VOLUME-READER.md`]. So substitution must work at sector level inside `GT4.VOL`:

- A "virtual GT4.VOL" overlay: a Reader wrapper that serves (1) a rewritten TOC (the changed entries point at new
  extents and carry new sizes) and (2) the new file payloads (PS2ZIP-compressed by us) from sectors appended past the
  real data, or in place when the new payload fits the old extent [H for feasibility; the TOC rewrite and whether the
  game checks anything besides the PS2ZIP size complement are U].
- `scripts/gt4_volume.py` (slice120) already parses and validates the TOC and PS2ZIP; it is the base for a patch
  builder [C that it exists; H that it extends to writing].
- Precedent: Nenkai's GT4Hooks does "HostFS" loose files on GT4 Online by hooking the game [C, README]; we would do it
  host-side, without guest code changes.
- Legal: patched `option.adc` / `.mproject` / `.rt2` are derived from game data and must not be shipped. Ship only a
  **patch recipe** (our own strings, widget fragments, byte-level edits with anchors/hashes) applied by the launcher to
  the user's own disc files during "Preparing game files" (the launcher already extracts disc files there,
  LAUNCHER-PLAN line 110). Owner decision needed on whether a patch derived from OpenAdhoc (GPL-3.0) sources may ship
  (it is data, not the exe; decision 0070 only covers the exe) [Hy, owner call].

What must change: runtime capsule (`system.cpp` opens the disc through the overlay reader) or a native-side reader
installed before boot; a new `docs/inputs` pin for the overlay recipe; a launcher step that builds the overlay;
CTest/Python fixtures (TOC round trip, PS2ZIP round trip, sector map). No producer change.

What each file edit can do:

| Edit | Tooling | Risk |
|---|---|---|
| `rtext.rt2`: relabel `LabelHiVision`, `Normal`, `Mode480p`, `Mode1080i`, the `HiVision` tooltip and the "Try..." texts | RText writer (format documented) | low |
| `option.mproject`: hide or add widgets, add popup items (text format) | text edit + recompress | low to medium (the parser's tolerance of new nodes is H, not C) |
| `option.adc`: same-length constant edit (for example `"NTSC"` → another 4-letter word, so the HiVision row stays hidden as on PAL) | byte patch of the decoded file + recompress | low |
| `option.adc`: new logic (6 entries, new handlers, call a host bridge) | GTAdhocToolchain v5 compile ("supports version 5 (GT4)" in its README, earlier noted as experimental in MENUS-AND-WHEEL) or a hand bytecode patch | **high** until a round-trip gate passes (section 5) |

Note: OpenAdhoc's GT4 `option` project targets the **GT4 Online beta, bytecode version 7**
(`option_us_online_beta.yaml`, `version: 7`) [C]. Its `ScreenRoot` logic matches the retail strings, but it is not a
verified source of the retail v5 file.

Player view: native-looking GT4 page, our labels in GT4's font and style.

### (b) Repurpose the existing HiVision widget

The row, its popup and its 3 items exist. Limits [C]:

- the stored value is the race display index; `race_display_mode` accepts only the 3 names (`func_00437380` loops 3);
- the same index drives the race video mode at race start (`func_00436640`), so it cannot both hold "resolution" and
  keep races at 480p, unless the race path is changed (ELF patch, see (d)) or the index is decoupled by a bridge;
- the script shows "Try..." confirmations for items 1 and 2 and sets 16:9 for item 2;
- 6 presets need 6 popup items (`.mproject`), 6 names (script `ModeString` / `ModeType`), and a storage place.

So pure relabelling gives at most 3 choices and still drags the old semantics (dialogs, 1080i preview). Repurposing is
fine as the **widget** for the new option, but the script must change (a), and the value must go to the host (d).

### (c) Host-side overlay menu (our own UI, ImGui-style)

- Drawn by us, opened by a hotkey (for example F1 or the pad Guide button), or opened when the guest shows the
  Screen page (page detection needs guest-state reading; U).
- CoreGS 1.1: an extra pass before present in `native_vulkan.cpp` [H].
- PCSX2 GS: the picture is a `WS_CHILD` window of `gt4-gs.exe` over our client area [C, GS-LIVE section 1]. Options:
  an owned layered window above the child (no protocol change; may cost independent flip while open) [Hy], or a new
  protocol record carrying an overlay image that gt4-gs composites (fork change by a PCSX2-tainted agent, gt4-gs
  release) [H].
- Input: while open, the host stops feeding the pad to the guest (neutral pad) [H, host_io/window own the input].
- No game data, no producer change, renderer-independent if done as a window.

Player view: a modern panel over the game, clearly "not GT4". Good fallback; not what the owner asked first.

### (d) Guest-side hooks in translated code

Two mechanisms:

1. **Dispatch override (runtime capsule only).** A small table in `dispatch()` maps a guest entry address to a host
   function. It works for targets reached by `jalr` (all script natives) [C, section 1.5]. Uses:
   - override the `race_display_mode` callback 0x001805D0: the setter can store the user's choice host-side and keep
     the guest index at 1 (races always 480p) [H];
   - give scripts a host bridge: override a retail **stub** native, for example `MSystem.SetDrawPerfMeter` 0x001AC238
     and `IsDrawPerfMeter` 0x001AC240 (40-byte empty functions in retail, script-engine.md) [C that they are stubs], or
     register brand-new natives at boot by calling the guest registrar (`func_002F3818`) with a reserved callback
     address that the dispatch table owns [Hy].
   - Returning a value to the script needs a boxed `hInt`/`hString`: call the guest helpers the matched getters use
     (`func_002FE278` boxes an int, `func_00314B20` wraps a string) from the host through `ps2::call` [Hy; the
     callback calling convention is only partly derived, script-engine.md section 6].
2. **ELF patch before translation (producer).** For directly called code (`func_00436640`, `func_00436740`) patch
   instruction words in the pinned reconstructed ELF (a reviewed patch table with addresses, old and new words), then
   regenerate the affected units and re-pin `ps2game.lib`. Examples:
   - race always 480p: at 0x0043665C replace `lw v1,0x16B0(s1)` with `addiu v1,zero,1` [H; word confirmed in BUILD.md];
   - menus progressive: make `func_00436740` request `(2, 6, 640, 448)` instead of `(1, 0, 640, 448)` (two immediates)
     [Hy for the visual result; see section 3].

What must change: dispatch override = runtime overlay capsule (`scripts/prepare_perf_runtime.py`), fixtures, pins;
ELF patch = producer input (new `private/reconstructed` derivative), new library slice and pins (TASK-014 pipeline).
Risk: medium (overrides must exactly honour the callback contract; an exception in a script is fatal in GT4).

### Summary table

| Way | Feasible | Changes | Risk | Effort | Player sees |
|---|---|---|---|---|---|
| (a) data substitution | yes [H] | runtime reader overlay, launcher patch step, pins | low for strings/mproject/same-length adc; high for new script logic | 1-1.5 slices for the overlay; script work extra | native GT4 page |
| (b) repurpose HiVision | partly (3 values, old dialogs) | needs (a) and (d) to be useful | medium | small on top of (a)+(d) | native row, our labels |
| (c) host overlay | yes [H] | native window/present code; PCSX2 path via window or fork | low | 1.5-2 slices | modern panel over the game |
| (d1) dispatch override | yes for natives [C path, H contract] | runtime capsule | medium | 1 slice | invisible plumbing |
| (d2) ELF patch | yes [H] | producer + library pins | medium | 1 slice + full checks | invisible plumbing |

---

## 3. Fixing the game at 480p

### 3.1 The existing mechanism (slice164, TASK-009)

[C, `native/race_display.*`, `native/window.cpp`, BUILD.md slice164/178]:

- `GT4_FORCE_480P=1`: at every guest VBlank (`window.cpp` `on_vblank`, after `pump_messages`) the host reads
  `[0x622F4C]`, adds `0x38CB0 + 0x16B0`, and writes 1 when the index is **0**. Index 2 (1080i) is left alone.
- Pure helpers (`race_display_index_address`, `should_force_480p_index`, `force_race_480p_at_vblank`), fixture
  `native_race_display`. Owner approved (slice164 `--mode quickfix3 --force-480p`, STATUS line 104-105); the launcher
  passes `GT4_FORCE_480P=1` for 480p and `0` for Native 480i (`launch.rs` `game_environment`/`pcsx2_environment`);
  `GT4_RACE_DISPLAY=1080i` is sent by the launcher but not read by the game yet [C].
- Side effect (documented limit 2): choosing "Normal" in the options menu is undone at the next VBlank.

### 3.2 Making 480p permanent and removing the guest option

1. Hold the index at 1 for **any** value (0 and 2), so an old save that holds 1080i also races at 480p. Host-only change
   in `race_display.cpp` (`should_force_480p_index`), fixture update. [H]
2. Hide the HiVision row: same-length patch of the single `"NTSC"` constant in `option.adc` (the row then stays hidden,
   exactly as on non-NTSC systems), through the overlay of 2(a) [H]. The layout with the row hidden is the layout the
   game already uses when `GetVideoSystem` is not NTSC [H]. Overriding `MSystem.GetVideoSystem` instead is **not**
   recommended: other code may use it for timing or region decisions [U].
3. Launcher: drop the `input_resolution` row (480i/480p/1080i) or keep it hidden as a developer switch.
4. With the dispatch override of 0x001805D0 (2(d1)) the guest index can be held at 1 inside the setter itself, which
   replaces the per-VBlank poke [H].

### 3.3 Can menus be progressive too?

- `func_00436740` hard-codes the interlaced row for menus [C]; there is no option value for menus.
- Evidence that menus render correctly at 480p: during the 480p "Try" confirmation the menu page is shown with
  SetGsCrt 0/0x50/1 and a 640x448 picture without halving [C, journal 2026-10-13]. So at least that page draws full
  frames that work progressive [H for all menus].
- Paths: (i) ELF patch of `func_00436740` to request row 6 with `a1 = 2` (producer) [Hy for all screens: movies via
  `func_001C89F0` and photo pause via `func_003C4928/5430` request row 2 separately and would stay interlaced unless
  patched too]; (ii) host rewrite of the pending request in the struct at `0x006184D0` (row/mode fields) at VBlank
  [Hy; slice158 shows that rewriting the CRT table alone changed nothing, and the consumer of the request is U];
  (iii) keep 480i menus and rely on renderer deinterlacing (current state: gt4-gs deinterlace Automatic since
  TASK-047; owner reported soft menus before that fix) [C].
- Owner context: OPTIMIZATION-PLAN records that Spec II on PCSX2 also keeps menus interlaced (owner check).
- Recommendation: treat progressive menus as an optional later phase, with a full menu/FMV/photo-mode owner pass. The
  guest option can be removed without it, because the menu mode never depended on the option.

---

## 4. From an in-game value to the renderers, and persistence

### 4.1 Renderers

| Setting | CoreGS 1.1 (built in) | PCSX2 GS (`gt4-gs.exe`) |
|---|---|---|
| Output (internal) resolution 480p..8K | not available on main: `gpu_init.inc` refuses `PS2_SCALE` other than 1 [C]; the launcher hides CoreGS upscale (decision 0070) | INI `upscale_multiplier` 1/2/3/4/6/12 = presets 480p/720p/1080p/1440p/4K/8K [C, GS-SETTINGS]; read at process start from `--config` |
| FXAA | `GT4_AA` is only a launcher variable; not read by the main game build [C, grep] | INI `fxaa` (Graphics page row, status ok) [C] |
| Fullscreen | `native/window.cpp` borderless toggle (Alt+Enter/F11), live [C]; `GT4_FULLSCREEN` start flag exists on the launcher branch (slice209) [C] | same: the GS child follows the game window via CONTROL resize [C] |
| Texture filtering, anisotropic, CAS | n/a | rows exist but `resolved`, not offered yet [C] |

Live change in the PCSX2 GS: the protocol already defines `CONTROL kind 4 reload_config` [C, GS-STREAM-PROTOCOL 6.1],
but gt4-gs answers it "unsupported" [C, GS-LIVE section 2]. A live change therefore needs a gt4-gs change (fork,
PCSX2-tainted agent, new gt4-gs release and allow-list), or the change applies at the next game start. Whether PCSX2's
GS can switch its upscale multiplier mid-session without artefacts is [Hy] (not checked; clean room). Restarting only
the GS process mid-game is not possible today: the GS state lives in that process and `STATE_SAVE/LOAD` records are
reserved but unused [C].

### 4.2 Persistence and launcher sync

Launcher model [C, `origin/task/019-launcher`]: `%LOCALAPPDATA%\GT4Recomp\config\settings.toml` (`paths.rs`;
`GT4RECOMP_DATA_DIR` overrides), schema 2 with migrations (`config.rs`); one settings table (`settings.rs`) with ids
`renderer`, `input_resolution` (480i/480p/1080i, default 480p), `internal_scale`, `present_filter`, `fsr_sharpness`,
`deinterlacing`, `anti_aliasing`, `fullscreen`, `vsync`, `async_gs`, `performance_report`, `check_vu_code`, plus the
PCSX2 GS overrides from `gs-settings.toml`. The launcher scrubs inherited `GT4_*`/`PS2_*` and passes settings as
environment variables and a session `gt4-gs.ini` (`launch.rs`). It already reads a game-side signal after exit
(`GS_RESTART_MARKER` in the log). LAUNCHER-PLAN line 304 foresees the exe reading the same TOML via `--config`
"so in-game settings menus ... can share it".

Proposed contract (keep the launcher the only writer of `settings.toml`):

1. The launcher passes `GT4_SETTINGS_FEEDBACK=<data>\config\game-changes.toml` (new variable).
2. When the player changes a host setting in game, the game writes that file atomically (id, value, time); ids are
   the launcher's ids (`fullscreen`, `anti_aliasing`, and the GS row `upscale`).
3. On game exit (and at launcher start, for a crash) the launcher validates each value against its table, merges it
   into `settings.toml`, deletes the file, and uses it on the next launch.
4. The guest save stays neutral: the GT4 save keeps index 1 (480p); no host values are written into the memory card
   (a card moved to a real PS2 keeps working) [H].
5. The menu shows the current value from the host state (the game knows its start settings from its environment).

---

## 5. Recommended approach and phased plan

Recommendation: **native GT4 page, built with (a) data substitution + (d1) a dispatch-level host bridge**, with
480p fixed on the host side; (c) overlay only as fallback; menus progressive (d2) as an optional last phase.

| Phase | Content | Slices | Owner test | Proves |
|---|---|---|---|---|
| P0 | Host-only: hold race index at 1 for 0 **and** 2; launcher hides `input_resolution` | 0.5 | race with a save that had 1080i shows 480p | "fixed at 480p" without data changes |
| P1 | VOL overlay reader + launcher patch step + first patch: hide the HiVision row (`"NTSC"` same-length patch in `option.adc`) | 1-1.5 | Options → Screen has no video-output row; other pages, save/load and races unchanged | file substitution works end to end |
| P2 | Relabel + repurpose: the HiVision row becomes "Output resolution" with 6 items (`.mproject` popup items, `rtext.rt2` labels), script edits (6 names, no "Try" dialogs, visible again), host bridge via dispatch override (0x001805D0 setter/getter or a stub native) that stores the preset host-side and keeps the guest index at 1; `game-changes.toml` feedback; applies at next start for PCSX2 GS | 2-3 | pick each preset, leave the menu, restart: the launcher shows the same value; no crash on any option page | the in-menu option reaches the launcher and renderer |
| P3 | More rows: Fullscreen (live, `set_fullscreen`), FXAA (PCSX2 GS), using the same bridge; possibly reuse the empty Photo-mode `Dummy` page slots for a "Graphics" page [Hy] | 1-2 | toggle in game, see the effect | multi-setting page |
| P4 | Live apply in PCSX2 GS: gt4-gs implements `reload_config` (fork, tainted agent, new release, allow-list) | 1 + release | change resolution in the menu, picture changes without restart | live |
| P5 (optional) | Progressive menus: ELF patch of `func_00436740` (and maybe FMV/photo rows) | 1-2 | every menu, FMVs, photo mode, license tests | menus at 480p |

Gate before any script logic edit (P2): compile the unmodified option scripts with GTAdhocToolchain for v5 and compare
with the retail `option.adc` (byte-identical, or identical disassembly). If it does not round-trip, use hand-made
bytecode patches limited to `ScreenRoot`, or fall back to (c) for the new rows while keeping P0/P1.

Smallest first step that proves the idea: **P0 + P1** (about 2 slices): the game is fixed at 480p and the old row is
gone, through the same overlay that P2 needs. If the owner wants to see a label change first, P1 can also patch the
three `rtext.rt2` labels (display only, values unchanged) as a visual check of the text path, but shipping relabelled
items with the old semantics is not recommended.

Effort estimate: P0-P2 about 4-5 slices; full P0-P4 about 6-8 slices plus one gt4-gs release; P5 1-2 more.

### Risks

- Script exceptions are fatal in GT4 (no recovery) [C, hub]: any script edit or bridge mismatch can hang the options
  menu. Mitigation: round-trip gate, headless schedule through Options → Screen, owner pass over all option pages.
- v5 compiler maturity [H risk]; OpenAdhoc GT4 option sources are the v7 Online beta [C].
- VOL TOC rewrite details (where the TOC sits, segment sizes, any checks) [U].
- Callback contract of natives for the bridge (arguments, boxing) only partly derived [Hy].
- Legal/distribution: ship patch recipes, never modified game files; GPL status of OpenAdhoc-derived edits needs an
  owner decision [Hy].
- Live resolution in PCSX2 GS depends on fork work [C dependency].
- Clean room: P4 is fork work for a PCSX2-tainted agent; P0-P3 and P5 can stay clean.

### Open unknowns

1. Who consumes the pending display request at `0x006184D0` and issues SetGsCrt (needed for P5 via host path).
2. Exact GT4.VOL TOC layout for writing (entry fields, segment framing, room for growth).
3. Whether the retail `.mproject` parser tolerates added nodes (expected, since it is a generic tree) [H].
4. Exact retail RText magic/version (`.rt2` vs RT03 template).
5. Whether any non-option script reads `race_display_mode` (a grep over all decoded `.adc` files is the next check).
6. Whether hiding the row leaves a visual gap on the Screen page (expected not, PAL layout) [H].
7. CoreGS 1.1 has no upscale on main; the in-game resolution row should be greyed or hidden when CoreGS is the renderer.

### Owner decisions needed

- OK to remove the guest 480i/1080i choice entirely (and the launcher's `input_resolution` row)?
- Native page (P1-P3) versus host overlay (c)?
- "Applies at next start" acceptable until P4?
- Distribution of the script patch (OpenAdhoc GPL-3.0 provenance).
