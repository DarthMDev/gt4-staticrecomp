# TASK-019 — Launcher, installer and branding: plan

Status: planning study, 2026-10-15. No repository file was changed. Inputs read: CLAUDE.md,
TASKS.md, README.md, docs/BUILD.md, docs/ARCHITECTURE.md, docs/EE-PRODUCER.md,
docs/VOLUME-READER.md, native/THIRD_PARTY.md (main and the TASK-012 worktree),
docs/PARALLEL-GS-PLAN.md (TASK-012 worktree), scripts/play_performance.py,
scripts/play_external_runtime.py, scripts/external_runtime_probe.py, scripts/gt4_volume.py,
Jogar-GT4.cmd, native/window.cpp, native/CMakeLists.txt, donor `runtime/src/iop/{sio2,io,cdvd}.cpp`,
build logs (`.ninja_log`) of the production EE library and of slice 166.

Labels: **Confirmed** (read in code/data or measured), **High confidence**, **Hypothesis**,
**Unknown**. Nothing here is legal advice.

## 0. Owner decisions applied (2026-10-15)

1. **Distribution = option A.** The NSIS installer ships prebuilt executables that contain the
   statically translated game code. The owner accepts the risk. Option B (build the
   game-derived code on the user's PC from their ISO, with a bundled toolchain and an unbranded
   launcher) is kept as the documented fallback if Sony objects. The launcher is designed around
   a `GamePayload` / prepare-step abstraction (section 7.3) so switching to B is a contained change.
2. **Branding = official.** The launcher and the executable icons use the official Gran Turismo 4
   cover art and the game's logo/icon style. Every official asset lives in one folder
   (`branding/official/`) and is listed in one manifest (`brand.toml`); a de-branded build swaps the
   folder for `branding/neutral/` (section 8).
3. **Owner design feedback (round 2).** Clean and minimalist, borrowing the Gran Turismo identity
   (GT4 plus the GT Sport/GT7 look) without copying GT4 screens; far less information everywhere;
   Home = art, logo, Play and a way to Settings only; setup step renamed "Preparing game files";
   memory card created automatically (no choice in setup); Graphics = renderer, resolution (race
   display mode), super-sampling, high-resolution output, deinterlacing and the paraLLEl-GS options;
   Advanced = Asynchronous GS (on), renderer hacks, diagnostics; no frame-pacing option; no variable
   names in the UI. Applied in sections 3, 6 and 12 and in the three mockups.

Consequences to record when TASK-019 starts (documentation changes, not done here):
`TASKS.md` "First stable release" currently says *"no game assets or binaries derived from the
game"*; it must be rewritten to match decision 1, and a numbered decision (`docs/decisions/0067-…`)
should record decisions 1 and 2 with the risk acceptance. Release binaries still never enter git
(CLAUDE.md data rules stay valid): they are built from `private/` and uploaded as release assets.

## 1. Recommendation in one paragraph

A **Rust + Slint** launcher (`GT4Recomp.exe`) installed per-user by an **NSIS** installer into
`%LOCALAPPDATA%\Programs\GT4Recomp` (no admin). First run: the user picks their ISO; the launcher
pre-checks it in under a second (size + `SYSTEM.CNF`), hashes it against the existing pin with a
progress bar, extracts `CORE.GT4`, rebuilds the pinned ELF (both hash-checked), optionally extracts
the 64 in-game photo JPEGs for the home background, creates the memory-card location and writes a
receipt. Later runs open straight to the home screen: full-bleed art, the logo, a **PLAY** button
and a Settings icon, nothing else. Settings has three short pages (Graphics, Display, Advanced) in a
GT7-style option list. The race display mode (Native 480i / 480p / 1080i) is a setting with 480p as
the default, held by the game itself. The launcher starts the game executable with validated
settings passed as environment variables on the child process only, supervises it, backs up the card,
and turns a crash into a readable report. paraLLEl-GS moves into its own DLL to satisfy LGPL-3.0
relinking cleanly.

## 2. How the game is launched today (Confirmed, from the scripts)

`Jogar-GT4.cmd` → `play_external_runtime.py` (or `play_performance.py --mode main`) →
`external_runtime_probe.run_probe`, which:

- verifies the runtime exe and dependencies against `docs/inputs/<slice>.json`;
- verifies `private/fingerprint-check/CORE.GT4` (pin `85d26aa8…`, 2,020,861 bytes),
  `private/reconstructed/SCUS_973.28.elf` (pin `10f82e22…`, 6,123,004 bytes, from
  `usa-v2.00-native.json`) and **the whole ISO** (`67b6c007…`, 5,314,478,080 bytes) on every launch;
- copies the ELF into a fresh session directory as `SCUS_973.28` (argv0 must equal the disc boot
  name; donor `system.cpp:353` builds `cdrom0:\SCUS_973.28;1` from the file name);
- copies a pinned seed card on first play, otherwise uses `private/manual-play/card.ps2`, and keeps
  `card-before.ps2`; a byte lock (`session.lock`) prevents two sessions;
- scrubs inherited `PS2_*`, `GT4_PERF_*`, `GT4_VU1_*`, `GT4_VU0_*`, `GT4_ASPECT`…, forwards and
  records `GT4_PACING`, `GT4_PRESENT_MODE`, `GT4_GS_SYNC`, `GT4_FORCE_480P`;
- runs `gt4vulkan.exe <session>\SCUS_973.28 --ntsc --disc <iso> --frames 0 --memcard <card>
  --misses <session>\misses.txt --window`, cwd = session dir, stdout+stderr → `probe.log`;
- decides success by the last `[ee] stopped after N frames, C cycles: <reason>` line
  (`window closed` = normal exit) and writes `result.json`.

The launcher reproduces exactly this contract natively, minus the developer-only modes.

### Release blockers found in the native code (Confirmed)

