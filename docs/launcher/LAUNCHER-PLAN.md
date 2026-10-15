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
   folder for `branding/neutral/` (section 5).

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
receipt. Later runs open straight to the home screen: full-bleed art, a large **PLAY** button at the
bottom, and a settings page whose normal section exposes paraLLEl-GS upscaling and deinterlacing and
whose **Advanced/Experimental** section hides the A/B and debug switches. 480p in races is the
default (flipped in the game itself). The launcher starts the game executable with validated
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
| 480p is opt-in | `GT4_FORCE_480P` unset = off (`window.cpp:291`) | Flip default to on; keep `GT4_FORCE_480P=0` as a hidden opt-out |
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
| In-game photos `GT4.VOL: photo/000.JPG … 063.JPG` (64 JPEGs, 1280x960, ~10.9 MB) | Optional extraction to `art\` for the home background (Confirmed: plain JFIF inside PS2ZIP entries) | Read by the launcher only |

**PS2 BIOS: not required — Confirmed.** The donor runtime has no BIOS loader: `rom0:` modules are
HLE (`iop/cdvd.cpp:169`), `ROMVER` is synthesized (`iop/io.cpp:57`), the launcher passes no BIOS
argument and no BIOS file is pinned. The setup screen should say so ("No PS2 BIOS needed").

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
   NVMe a few seconds, SATA SSD ~10-20 s, HDD ~45-90 s (**Hypothesis**, measure). Shown as a
   tachometer-style ring with MB/s. Mismatch with correct serial/version → "This dump differs from
   the known good v2.00 disc (damaged or patched). Please re-dump your disc."
3. **Extract + rebuild** CORE and ELF, verify both pins. Any mismatch at this point is an internal
   error (the disc hash already matched) and is reported as a bug.
4. **Memory card** (section 3.3). 5. **Art** (optional, skippable). 6. **Receipt**
   `prepared\prepare.json` (launcher version, payload kind, disc path/size/file-ID/mtime/hash, CORE and
   ELF hashes, timestamps).

Never copy the ISO by default; offer "Move/copy my ISO into GT4Recomp's folder" only as an option.
If the ISO later moves, the home screen shows "Disc image not found — Locate…" instead of PLAY.

### 3.3 Memory card (Confirmed from donor `sio2.cpp:173-179, 322-333`)

A missing card file is presented to the game as a **new, unformatted** 8,650,752-byte card and is
written on save; GT4 then offers to format it. So the launcher does not need any seed card (the pinned
seeds stay private evidence). Layout under `%LOCALAPPDATA%\GT4Recomp\memcards\`:
`Mcd001.ps2` (slot 1), `backups\Mcd001-YYYYMMDD-HHMMSS.ps2` (copy before each session, keep the last
10). **Import**: accept an existing PCSX2 8 MB `.ps2` card — same size and raw page+ECC layout
(**High confidence**; validate size and the `Sony PS2 Memory Card Format` superblock magic before
import). Hold the card lock (same byte-lock design as `session.lock`) while the game runs.

**Hypothesis to check during TASK-019:** the owner's game boots in 16:9 because the seed card has the
game's wide option set; a fresh card may show a 4:3 image in the fixed 16:9 window until the player
enables 16:9 in GT4's options. Either show a one-time tip or add a hold similar to the 480p hold.

### 3.4 Progress UI, errors, repair

- Wizard with four steps (Disc → Verify → Prepare → Ready), each sub-task a row with state icon,
  duration and a short explanation; Cancel is always available and leaves no partial output (write to
  `prepared.tmp\`, atomic rename at the end).
- Errors are written for players ("what happened / what to do"), with a "Details" expander and
  "Copy details" button containing the technical line.
- **Repair** (Settings → Saves & files): re-verify installed binaries against the release manifest
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

### 6.1 Today and from TASK-012 (Confirmed from code unless marked)

| Setting (UI) | Variable / argument | Values | Section |
|---|---|---|---|
| Renderer | `GT4_RENDERER` | `parallel` (default once TASK-012 qualifies), `donor` (legacy fallback) | Graphics |
| Upscaling (super-sampling) | `GT4_PGS_SSAA` (planned name; backend maps 1/2/4/8/16 in `parallel_gs_backend.cpp:46-54`) | 1, 2, 4, 8, 16 | Graphics |
| High-resolution scanout | `GT4_PGS_HIGHRES` (planned) | 0/1, only with ≥4x | Graphics |
| Deinterlacing (menus are 480i) | `GT4_PGS_PROGRESSIVE`, `GT4_PGS_ANTIBLUR` (planned); paraLLEl-GS `force_progressive`, `anti_blur`, `skip_deinterlace` | UI: Auto (progressive + anti-blur) / Sharp (progressive, no anti-blur) / Original (interlaced fields) | Graphics |
| 480p in races | `GT4_FORCE_480P` | default **on** in the exe; launcher never sets it unless the hidden opt-out is used | Advanced |
| Present mode | `GT4_PRESENT_MODE` | `fifo` (VSync, default) / `mailbox` / `immediate`; launcher greys out modes the surface lacks (query before showing) | Display |
| Start in fullscreen | `GT4_FULLSCREEN` (new) | 0/1 | Display |
| Frame pacing clock | `GT4_PACING` | `paced` (default) / `audio` | Experimental |
| Synchronous GS | `GT4_GS_SYNC` | 0/1 | Experimental |
| GS hacks | `GT4_PGS_HACKS` (planned) | comma list: `disable_mipmaps`, `backbuffer_promotion`, `allow_blend_demote` (not `unsynced_readbacks`: diagnostic only) | Experimental |
| VU1 verify / VU0 verify | `GT4_VU1_VERIFY`, `GT4_VU0_VERIFY` | sample period N | Experimental → Diagnostics |
| VU1 interpreter only | `PS2_VU_INTERPRET=1` | flag | Experimental → Diagnostics |
| Performance overlay / F7-F8 | `GT4_PERF=1` | flag | Experimental → Diagnostics |
| Donor scale | `PS2_SCALE` | keep 1 (donor renderer only) | not exposed |
| Future (TASK-012 step 5) | post-processing (sharpen/FXAA), dithering, texture options | — | Graphics when qualified |
| Controls | keyboard map fixed (arrows/Z/A/S/X/Enter), XInput | read-only reference page now; remapping and wheels (TASK-018) later | Controls |
| CPU build variant | x86-64 / x86-64-v3 folder | auto by CPUID; only baseline shipped until v3 shows a benefit (TASK-010: Unknown) | Advanced |

### 6.2 How the launcher passes settings

- **v1: environment variables on the child process only** (no system/user environment change). The
  exe already parses and refuses unknown values for most of them, so this needs no new game code and
  matches the A/B rule (one switch = one variable). The launcher starts from a scrubbed environment
  (drops every inherited `GT4_*`/`PS2_*`, as `external_runtime_probe` does), then applies its own.
- One Rust table is the single source of truth: setting id → variable → allowed values → default →
  section (Normal/Advanced/Experimental/Diagnostics) → "requires" rules (e.g. high-res scanout needs
  ≥4x; verify modes are mutually exclusive with interpreter-only, as in `play_performance.py`).
  Validation runs on load and before launch; invalid values reset to default with a notice.
- Persisted in `%LOCALAPPDATA%\GT4Recomp\config\settings.toml` (`schema_version`, unknown keys kept
  but ignored, migration function per version).
- Each launch writes `launch.json` into the session folder: arguments, the exact variables set,
  versions and hashes — the release equivalent of `result.json`, and part of crash reports.
- **Later (optional):** the exe reads the same TOML via `--config` so in-game settings menus (idea list)
  can share it. Not needed for TASK-019.

Experimental and Diagnostics sit behind a collapsed "Advanced" panel with an amber "may reduce
stability or speed" banner; values that differ from defaults show a small dot on the Settings tab and
a chip on the home screen ("Experimental: GS sync") so A/B states are never forgotten.

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
- While the game runs the launcher shows a slim "Running" state (elapsed time, Stop game) and can
  minimize to the taskbar; option "Close launcher when the game starts" (off by default, because the
  launcher reports crashes).
- Exit classification: exit code 0 and `[ee] stopped … window closed` = normal. Anything else = crash
  dialog: what happened in one sentence (reason from the `[ee] stopped` line, e.g. "no translated code
  at 0x…"), last 40 log lines, buttons **Open session folder**, **Copy report** (zip of `game.log`,
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
accent         = "#2F7BFF"
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
| `cover_front` | Setup left panel (`slot-cover`), About page, installer welcome bitmap (cropped) | **Not on the disc** (Confirmed: the ISO root holds only CORE.GT4, GT4.VOL, IRX, NET, EPSON, IOPRP300.IMG, SCUS_973.28, SYSTEM.CNF). Owner supplies a clean scan/photo of the NTSC-U box front (own copy), color-corrected, ≥1200x1700. |
| `logo_wordmark` | Home top-left (`slot-logo`), Setup header, splash | Owner-supplied vector/PNG of the GT4 logo (trace from the cover scan). In-disc title textures exist as Tex1 `.img` files in `GT4.VOL` (e.g. `advertise/loading.img`, `advertise/us/main.img`), but decoding Tex1 is new work and which file holds the logo is **Unknown** — not planned for v1. |
| `emblem` → `launcher.ico`, `game.ico` | Taskbar, Task Manager, Start Menu, desktop shortcut, window title bar, Alt+Tab | Derived from the official GT emblem/logo style (owner-supplied master). Generated by a script (`scripts/make_icons.py` with Pillow) into multi-size ICO: 16, 20, 24, 32, 40, 48, 64, 96, 128, 256 (256 PNG-compressed). Launcher and game get **distinct** variants (e.g. game = emblem on dark tile, launcher = emblem with a small chevron badge) so the two taskbar buttons are distinguishable. |
| Home background (`slot-hero`) | Home, Setup "Ready" step | **From the user's own disc at setup**: `GT4.VOL photo/000.JPG…063.JPG` (64 in-game photos, 1280x960 JPEG, Confirmed by extraction in this study), resized/cropped to 16:9 and cached in `%LOCALAPPDATA%\GT4Recomp\art\`; never shipped. Before setup completes, `hero_fallback` (official screenshots or neutral art) is used. |
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
| M1 | Native release prerequisites: exe-relative shader dir (release config), `.rc` icon + VERSIONINFO, window `hIcon`, `GT4_FULLSCREEN`, 480p default on (opt-out `0`), minidump filter, decide `/MD`+app-local CRT vs `/MT` | CTest + Python suite green; new fixture: exe started from a copied folder finds shaders; headless 4,200-frame smoke identical to slice 166 (480p hold only changes race display index); owner check of icon in taskbar/Task Manager | 2-3 |
| M2 | paraLLEl-GS as `gt4_parallel_gs.dll` with C ABI (after TASK-012 step 2 lands) | Fixture suite passes through the DLL; frames identical to the static build; F8 shows no GS cost change beyond noise; relink test: rebuild DLL from the source package, drop in, game runs | 2-3 |
| M3 | `gt4l-disc`: ISO9660 + SYSTEM.CNF + hash + CORE/ELF rebuild + VOL photo reader | Synthetic ISO fixtures (good, wrong serial, PAL-like, Spec II-like, truncated, wrong VER, bin/cue); private parity tests: CORE/ELF bytes equal the pinned files; Spec II ISO rejected with the Spec II message; 64 photos extracted with hashes recorded | 3 |
| M4 | `gt4l-core` + `gt4l-payload` (Bundled): paths, settings table + TOML, card manager (new/import/backup/lock), launch/supervise, exit classification, crash report zip, single instance | Unit tests for every validation rule mirrored from `play_performance.py`; dry-run launch prints the exact argv/env; scrubbed env test; crash classification tests from recorded logs; second instance focuses the first | 3 |
| M5 | Slint UI: theme, setup wizard, home, settings (normal + advanced), running state, crash dialog, about/licenses; motion; keyboard/gamepad navigation (XInput: D-pad + A to PLAY) | Screens match the mockups at 1280x720 and scale at 125/150%; reduced-motion honored; whole flow usable by keyboard and controller; owner design review | 6-8 |
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

## 12. Visual design (mockups next to this file)

`mockup-setup.html` (states: verifying / rejected Spec II / ready), `mockup-home.html` (ready /
game running), `mockup-settings.html` (Graphics / Advanced & experimental), all 1280x720, inline
CSS, original placeholder art. Toggle "Show official asset slots" outlines every official or
disc-derived asset in magenta with its file name and source.

- **Palette:** asphalt black `#07090C`, graphite panels `#121820`/`#17202B`, chrome text `#EEF2F7`,
  muted `#8792A2`, one accent "GT blue" `#2F7BFF` (glow `#79ACFF`), amber `#FFB020` only for
  experimental/warnings, green `#35D07F` for verified/running, red `#FF4545` for rejections.
  The accent comes from `brand.toml`, so the neutral brand can recolor without code changes.
- **Type:** condensed DIN-style headings (Bahnschrift on Windows; the Slint build embeds an
  OFL condensed face such as Barlow Condensed for portability) with wide tracking for labels,
  tabular numerals for counters; Segoe UI for body text.
- **Layout:** Home = full-bleed art, dark left/bottom shade, title + 360x78 PLAY bottom-left,
  frosted status cards right, disc-photo strip bottom-right. Setup = cover art on the left (3D
  tilt), four-step wizard right. Settings = sidebar, rows with one-line explanations, live
  preview/"what the game receives" panel right, sticky Save bar.
- **Motion** (all off under reduced motion): Ken Burns + 12 s cross-fade of disc photos; PLAY
  breathing glow + periodic sheen; staggered card rise (80 ms); tachometer progress ring for the
  disc hash; spinning disc while working; 1x/4x comparison wipe; 200 ms toggle springs.
- **Input:** full keyboard and XInput navigation (D-pad focus ring in accent, A = activate,
  B = back, Start = Play).
