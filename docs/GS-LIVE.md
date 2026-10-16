# Live PCSX2 GS process (TASK-036)

Status: **built and checked headless; owner windowed test pending** (2026-10-16). Branch `task/036-gs-live` (main
repo, MIT) and `gt4gs/live` in the GPL fork [moesuito/gt4recomp-gs](https://github.com/moesuito/gt4recomp-gs).
Decision [0070](decisions/0070-pcsx2-gs-separate-process.md), plan [GSDX-PLAN](GSDX-PLAN.md) sections B.3 and C,
protocol [GS-STREAM-PROTOCOL](GS-STREAM-PROTOCOL.md). Labels: **Confirmed**, **High confidence**, **Hypothesis**,
**Unknown**.

Clean-room note: the fork side is PCSX2-tainted work (PCSX2 sources read and changed in the fork only). Nothing here
touches CoreGS 2.0 (TASK-027, `native/hw_gs`, `task/027-*`). The MIT side of this task uses only our protocol; no
PCSX2 code was copied into this repository.

## 1. What runs

```
gt4vulkan.exe (MIT, game)                          gt4-gs.exe (GPL-3.0+, fork gt4gs/live)
  EE/VU/IOP, pacing, audio, input, F7/F8, card       PCSX2 v2.8.2 GS (Vulkan), GameDB fixes for SCUS-97328
  GT4_RENDERER=pcsx2: no donor Vulkan device         GSStreamReplayer: executes the records on the GS
  GIF/PRIV/VSYNC/READ_FIFO/RESET/CONTROL  ---ring--> MTGS / GS thread presents on its own window,
  game window (top level)        <---events--- READY, STATS, ERROR, WINDOW, SNAPSHOT_DONE, EXITING
                                                   which is a WS_CHILD of the game window
```

- The game spawns `GT4_GS_PROCESS` suspended in a kill-on-close Job Object with
  `--ipc <session> --parent <pid> --config <gt4-gs.ini>` and waits up to 20 s for READY (TASK-035 transport).
  New in TASK-036: `GT4_GS_PROCESS_SHA256` (set by the launcher from the manifest pin) makes the game refuse a GS
  process with another SHA-256 (Windows CNG); `GT4_GS_CONFIG` adds `--config`. Confirmed (CTest scenarios).
- Window: the game posts CONTROL attach_window with its HWND after the handshake and CONTROL resize on WM_SIZE
  (the Alt+Enter/F11 fullscreen toggle resizes the window, so it goes the same way). The game sets
  `WS_CLIPCHILDREN` on its window. `gt4-gs.exe` creates a hidden non-activating popup at start (so its Vulkan device
  has a surface), and on attach its window thread makes it a `WS_CHILD` covering the game client area; the child
  answers `WM_NCHITTEST` with `HTTRANSPARENT`, `WM_MOUSEACTIVATE` with `MA_NOACTIVATE`, gives focus back to the
  parent and never blocks. Confirmed in the hidden-window check (section 4.4); not yet seen by the owner on screen.
- Pacing stays in the game: the VSYNC record is posted from the guest vblank right after the pacer released the
  frame (`system.cpp` order: `on_vblank` -> `hw::vblank_start` -> `gs::vblank`), and the game waits while more than
  `GT4_GS_FRAMES_IN_FLIGHT` (default 2) VSYNCs are unprocessed. `gt4-gs.exe` bumps `vsyncs_done` from its GS thread
  after the vsync was processed (presented). The game presents nothing itself.
- GS crash: any wait sees the GS process handle, the game prints `GS renderer stopped (exit code X)`, halts the
  guest through the normal exit path (`[ee] halted: GS process exited`) and, when a window was attached, shows a
  message box after the session summary. Confirmed headless (kill test, no window); the message box is not verified.

## 2. gt4-gs.exe (fork)

Summary; details in the fork's `gt4gs/README.md` and `gt4gs/BUILD.md`.

| Part | What it does |
|---|---|
| `pcsx2/GSStreamReplayer.{h,cpp}` | CPU provider like the GS dump replayer. GIF chunks go to the GIF unit path named in the record (PATH1/2/3) as completed GS packets (MORE parts joined); PRIV into the 8 KiB privileged block (CSR RESET resets block, IMR, CSR and GS; CSR bits 0-4 clear flags; CSR is otherwise not stored; IMR keeps mask bits; PMODE/DISPFB writes mark "registers written"); VSYNC sets CSR FIELD to the previous VSYNC's field and posts the vsync; READ_FIFO via `MTGS::InitAndReadFIFO` into the response area; RESET hard/soft; CONTROL shutdown, attach_window, resize, snapshot (reload_config answered "unsupported"). STATS every 500 vsyncs. Starts with a clean GS hard reset. |
| `pcsx2-gt4gs/` | `gt4-gs.exe` frontend (from `pcsx2-gsrunner`): `--ipc/--parent/--config` live mode, `--replay-gscap` offline mode, GSRunner dump mode. No input, audio or memory cards. GT4 defaults: Vulkan, blending High, deinterlace Automatic, 16:9, OSD messages off in live mode. |
| `3rdparty/gt4recomp-gs-stream/` | our MIT protocol header, transport and `.gscap` reader, copied unchanged from `57d1fa5`; `SHA256SUMS` (LF form) checked at configure; built with exceptions in a separate library behind a no-throw adapter. |
| VMManager, GSDumpReplayer, Console | a prepared stream boots like a dump (no BIOS, no disc), its serial selects the GameDB entry; console text also reaches redirected handles, so `gt4-gs.exe` lines land in the game's session log. |

Field timing: PCSX2's EE posts the vsync and swaps CSR FIELD about 3.5 scanlines later, so its GS sees the field of
the frame that just ended. Our VSYNC record carries the field after the toggle, so the consumer applies the
previous record's field. High confidence from the code; the menus do not depend on it (TASK-034 control), and
interlaced races are not checked yet.

Start without a freeze state: the dump replayer with an empty state reports `Failed to load GS state` and keeps the
state the GS had at open time. The stream always starts at the game's power-on, so `gt4-gs.exe` does an explicit GS
hard reset instead (no error). Confirmed for the boot-to-menus schedule.

## 3. Launcher (`--mode pcsx2`, slice193)

- Manifest [`docs/inputs/slice193-pcsx2.json`](inputs/slice193-pcsx2.json): runtime
  `private/slice193-gs-live-build/build/gt4vulkan.exe` (same runtime capsule, port and shaders as slice191), and a
  `gs_stream` block naming `private/slice193-gt4gs-pkg/gt4-gs.exe` (pinned as a runtime dependency) and the package
  tree digest (`tree_sha256` over "path, size, sha256" of all 140 files). The package is the **local build** of fork
  commit `acf4f986e` (zip `gt4-gs-0.2.0-live-windows-x64.zip`, sha256 `b32d7f8d...`), recorded in the manifest's
  `gs_package.source`. The CI prerelease `gt4gs-v0.2.0-live` is built from the same commit with another compiler and
  LTO, so its binary is not the pinned one. CI run 38908248101 (27 min, dependencies rebuilt because the script
  changed) published https://github.com/moesuito/gt4recomp-gs/releases/tag/gt4gs-v0.2.0-live: `gt4-gs-0.2.0-live-windows-x64.zip`
  18,227,848 bytes, sha256 `369192e9f3bab26b41fbe428c144f8d9b4d811547e55557201654c5c44362ba9` (includes the FFmpeg DLLs), source zip sha256
  `47b4dc1485a9a4babfdaf8c7874a51fca1d6a5b56fd215da0fee254a03bb9127`.
- Each pcsx2 run writes `gt4-gs.ini` into the session folder (minimal settings; TASK-039 owns the full table):
  `Renderer = 14` (Vulkan), `upscale_multiplier` from `GT4_GS_SCALE` / `--gs-scale` (1..8, default 3),
  `accurate_blending_unit = 3` (High), `deinterlace_mode = 0` (Automatic), `AspectRatio` from `GT4_GS_ASPECT`
  (`16:9` default, `4:3`, `auto` = PCSX2 "Auto 4:3/3:2"), `VsyncEnable` from `GT4_PRESENT_MODE` (fifo/unset: on;
  mailbox: on with mailbox allowed; immediate: off), `[Folders] Cache = private/slice193-gt4gs-cache` (shader cache
  kept between sessions). Headless runs add `[GT4GS] Surfaceless = true` and PNG dumps every 100 vsyncs into
  `<session>/gs-frames/live_frameNNNNN.png`.
- Owner command (PowerShell, from the worktree):
  `C:\Antigravity\gt4-staticrecomp\private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode pcsx2 --gs-scale 1`
  (or `--gs-scale 3`). F7 starts an interval, F8 writes the report with the `[gs-ipc]` section; the `gt4-gs.exe`
  console lines (GS thread ms, every 500 vsyncs) are in the same `probe.log`.

## 4. Checks (2026-10-16, idle machine)

### 4.1 Automated

- CTest 29/29 on `private/slice193-gs-live-build/build` (`native_gs_stream_client` adds: correct pin accepted with a
  `--config` file, pin mismatch refused, missing config refused). Python 335 tests OK, 13 skipped
  (`GT4_NATIVE_BUILD` = slice193 build; new `tests/python/test_gs_live.py`: ini generation, package digest refusal,
  owner-run settings, `--gs-scale`, pins).
- Fork: configure-time hash check of the vendored files; local build of `pcsx2-gt4gs` (751 steps cold).

### 4.2 Headless, slice157 schedule (4,200 frames, 12 presses)

| Run | Result |
|---|---|
| `GT4_RENDERER` unset (donor) | 42/42 frames identical to `private/slice157-smoke/headless-157/frames`. Confirmed. |
| pcsx2, `GT4_GS_SCALE=1`, `GT4_GS_ASPECT=auto`, surfaceless | Completed 4,200 frames, GS process exit 0 after shutdown; 1,637,885 records, 2,599,116,784 bytes (same as slice191); in-process capture of frames 0-1100 is a byte prefix of the slice191 donor capture. **41/41 live PNGs (every 100 vsyncs) byte-identical to the offline `--replay-gscap` PNGs of the slice191 recording; frames 300 and 700 byte-identical to the TASK-034 PoC images.** Confirmed. |
| pcsx2, `GT4_GS_SCALE=3`, 16:9, surfaceless | Completed, exit 0. |
| pcsx2, GS process killed after about 570 frames | `GS renderer stopped (exit code 4294967295) ... while waiting for frames in flight`, `[ee] halted: GS process exited`; no hang. Confirmed. |

### 4.3 Offline replay (`--replay-gscap`)

`gt4-gs.exe -dumpdir <dir> --replay-gscap private/slice191-smoke/streams-final/pcsx2-recorded.gscap` (1x): 4,200
vsyncs in 57 s with PNG dumps of every frame, exit 0. With `AspectRatio = Auto 4:3/3:2` the PNGs of frames 60, 300,
700 and 1100 are byte-identical to the TASK-034 PoC (PCSX2 nightly `685637d`), frame 900 differs by at most 2 per
channel (0.00% of pixels above 8). Confirmed.

**The PoC's 480-versus-448 "vertical scale" is the aspect-ratio correction** (PoC condition (a)): the default "Auto
4:3" output is 640x480, 16:9 is 796x448, and `ScreenshotSize = 2` (internal resolution, uncorrected) gives
640x448. Against the donor frames at 640x448: frame 300 0.00% (exact), 700 0.00% (57.4 dB), 100 1.33%, 900
17.5%, 1100-4000 about 7.5% (27.8 dB). Confirmed as measured; the 900-4000 differences are the menu content the
owner judged in TASK-034, not a scale problem.

### 4.4 Hidden-window check (`tools/gs_live_check`)

A producer with a never-shown parent window replays 1,100 VSYNCs of a capture into `gt4-gs.exe` (3x, vsync off),
pumping its messages once per VSYNC like the game: READY, attach (`window attached ... ok`), resize to 1280x720 midway,
CONTROL snapshot (640x360 at 16:9 inside 640x480), child window found with style `WS_CHILD | WS_VISIBLE |
WS_CLIPSIBLINGS` and client 1280x720, shutdown exit code 0. With `--kill-at 300` the next frames-in-flight wait
ends with PeerExited (exit code 77). Confirmed.

### 4.5 Costs (headless, menus, unthrottled; not an owner measurement)

| Cost | 1x | 3x |
|---|---|---|
| Game `gs_ipc_write` (EE ms/frame, F8 category) | 0.11-0.21 | similar |
| Game `gs_ipc_vsync_post` | 0.006-0.016 | similar |
| Game `gs_ipc_backpressure` / readback wait | 0.000 (0.022 in the boot interval) / 0 (no readbacks) | 0.000 / 0 |
| Frames-in-flight waits over 4,200 frames | 3 waits, 20.8 ms total | 17 waits, 64.2 ms total |
| `gt4-gs.exe` CPU thread (record processing) | 0.07-0.10 ms/frame | 0.07-0.10 ms/frame |
| `gt4-gs.exe` GS thread (PCSX2 metric) | 0.64-1.00 ms/frame, about 11% | 0.70-0.94 ms/frame, about 12% |
| GPU time | Unknown: PCSX2 reports 0.000 ms surfaceless (also in the PoC) | Unknown |
| Stream | 619 KB and 390 records per frame on average (menus) | same |

The GS-thread figures are menus only, headless and faster than 60 FPS; race scenes and the paced 60 Hz case are
the owner test and TASK-041.

## 5. Not verified

- The windowed game: the image in the game window, fullscreen toggle, focus and alt-tab, RTSS, the crash message box
  (owner test).
- Races, interlaced 480i scenes and the field bit; night race readbacks (TASK-037); GPU time.
- First-run shader compile stutter with the persistent cache (`private/slice193-gt4gs-cache`).
- Behaviour when the game window thread stalls while `gt4-gs.exe` attaches (TASK-038 hardening).
- The CI prerelease binary was not run locally (only its build and publication are checked).