| Finding | Where | Needed change |
|---|---|---|
| Shader directory is an absolute path baked at compile time | `native/CMakeLists.txt:336` `GT4_VULKAN_SHADER_DIR="${SHADERS}"`, used by `gpu_init.inc:7` | Resolve shaders relative to the exe (`<exe dir>\shaders`) in release builds; keep the baked path for dev builds. (Slice 165 already broke on this.) |
| No window/exe icon, no version resource | `window.cpp:241-247` (no `hIcon`), no `.rc` in CMake | `.rc` with icon + VERSIONINFO; `WNDCLASSEXW.hIcon/hIconSm` from resource 1 |
| Start-in-fullscreen throws | `window.cpp:239` `open_window(true)` → "not qualified" | `GT4_FULLSCREEN=1` that applies the existing Alt+Enter borderless path after creation |
| 480p is opt-in and binary | `GT4_FORCE_480P` unset = off (`window.cpp:291`); the hold writes index 1 only when the index reads 0 | Generalize to `GT4_RACE_DISPLAY=480i\|480p\|1080i` (default `480p` when unset): at each guest VBlank hold the race display index (`[0x622F4C]+0x38CB0+0x16B0`) at 0, 1 or 2. Menus stay as the game draws them. Keep `GT4_FORCE_480P` as an alias for one release. Index 2 = 1080i is **High confidence** (BUILD.md: "a user choosing 1080i keeps it"); confirm in game. |
| CRT is `/MD` | worktree `native/THIRD_PARTY.md` notes | Ship `vcruntime140*.dll`/`msvcp140.dll` app-local (Microsoft "Distributable Code") or build `/MT`; decide once |
| Crash evidence is only the log | — | Unhandled-exception filter writing a minidump (`MiniDumpWriteDump`) into the session folder |

## 3. First-run flow

### 3.1 What is needed from the ISO, and when (Confirmed unless marked)

| Item | Setup (once) | Run time |
|---|---|---|
| Disc identity (size + SHA-256 = `usa-v2.00.json`) | Full hash once, with progress | Cheap recheck: path, size, last-write time, NTFS file ID; full rehash only if any changed |
| `CORE.GT4` → `%LOCALAPPDATA%\GT4Recomp\prepared\CORE.GT4` | Extract (ISO9660), verify pin | Not read by the exe (only the ELF is) |
| Reconstructed ELF → `prepared\SCUS_973.28` | Raw-deflate CORE, rebuild layout per `usa-v2.00-native.json`, verify pin | Passed as argv[1] |
| `GT4.VOL`, `IRX/`, `IOPRP300.IMG`, everything else | — | Read **from the ISO** by the exe via `--disc` (streamed; no extraction) |
| VU1/VU0 microcode images | **Not needed under A** (the game uploads microcode from the ELF; compiled programs are matched by identity digest) | — |
| In-game photos `GT4.VOL: photo/000.JPG … 063.JPG` (64 JPEGs, 1280x960, ~10.9 MB) | Extracted to `art\` as part of "Preparing game files" (no separate step; Confirmed: plain JFIF inside PS2ZIP entries) | Read by the launcher only (Home background) |

**PS2 BIOS: not required — Confirmed.** The donor runtime has no BIOS loader: `rom0:` modules are
HLE (`iop/cdvd.cpp:169`), `ROMVER` is synthesized (`iop/io.cpp:57`), the launcher passes no BIOS
argument and no BIOS file is pinned. (Not shown in the UI; it belongs in the README/FAQ.)

### 3.2 Identity check and friendly rejection

1. **Pre-check (< 1 s).** Reject at once if the extension/format is not a 2048-byte-sector ISO
   (`.bin/.cue`, `.chd`, `.cso/.zso`, `.gz`, `.7z`: "Please convert to a plain .iso"). Read the
   ISO9660 primary volume descriptor and `SYSTEM.CNF` (`BOOT2`, `VER`, `VMODE`).
   - `BOOT2` not `cdrom0:\SCUS_973.28;1` → "This is not Gran Turismo 4 USA (SCUS-97328)". If the
     serial is a European one (`SCES…`): "PAL versions are not supported"; if the volume/serial
     identify *Spec II*: "Gran Turismo 4 Spec II is not supported (yet)"; other serials: show what
     was found.
   - `VER` ≠ `2.00` → "This is GT4 USA version X; only the v2.00 disc is supported."
   - Size ≠ 5,314,478,080 → "This copy is incomplete or modified."
   (Owner's `Gran Turismo 4 Spec II v1.10.iso` is a ready negative fixture.)
2. **Full hash** (SHA-256 with SHA-NI/AVX2 via the `sha2` crate's asm backend). Speed is disk-bound:
   NVMe a few seconds, SATA SSD ~10-20 s, HDD ~45-90 s (**Hypothesis**, measure). Shown as a thin
   progress bar with a percentage ("Checking disc"). Mismatch with correct serial/version → "This dump differs from
   the known good v2.00 disc (damaged or patched). Please re-dump your disc."
3. **Preparing game files (shown as "about a minute"):** extract `CORE.GT4`, rebuild the game's ELF
   from it (a fixed-layout reconstruction: nothing is compiled and the executable is not touched),
   extract the disc photos, verify the CORE and ELF pins. Any mismatch here is an internal error (the
   disc hash already matched) and is reported as a bug.
4. **Memory card**, created automatically (section 3.3). 5. **Receipt**
   `prepared\prepare.json` (launcher version, payload kind, disc path/size/file-ID/mtime/hash, CORE and
   ELF hashes, timestamps).

Never copy the ISO by default; offer "Move/copy my ISO into GT4Recomp's folder" only as an option.
If the ISO later moves, the home screen shows "Disc image not found — Locate…" instead of PLAY.

### 3.3 Memory card (Confirmed from donor `sio2.cpp:173-179, 322-333`)

A missing card file is presented to the game as a **new, unformatted** 8,650,752-byte card and is
written on save; GT4 then offers to format it. So the launcher does not need any seed card (the pinned
seeds stay private evidence). Layout under `%LOCALAPPDATA%\GT4Recomp\memcards\`:
`Mcd001.ps2` (slot 1), `backups\Mcd001-YYYYMMDD-HHMMSS.ps2` (copy before each session, keep the last
10). Setup offers no choice: the card is simply created. **Import** of an existing PCSX2 8 MB `.ps2`
card (same size and raw page+ECC layout, **High confidence**; validate size and the
`Sony PS2 Memory Card Format` superblock magic) is a later feature under Settings › About, not part
of setup or v1. Hold the card lock (same byte-lock design as `session.lock`) while the game runs.

**Hypothesis to check during TASK-019:** the owner's game boots in 16:9 because the seed card has the
game's wide option set; a fresh card may show a 4:3 image in the fixed 16:9 window until the player
enables 16:9 in GT4's options. Either show a one-time tip or add a hold similar to the 480p hold.

### 3.4 Progress UI, errors, repair

- One list of four rows: **Disc** (name, region, version), **Checking disc** (progress bar),
  **Preparing game files** ("about a minute"), **Memory card**. Each row shows only a tick, a
  percentage or "Not supported". Cancel is always available and leaves no partial output (write to
  `prepared.tmp\`, atomic rename at the end).
- Errors are one or two plain sentences ("what happened / what to do"); technical detail goes to the
  log only.
- **Repair** (Settings › About): re-verify installed binaries against the release manifest
  (hash list shipped with the installer, < 1 s for ~110 MB); re-run setup from the ISO; never touches
  memory cards. Under A a damaged binary means "reinstall" (link opens the release page).
- **Re-run setup** = "Change disc image…" (same flow, keeps saves).
- Pre-flight before PLAY: Vulkan 1.2 device with `VK_EXT_swapchain_maintenance1` (BUILD.md
  requirement) checked through `ash`; failure message names the GPU and suggests a driver update.

## 4. Distribution model

### 4.1 What the shipped exe contains (Confirmed)

`gt4vulkan.exe` (slice 166: 95,529,472 bytes) statically links `ps2game.lib` (326 EE units, 86 MB of
generated C++ derived from the game's MIPS code) and the generated VU1 (2,048 entries) and VU0 (14
entries) programs, derived from the game's encrypted microcode. Under A this binary is distributed.

### 4.2 Comparison

| | **A — prebuilt (chosen)** | **B — build on the user's PC (fallback)** | **C — hybrid: encrypted payload** |
|---|---|---|---|
| What ships | Launcher + game exe (with translated code) + pgs DLL | Launcher + runtime host exe (no game code) + translator + VU recompiler + clang-cl/lld-link | As A, but the game-code DLL is encrypted with a key derived from the user's `CORE.GT4`/ELF hash |
| Installer size | ~35-45 MB compressed (**Hypothesis**; exe ~96 MB, compresses well) | ~80-100 MB compressed (toolchain ~190 MB unpacked: `clang-cl.exe` 104.7 MB + `lld-link.exe` 71.4 MB + a header subset; Confirmed file sizes of LLVM on this machine) | As A |
| First-run time | ISO hash + extract: ~0.5-2 min | Above + translate + compile: game-derived compile is ~2,000 CPU-s (EE units 811 CPU-s, Confirmed from `reference-acceleration-study/runtime-build/.ninja_log`; VU1 parts 1,172 CPU-s, Confirmed from slice 166 `.ninja_log`) → ~4-6 min on 16 threads, ~10-20 min on 4 threads (**Hypothesis**) | As A + decrypt (seconds) |
| Machine needs | None beyond the game | 8 GB RAM recommended (jobs capped by free RAM), ~3 GB temporary disk, all cores busy for minutes | As A |
| Failure modes | Binary rejected by AV/SmartScreen; GPU driver | AV quarantining freshly linked binaries (high risk for unsigned new files), out-of-disk/RAM, laptop thermal throttling, non-ASCII user paths, toolchain/CRT mismatch; result is still verifiable (generated-source inventory hash `7a1a0909…` and pinned toolchain → reproducible objects) | As A |
| Legal posture (not legal advice) | Distributes code derived from the game: copyright-takedown exposure; using "Gran Turismo" marks adds trademark exposure | Distributes only original/MIT/LGPL code; game-derived code exists only on the user's PC — the conservative model | Still distributes a derivative work, just encrypted; obfuscation is not a legal shield (**Hypothesis**) but makes "needs your own disc" technically true |
| Engineering cost | Lowest; current qualified build | High: game code must become a **freestanding DLL** (no CRT/STL; imports runtime symbols from the host exe) because MSVC/UCRT `.lib` files are not redistributable; `ee_producer`/VU steps ported or Python embedded; performance of the DLL boundary must be measured | Medium: same DLL split as B, no compile on the user side |

**Comparable projects** (High confidence unless marked; not legal advice): *Zelda64Recomp*
(N64Recomp) and *Sonic Unleashed Recompiled* (XenonRecomp) publish prebuilt executables that contain
the recompiled game code and ask the user for their own ROM/game files at first run for assets —
the same UX and the same model as A. No takedown of either is known to me as of my knowledge cutoff
(**Hypothesis** that this still holds). The *Super Mario 64 PC port* era saw DMCA takedowns of GitHub
repositories distributing builds (High confidence). Sony's posture toward recompilation projects is
**Unknown**.

**Risk reducers for A (recommended):** host release binaries **outside** the source repository (a
separate `gt4recomp-releases` repo or other host) so a DMCA notice against binaries does not take the
source repo and its history down; never ship ISO-derived data files (ELF, CORE, photos) — those are
always produced on the user's PC; keep B's DLL split prototyped (milestone M10) so the fallback is real.

**GPL interaction (important):** under A the distributed exe combines our code with code we cannot
license (the game's). LGPL-3.0 (paraLLEl-GS) permits this with relinking (section 4.3). **GPL-3.0
code inside the game exe does not** — e.g. importing PCSX2 GS code (decision 0066) or the ARMSX2
pipeline planned for Android (TASK-021) into a binary that also contains translated game code would
make that binary undistributable under GPL terms (**High confidence**). Under A, GPL components must
stay out of the game binary (or Phase 2 uses B-like delivery). This needs an owner decision before
TASK-021.

### 4.3 Recommendation and its consequences

A as decided, with three structural choices that keep B one step away:

1. Launcher logic is written against a `GamePayload` trait (section 7.3). A = `BundledPayload`
   (verifies hashes of shipped files); B = `BuildFromIsoPayload` (runs translator + compiler). The UI
   only sees "prepare steps" with progress, so B adds steps, not screens.
2. paraLLEl-GS in a DLL now (needed for LGPL anyway).
3. Fallback branding: B ships with `branding/neutral/` and name "GT4Recomp" only.

Installer: ~40 MB, first run ~1 min. If B is ever needed, installer grows to ~100 MB and first run
to 5-20 min with a "Building the game for your PC…" step.

## 5. Licenses in a binary release

| Component | License | Obligation in a binary release |
|---|---|---|
| paraLLEl-GS `cc6184af…` | LGPL-3.0-or-later | LGPL §4: prominent notice that the library is used and is LGPL; ship GPL-3.0 and LGPL-3.0 texts; show its copyright notice where we show notices; allow relinking: **ship it as a shared library (`gt4_parallel_gs.dll`) — LGPL §4(d)(1)** — the user can drop in a rebuilt DLL with the same C ABI. Provide the exact corresponding source (pinned commit tarball + our patch files + build script) next to the release (GPL §6 via LGPL). Static linking would instead require shipping all other object files (including game-derived ones) for relinking — avoid. |
| Granite, volk (MIT) | MIT | Keep copyright + license text (they move into the DLL with paraLLEl-GS and our thin backend) |
| Vulkan-Headers | Apache-2.0 OR MIT | Notice |
| ps2-web-recomp donor `1b480879…` | MIT | Copyright + license text (runtime, translator, VU recompiler derivative) |
| naga (shader build tool, not shipped) | MIT OR Apache-2.0 | None for the release unless shipped |
| Rust crates of the launcher | mostly MIT/Apache-2.0 | Generated with `cargo about` into `licenses\launcher-crates.html` |
| Slint | GPLv3 / Royalty-free 2.0 / commercial | Royalty-free license for desktop apps requires attribution ("Made with Slint" in About) — or GPLv3 if the launcher is GPL. Choose with the project license question. |
| MSVC runtime DLLs (if `/MD`) | Microsoft Distributable Code | App-local copy allowed; include no headers/libs |
| NSIS | zlib/libpng-style | No notice required in the installer output |
| **GT4Recomp itself** | **none yet — the repo has no LICENSE file (Confirmed)** | Must be chosen before release (open question 1) |

DLL ABI: a small `extern "C"` API (init with our `VkDevice`/queues, `gif_transfer`,
`write_register`, privileged register state, `read_transfer_fifo`, `map_vram_*`, `vsync`,
`set_super_sampling`, `set_hacks`, signal callbacks). Our glue (`parallel_gs_backend.cpp`) moves inside
the DLL. Cost: one indirect call per GIF transfer (negligible; **Hypothesis**, measure in F8).

**Launcher "Licenses" screen:** About → Open-source licenses: list of components (name, version/commit,
license, link), full texts rendered from `licenses\*.txt`, the LGPL relink note with a link to the
source package, and the trademark/ownership notice ("Gran Turismo and the Gran Turismo 4 artwork are
trademarks/copyright of Sony Interactive Entertainment / Polyphony Digital; GT4Recomp is an
unofficial fan project, not affiliated or endorsed").

## 6. Settings model

There are three short pages. The UI shows no variable names. Each control maps to exactly one variable (the A/B rule), all of them listed in one Rust table. Settings save as soon as they change and take effect at the next Play.

### 6.1 Graphics

| Group | UI control | Values (default **bold**) | Variable / paraLLEl-GS field |
|---|---|---|---|
| Image | Renderer (dropdown) | **paraLLEl-GS**, Legacy | `GT4_RENDERER=parallel\|donor` (Confirmed in the TASK-012 worktree) |
| Image | Resolution (dropdown): race display mode. Helper: "480p is recommended: it is progressive. Native and 1080i are interlaced, which looks softer and flickers." | Native (480i), **480p**, 1080i | `GT4_RACE_DISPLAY=480i\|480p\|1080i` (new; generalizes `GT4_FORCE_480P`, see section 2) |
| Image | Super-sampling (segmented) | Off, 2×, 4×, 8×, 16×. The default is chosen after the TASK-012 measurements; the mockup shows 4×. | `GT4_PGS_SSAA=1\|2\|4\|8\|16` → `SuperSampling::X1..X16` (Confirmed enum, `gs_renderer.hpp:232`) |
| Image | High-resolution output (toggle, disabled below 4×; helper "Needs 4× or more.") | **On** at ≥4× | `GT4_PGS_HIGHRES` → `VSyncInfo::high_resolution_scanout`, which also needs `force_progressive` (Confirmed, `gs_renderer.cpp:4294-4310`) |
| Image | Super-sampled textures (toggle) | **Off** | `GT4_PGS_SS_TEXTURES` → `set_super_sampling_rate(…, super_sampled_textures)` |
| Interlacing | Deinterlacing (dropdown; helper "For screens the game draws interlaced, such as menus.") | **Progressive**, Weave (motion-adaptive), Off | Progressive = `force_progressive`. Weave = paraLLEl-GS's built-in deinterlacer: the `weave` program, plus a motion-adaptive pass over the last 4 fields labelled "fastmad" in the source (Confirmed, `gs_renderer.cpp:5106-5190`). Off = `skip_deinterlace` (raw fields, shown as they are) |
| Interlacing | Reduce field blending (toggle) | **On** | `anti_blur` (the paraLLEl-GS header calls it "usually a good idea" with progressive or SSAA) |

### 6.2 Display

| Group | UI control | Values (default **bold**) | Variable / field |
|---|---|---|---|
| Window | Fullscreen (toggle; helper "Alt+Enter or F11 switches while playing.") | **On** | `GT4_FULLSCREEN` (new) |
| Window | VSync (dropdown) | **On** (fifo), Fast (mailbox), Off (immediate). Modes the surface lacks are hidden, based on a Vulkan surface query. | `GT4_PRESENT_MODE` (Confirmed) |
| Picture | Show overscan | **Off** | `VSyncInfo::overscan` |
| Picture | Use the game's screen offsets | **Off** | `crtc_offsets` |
| Picture | Match the game's horizontal resolution | **On** (to be confirmed by measuring on GT4) | `adapt_to_internal_horizontal_resolution` |
| Picture | Direct scanout | **Off** | `raw_circuit_scanout` |

### 6.3 Advanced (kept short)

| Group | UI control | Default | Variable / field |
|---|---|---|---|
| Performance | Asynchronous GS | **On** | `GT4_GS_SYNC`: unset or 0 = on (Confirmed). Turning it off writes `GT4_GS_SYNC=1`. Under paraLLEl-GS this switch must map to that renderer's own submission path; how is **Unknown** until TASK-012 step 2 settles it. |
| Renderer hacks ("may cause glitches") | Disable mipmaps · Back-buffer promotion · Blend demotion · Unsynced readbacks | all **Off** | `GT4_PGS_HACKS=` comma list → `Hacks::{disable_mipmaps, backbuffer_promotion, allow_blend_demote, unsynced_readbacks}` (Confirmed struct, `gs_interface.hpp:211-237`) |
| Diagnostics | Performance report (F7/F8) | **Off** | `GT4_PERF=1` |
| Diagnostics | Check compiled VU code | **Off** (choices: Off / every 100th / every 1000th call) | `GT4_VU1_VERIFY`, `GT4_VU0_VERIFY` |

These are deliberately left out of the UI:
- **Frame pacing:** always paced. The launcher never sets `GT4_PACING`; the audio-clock A/B stays a developer switch in the Python launcher.
- **Developer and debug switches:** `PS2_VU_INTERPRET`, `PS2_SCALE`, paraLLEl-GS `ordered_super_sampling` (a debug aid) and `DebugMode`.
- **Controls remapping:** comes later; racing wheels are TASK-018.
- **CPU build variant:** chosen automatically from CPUID. Only the baseline x86-64 build ships until TASK-010 shows that x86-64-v3 helps.

### 6.4 How the launcher passes settings

- **Environment variables on the game process only.** Nothing changes in the system or user environment. The launcher starts from a scrubbed environment (it drops every inherited `GT4_*`/`PS2_*`, as `external_runtime_probe` does) and then sets the renderer plus any value that differs from its default.
- **One Rust table is the single source of truth.** Each entry lists: setting id → variable → allowed values → default → page → "requires" rules. Examples of rules: high-resolution output needs ≥4× and progressive; the two verify modes exclude each other. Values are validated on load and again before launch; an invalid value resets to its default.
- **Storage:** `%LOCALAPPDATA%\GT4Recomp\config\settings.toml`, with a `schema_version` and migrations.
- **Launch record:** each launch writes `launch.json` (arguments, variables, versions, hashes) into the session folder.
- **Later (optional):** the exe reads the same TOML through `--config`, so in-game settings menus and the Android build can share it.
- **Native work this implies:** `GT4_RACE_DISPLAY`, `GT4_FULLSCREEN` and the `GT4_PGS_*` names. TASK-012 owns the paraLLEl-GS ones. Each must refuse unknown values, as the existing variables do.

## 7. Launcher architecture

### 7.1 UI toolkit choice

| Toolkit | Look & motion | Maturity on Windows | Portability (Android/iOS later) | Verdict |
|---|---|---|---|---|
| **Slint** | Declarative `.slint` markup, built-in property animations, states/transitions, custom fonts, image effects (blur/drop shadow, rounded clipping), live preview for design iteration | Stable (1.x), native Windows with Skia or FemtoVG renderer, high-DPI | Android supported; iOS supported in recent releases (**High confidence**); embedded too | **Chosen** |
| egui | Immediate mode; polishable but animations/typography need hand-work; looks like a tool unless heavily customized | Very stable | Android possible, iOS weak | Good for debug panels, not for the polished front |
| iced | Elm architecture, good theming; animation support still limited (**High confidence**) | Stable-ish, APIs churn between releases | Mobile not first-class | Second choice |
| GPUI (Zed) | Beautiful, GPU-native | Windows support is recent, API unstable, few docs outside Zed | No mobile | Too risky now |

Slint's licensing (Royalty-free 2.0 with attribution, or GPLv3) fits either project-license answer.

### 7.2 Crate layout (`launcher/` Cargo workspace inside the repo)

```
launcher/
  Cargo.toml                # workspace, pinned Cargo.lock
  crates/
    gt4l-core/              # no UI: paths, settings schema, config TOML, logging, card manager,
                            #   release manifest verification, single-instance, launch/supervise
    gt4l-disc/              # ISO9660 reader, SYSTEM.CNF parse, SHA-256 with progress,
                            #   CORE.GT4 raw-deflate + ELF rebuild (port of ee_producer prepare),
                            #   GT4.VOL catalogue reader (port of gt4_volume.py subset) for photo/*.JPG
    gt4l-payload/           # trait GamePayload + BundledPayload (A); BuildFromIsoPayload stub (B)
    gt4l-app/               # Slint UI (ui/*.slint), app state machine, theme, brand loading
      build.rs              # embed-resource: icon + VERSIONINFO from $GT4_BRANDING_DIR
    gt4l-cli/               # headless: `gt4l-cli prepare --iso X --data-dir Y`, `verify`, `launch --dry-run`
  tests/                    # synthetic ISO fixtures, parity tests vs Python (private, skip if absent)
installer/
  gt4recomp.nsi, pages/*.nsh, art/ (bitmaps generated from branding/)
branding/
  official/  brand.toml + assets      neutral/  brand.toml + original assets
```

Key crates: `slint`, `sha2` (asm), `flate2` (raw deflate, `miniz_oxide` or `zlib-rs` backend),
`serde` + `toml`, `tracing` + `tracing-appender` (rolling `launcher.log`), `windows` (CreateProcessW,
Job Objects, named mutex, `SHGetKnownFolderPath`, file IDs), `rfd` (native file dialog), `ash`
(Vulkan pre-flight), `raw-cpuid`, `image` (JPEG decode/resize to cached 1920x1080 WebP/PNG),
`fs4` (file locks), `embed-resource`.

### 7.3 Payload abstraction (the A→B switch)

```rust
trait GamePayload {
    fn describe(&self) -> PayloadInfo;                  // kind, version, expected files
    fn prepare_steps(&self, ctx: &PrepareCtx) -> Vec<Step>; // UI renders these generically
    fn verify(&self, ctx: &PrepareCtx) -> Result<Verified, PayloadError>; // fast check per launch
    fn game_executable(&self, ctx: &PrepareCtx) -> PathBuf;
}
```

Common steps (both kinds): `PrecheckDisc`, `HashDisc`, `ExtractCore`, `RebuildElf`, `PrepareCard`,
`ExtractArt`, `WriteReceipt`. A adds `VerifyBundledBinaries`. B adds `Translate` (donor translator →
generated sources, inventory hash check), `ExtractVuImages`, `RecompileVu`, `CompileGameDll`
(clang-cl, job count from cores and free RAM), `LinkGameDll` (lld-link, imports from the host exe),
`VerifyGameDll`. Selection is a build-time feature (`payload-bundled` / `payload-build`), so the B
launcher contains no reference to the official branding or bundled payload.

### 7.4 Paths

| What | Where |
|---|---|
| Program (installer-owned, replaced on update) | `%LOCALAPPDATA%\Programs\GT4Recomp\` → `GT4Recomp.exe`, `game\gt4recomp-game.exe`, `game\gt4_parallel_gs.dll`, `game\shaders\*.spv`, `branding\`, `licenses\`, `release-manifest.json`, `uninstall.exe` |
| User data (survives updates/uninstall unless asked) | `%LOCALAPPDATA%\GT4Recomp\` → `config\settings.toml`, `prepared\` (CORE, ELF, receipt), `memcards\`, `art\`, `logs\launcher.log`, `sessions\session-YYYYMMDD-HHMMSS\` (keep last 20), `cache\` (Vulkan pipeline cache) |

`%LOCALAPPDATA%` (not Roaming) avoids syncing 8 MB cards and 6 MB ELFs through roaming profiles/
OneDrive. A "Portable mode" (a `portable.txt` next to the exe redirects data to `.\data`) is cheap to
add and handy for testing.

### 7.5 Launch, supervision, crash report

- Single instance: named mutex `Local\GT4Recomp.Launcher`; a second start activates the first window.
  Game lock: byte lock on `memcards\session.lock` (same as today) so two games never share a card.
- Launch: verify payload (fast), cheap disc recheck, back up card, create session folder, write
  `launch.json`, `CreateProcessW(game\gt4recomp-game.exe, "prepared\SCUS_973.28 --ntsc --disc <iso>
  --frames 0 --memcard memcards\Mcd001.ps2 --misses <session>\misses.txt --window")`, cwd = session,
  stdout/stderr to `<session>\game.log`, process in a Job Object (so "Stop game" works and children die
  with it). The ELF is passed in place (`prepared\SCUS_973.28`, the name argv0 needs) instead of
  copying it per session.
- While the game runs, Play becomes "Running · Stop" (nothing else) and the launcher can
  minimize to the taskbar; option "Close launcher when the game starts" (off by default, because the
  launcher reports crashes).
- Exit classification: exit code 0 and `[ee] stopped … window closed` = normal. Anything else = crash
  dialog: what happened in one sentence (reason from the `[ee] stopped` line, e.g. "no translated code
  at 0x…"), no log text on screen, buttons **Open session folder**, **Copy report** (zip of `game.log`,
  `launch.json`, `misses.txt`, minidump if present; card and ISO excluded; user name in paths
  replaced by `%USER%`), **Play again**. Nothing is uploaded automatically.

### 7.6 Updates (later)

Opt-in "Check for updates" (GitHub Releases API of the releases host, at most daily). When a newer
version exists the launcher shows the notes and asks before downloading the installer to a temp
folder; it then runs it silently (`/S`) into the same per-user folder and restarts. Data is untouched.

## 8. Branding and assets

### 8.1 One folder, one manifest

`branding/official/brand.toml` (and `branding/neutral/brand.toml` with the same keys):

```toml
display_name   = "Gran Turismo 4"          # neutral: "GT4Recomp"
product_name   = "GT4Recomp"
tagline        = "Native PC recompilation"
accent         = "#2C6BED"   # focus ring only
[assets]
cover_front    = "cover-front.png"   # 1200x1700 (shown at 600x850)
logo_wordmark  = "logo-gt4.png"      # transparent, ~1600 px wide (or .svg)
emblem         = "emblem-gt.png"     # square, transparent, 1024x1024 (icon master)
launcher_icon  = "launcher.ico"
game_icon      = "game.ico"
hero_fallback  = ["hero-01.jpg", "hero-02.jpg"]   # 1920x1080, used when no disc art was extracted
installer_welcome = "nsis-welcome.bmp"            # 164x314 (MUI2), plus @2x 328x628
installer_header  = "nsis-header.bmp"             # 150x57, plus @2x
```

The launcher loads the folder at run time (missing file → embedded neutral fallback); the two `.ico`
files are compiled in (launcher via `build.rs`, game via the `.rc`) from `GT4_BRANDING_DIR`, so a
de-branded build is: point `GT4_BRANDING_DIR` at `branding/neutral`, rebuild, repackage. A CI check
greps the neutral package for any file hash listed in the official manifest.

### 8.2 Official asset slots and their sources

| Slot | Used in (mockup id) | Source / how we obtain it |
|---|---|---|
| `cover_front` | Setup left column, About page, installer welcome bitmap (cropped) | **Not on the disc** (Confirmed: the ISO root holds only CORE.GT4, GT4.VOL, IRX, NET, EPSON, IOPRP300.IMG, SCUS_973.28, SYSTEM.CNF). Owner supplies a clean scan/photo of the NTSC-U box front (own copy), color-corrected, ≥1200x1700. |
| `logo_wordmark` | Home bottom-left above Play, Setup header, Settings nav | Owner-supplied vector/PNG of the GT4 logo (trace from the cover scan). In-disc title textures exist as Tex1 `.img` files in `GT4.VOL` (e.g. `advertise/loading.img`, `advertise/us/main.img`), but decoding Tex1 is new work and which file holds the logo is **Unknown** — not planned for v1. |
| `emblem` → `launcher.ico`, `game.ico` | Taskbar, Task Manager, Start Menu, desktop shortcut, window title bar, Alt+Tab | Derived from the official GT emblem/logo style (owner-supplied master). Generated by a script (`scripts/make_icons.py` with Pillow) into multi-size ICO: 16, 20, 24, 32, 40, 48, 64, 96, 128, 256 (256 PNG-compressed). Launcher and game get **distinct** variants (e.g. game = emblem on dark tile, launcher = emblem with a small chevron badge) so the two taskbar buttons are distinguishable. |
| Home background | Home (full-bleed, one cross-fade every 15 s) | **From the user's own disc at setup**: `GT4.VOL photo/000.JPG…063.JPG` (64 in-game photos, 1280x960 JPEG, Confirmed by extraction in this study), resized/cropped to 16:9 and cached in `%LOCALAPPDATA%\GT4Recomp\art\`; never shipped. Before setup completes, `hero_fallback` (official screenshots or neutral art) is used. |
| Save icon (Saves page, optional) | Settings → Saves | `GT4.VOL icon/gamedata.ico` is a PS2 3D save icon, not a Windows icon; rendering it is optional future work. |
| Course/car thumbnails (future) | — | `piece/crs_pict`, `piece/car_pict` Tex1 textures; needs the Tex1 decoder (PDTools, MIT reference). Not in v1. |

Embedding in the game exe (CMake): `native/gt4recomp.rc.in` → `configure_file` with
`IDI_APP ICON "@GT4_BRANDING_DIR@/game.ico"` and `VS_VERSION_INFO` (`FileDescription` =
"Gran Turismo 4 — GT4Recomp" or "GT4Recomp" in neutral; `ProductVersion`, `CompanyName` empty,
`LegalCopyright` with the unofficial-project notice); `enable_language(RC)` (llvm-rc with clang-cl).
Task Manager shows `FileDescription`; the taskbar uses the window icon, so `window.cpp` must register
`WNDCLASSEXW` with `hIcon`/`hIconSm` loaded from resource `IDI_APP`. Set an explicit
AppUserModelID per process (`GT4Recomp.Launcher`, `GT4Recomp.Game`) and the same IDs on the Start Menu
shortcuts so pinning works.

Trademark note: using the Gran Turismo name and logo makes the release look official; keep the
"unofficial, not affiliated" line visible on the About page and in the installer's first page.

## 9. Installer (NSIS)

- `RequestExecutionLevel user`; `InstallDir "$LOCALAPPDATA\Programs\GT4Recomp"`; `SetShellVarContext
  current`; registry only under `HKCU\Software\GT4Recomp` and
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\GT4Recomp` (DisplayName, DisplayIcon,
  DisplayVersion, Publisher, EstimatedSize, UninstallString, QuietUninstallString, NoModify/NoRepair).
- Pages (MUI2, custom bitmaps from `branding/`): Welcome (cover art + "you need your own GT4 USA v2.00
  disc image") → Notice/License (unofficial project, third-party licenses) → Folder (default shown,
  changeable) → Install → Finish ("Start GT4Recomp" checked; "Create desktop shortcut" checkbox).
  The beautiful onboarding happens in the launcher; the installer stays short and clean.
- Shortcuts: Start Menu `GT4Recomp.lnk` (launcher icon, AUMID set via `WinShell` plugin), optional
  desktop shortcut. No auto-start, no file associations.
- Silent mode (`/S`) for updates; upgrade in place detects a running launcher/game and asks to close.
- Uninstaller: removes the program folder and shortcuts; a custom page asks
  "Also delete my saves, settings and prepared files?" (**unchecked by default**); when checked, a
  second line lists the memory-card path. Never deletes the user's ISO.
- Packaging script (`installer/build_release.py`): copies the pinned release files from a qualified
  slice build, writes `release-manifest.json` (path, size, SHA-256), bundles licenses and the LGPL
  source package link, runs `makensis`, hashes the installer.
- **Code signing / SmartScreen:** an unsigned installer gets the "Windows protected your PC" warning
  until it builds reputation per file hash. Options: (1) unsigned v1 with a README note; (2) OV
  certificate (~USD 200-400/year, hardware token/cloud HSM; reputation still builds gradually; EV no
  longer bypasses SmartScreen — High confidence); (3) Azure Trusted Signing (low monthly cost;
  eligibility for individuals outside some countries **Unknown**). Free OSS signing services
  (e.g. SignPath Foundation) would very likely refuse a binary that contains game-derived code
  (**High confidence**). Unsigned freshly built binaries also raise AV false-positive risk.

## 10. Implementation milestones (TASK-019 branch `task/019-launcher`)

| # | Milestone | Acceptance checks | Effort (agent-days) |
|---|---|---|---|
| M0 | Decision record + TASKS/README release wording; branding folders with neutral placeholders | Decision file merged; neutral package builds | 0.5 |
| M1 | Native release prerequisites: exe-relative shader dir (release config), `.rc` icon + VERSIONINFO, window `hIcon`, `GT4_FULLSCREEN`, `GT4_RACE_DISPLAY` (default 480p; 480i/1080i holds), minidump filter, decide `/MD`+app-local CRT vs `/MT` | CTest + Python suite green; new fixture: exe started from a copied folder finds shaders; headless 4,200-frame smoke identical to slice 166 (480p hold only changes race display index); owner check of icon in taskbar/Task Manager | 2-3 |
| M2 | paraLLEl-GS as `gt4_parallel_gs.dll` with C ABI (after TASK-012 step 2 lands) | Fixture suite passes through the DLL; frames identical to the static build; F8 shows no GS cost change beyond noise; relink test: rebuild DLL from the source package, drop in, game runs | 2-3 |
| M3 | `gt4l-disc`: ISO9660 + SYSTEM.CNF + hash + CORE/ELF rebuild + VOL photo reader | Synthetic ISO fixtures (good, wrong serial, PAL-like, Spec II-like, truncated, wrong VER, bin/cue); private parity tests: CORE/ELF bytes equal the pinned files; Spec II ISO rejected with the Spec II message; 64 photos extracted with hashes recorded | 3 |
| M4 | `gt4l-core` + `gt4l-payload` (Bundled): paths, settings table + TOML, card manager (new/import/backup/lock), launch/supervise, exit classification, crash report zip, single instance | Unit tests for every validation rule mirrored from `play_performance.py`; dry-run launch prints the exact argv/env; scrubbed env test; crash classification tests from recorded logs; second instance focuses the first | 3 |
| M5 | Slint UI with the tokens and components of section 12: setup list, home, settings (Graphics/Display/Advanced/About), running state, crash dialog, licenses; keyboard/gamepad navigation (XInput: D-pad + A to PLAY) | Screens match the mockups at 1280x720 and scale at 125/150%; reduced-motion honored; whole flow usable by keyboard and controller; owner design review | 6-8 |
| M6 | Branding pipeline: `brand.toml`, icon generation script, official assets dropped in, neutral swap test | Neutral package contains no official asset hash; official build shows art/icons everywhere listed in 8.2 | 1-2 |
| M7 | NSIS installer/uninstaller + release packaging + license bundle | Install/uninstall as a standard (non-admin) user in Windows Sandbox; non-ASCII user name; OneDrive-redirected Desktop; upgrade over previous version keeps data; uninstall keeps saves by default | 2-3 |
| M8 | End-to-end QA + owner test | Clean machine: install → select ISO → play → save → exit → relaunch skips setup → crash path (forced) produces a report; owner plays the reference scene from the launcher | 2 |
| M9 | Release candidate: CHANGELOG, known issues, release hosting per decision | Owner approval → tag | 0.5 |
| M10 (insurance, optional) | Option B spike: freestanding game-code DLL + bundled clang-cl/lld-link build on a test VM | Game DLL built on a clean VM from the ISO; frames identical; F8 shows the DLL boundary cost; first-run time measured on 4 and 16 threads | 5-8 |

Total for A: about **22-28 agent-days** (M0-M9), plus owner review/testing time. M2 depends on
TASK-012 progress; M1-M5 can start in parallel with it (single-writer rule: one branch writer at a time).

## 11. Open questions for the owner

1. **Project license** (there is no LICENSE file today). Recommendation: MIT for our own code
   (matches the donor and keeps option A legally simple), LGPL parts in the DLL. Alternative GPL-3.0
   blocks combining with game code in a distributed binary (see 4.2).
2. **Where to host the binaries**: separate releases repository/host (recommended, protects the
   source repo) or the main repo's Releases page?
3. **Official artwork files**: will you provide the NTSC-U cover scan and a clean GT4 logo/emblem
   master (section 8.2), or should we trace them from a scan you approve?
4. **Code signing**: ship v1 unsigned (SmartScreen warning) or budget for a certificate?
5. **Name in Windows**: Start Menu/Task Manager show "Gran Turismo 4" (official look) or
   "GT4Recomp" (lower trademark exposure)?

## 12. Visual design system (round 2: clean, minimalist, Gran Turismo identity)

The mockups sit next to this file. All three are 1280x720, use inline CSS and quiet original placeholder art:
- `mockup-setup.html`: checking / wrong disc / done
- `mockup-home.html`: ready / running
- `mockup-settings.html`: Graphics / Display / Advanced

The "Official asset slots" toggle outlines every official or disc-derived asset in magenta, with its file and source. Rendered screenshots are in `../shots/*.png`.

### 12.1 What the Gran Turismo identity is (research)

- **Official web brand (Confirmed: I read the computed styles on gran-turismo.com on 2026-10-15):**
  - Fonts: `"Helvetica Neue", HelveticaNeue-Medium, HelveticaNeue-Light, Roboto` and `"Roboto Condensed"` (loaded from Google Fonts), plus one heading face, `bebas-neue-pro`.
  - Backgrounds: `#000000`, `#13161B`, `#0A0C14`, and translucent `rgba(67,67,67,.85)`.
  - Text: `#FFFFFF`, `#DEDEDE`.
  - Light surfaces: `#E7E7E7`, `#F8F8F8`, with dark text `#171717` on them.
  - The GT7 online manual uses the same font stack.
- **GT4 in-game (Confirmed string):** the European-language font file on the disc, `GT4.VOL font/europe.kf`, contains the name **`ShinGo-M`** (Morisawa A-OTF Shin Go Medium). The other GT4 font files name `DFHeiBold-B5` (DynaFont, Traditional Chinese) and custom bitmap sets (`FC_*`, `DC_*`, `GT4meter`). So GT4's Latin UI text is set in Shin Go Medium (**High confidence**).
- **GT7 / GT Sport in-game UI typeface: Unknown.** No public source names it. The logo is custom lettering, not a font.
- **Look**, from GT4, GT Sport and GT7 menu screenshots:
  - full-bleed car photography;
  - quiet panels: near-black or charcoal in GT7 options and GT Sport, pale silver or paper in GT4 menus and the GT7 Café;
  - thin hairline separators;
  - small uppercase labels with wide letter spacing, and large light-weight titles;
  - flat line icons and one restrained accent colour;
  - almost no glow or decoration.

  We borrow this identity, not the layout of any particular GT4 screen.

### 12.2 Tokens

| Token | Value | Use |
|---|---|---|
| `ink` | `#0A0C14` | Window background, nav column (from gran-turismo.com) |
| `carbon` | `#13161B` | Page background (from gran-turismo.com) |
| `graphite` | `#1E2229` | Controls, inactive toggles |
| `hair` | `rgba(255,255,255,.10)` | Row separators, outlines |
| `white` / `silver` | `#FFFFFF` / `#DEDEDE` | Primary / secondary text (from gran-turismo.com) |
| `dim` / `faint` | `#9AA0A8` / `#5C626B` | Helper text / disabled |
| `paper` / `paper-ink` | `#F8F8F8` / `#171717` | Primary button, selected segment, toggle "on" (light surface from gran-turismo.com) |
| `accent` | `#2C6BED` | Keyboard and controller focus ring only (**Hypothesis**: a GT-style blue; the owner can pick another) |
| `ok` / `error` | `#2FA866` / `#D64545` | Ticks, "Not supported" |

**Type scale** (size/line height in px):
- Titles: 34/40 Light (setup), 30/36 Light (settings); the neutral-build logo text is 44 Light.
- Body 14/20 Regular; helper text 12/16.
- Labels: Roboto Condensed Medium at 11–13, uppercase, letter spacing 0.12–0.24 em.

**Spacing:** 8 px grid (8, 16, 24, 32, 40, 48, 72, 96). Corner radius 3 px. Rows are 48 px high (56 px in setup).

**Effects:** no shadow except on the cover art, no gradients on controls, no glow anywhere.

### 12.3 Fonts

| Role | Official / reference | Redistributable? | We ship |
|---|---|---|---|
| UI text | Helvetica Neue (gran-turismo.com); Shin Go Medium (GT4 in-game) | No (commercial licenses from Linotype/Monotype and Morisawa) | **Roboto**: the official site's own fallback. SIL Open Font License 1.1 in current Google Fonts releases; older releases were Apache-2.0 (**High confidence**) |
| Labels / numbers | Roboto Condensed (gran-turismo.com) | Yes | **Roboto Condensed** (same license as Roboto) |
| Display heading (optional) | Bebas Neue Pro (gran-turismo.com) | No (commercial) | Not used. If ever needed: Bebas Neue (OFL) |
| Japanese/CJK (later: Android, other locales) | Shin Go / DynaFont | No | Noto Sans JP / Noto Sans CJK (OFL) |
| Logo | Custom GT lettering | Official asset | Only as `logo-gt4.png` in `branding/official/` |

The fonts are embedded in the Slint build, so nothing depends on fonts installed on the machine. This matters on Android.

### 12.4 Components (Slint, portable)

| Component | Description |
|---|---|
| `TitleBar` | Icon slot, app name, window buttons |
| `PrimaryButton` | Paper fill, ink text, uppercase condensed label, 52 px high; hover darkens; focus = 2 px accent ring |
| `SecondaryButton` | Hairline outline |
| `IconButton` | 40 px; used for the settings gear |
| `OptionRow` | Label plus an optional one-line helper on the left, exactly one control on the right, hairline below |
| `GroupHeader` | Small uppercase label |
| `Dropdown` | — |
| `Segmented` | Selected segment = paper fill |
| `Toggle` | On = paper track with an ink knob |
| `StepRow` | Number, label, short detail, and a tick, percentage or status |
| `ProgressLine` | 2 px bar |
| `NavList` | Text items; a 2 px marker on the left shows the current page |
| `ArtBackground` | Cross-fading image with a dark shade along the bottom |

Components make no platform calls. The Windows shell (title bar, file dialog, process launch) stays outside them, so Android can reuse the same `.slint` files with a different shell.

### 12.5 Screens and motion

- **Setup:** cover art on the left. On the right: logo, "Set up your game", "This only happens once.", four step rows and one primary button. For a wrong disc: one red status, a two-line explanation and "Choose another file".
- **Home:** full-bleed disc photo, logo at bottom-left, Play under it, a Settings gear at top-right. While the game runs, Play becomes "Running · Stop". Nothing else: no cards, no news, no hints, no footer.
- **Settings:** a nav column (Back, logo, Graphics, Display, Advanced, About, version). Each page has a title, the single line "Changes apply the next time you play." and its option rows. About holds licenses, disc and game files, repair and log access.
- **Motion:**
  - background cross-fade of 1 s every 15 s;
  - button hover 150 ms, toggle 150 ms;
  - page-change fade 200 ms;
  - the progress bar's width eases.

  There is no Ken Burns, sheen, breathing effect or staggered entrance. Reduced-motion turns all of it off.
- **Input:** keyboard and XInput. D-pad moves focus, A activates, B goes back, Start = Play.
