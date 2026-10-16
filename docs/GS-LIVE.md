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

## 6. TASK-037/038: readbacks, cache, window and crash handling (slice194)

Status: **built and checked headless; owner windowed test pending** (2026-10-16). Branch `task/037-038-gs-window`
(main) and `gt4gs/window` in the fork (commit `10851476b`, tag `gt4gs-v0.3.0`). `--mode pcsx2` now selects
[`slice194-pcsx2.json`](inputs/slice194-pcsx2.json); `--mode pcsx2-slice193` keeps the TASK-036 build.

### 6.1 Night race late frames (owner run 2026-10-16, 3x): cause

Owner F8 (7,467 VBlanks, 59.89/s, 213 late frames): `gs_ipc_readback_wait` 0.934 ms/frame with exactly 1 readback
per frame (7,869 readbacks, 8,442 ms in total = 1.07 ms each), `gs_ipc_write` 0.59 ms/frame, 4.1 MB/frame, MTVU
`gs_privileged` EE wait 3.38 ms/frame, measured EE CPU 8.6 ms/frame. Frame-time buckets: 20 frames of 25 ms or more,
88 frames of 17-25 ms, the rest of the late count is the catch-up after them. The Nordschleife run (6 late frames)
had a **larger** stream (4.8 MB/frame, `gs_ipc_write` 0.70 ms) but only 0.2 readbacks per frame and an MTVU
`gs_privileged` wait of 0.67 ms.

- **Readbacks remove the GS buffering** (High confidence). Without readbacks the game runs up to 2 frames ahead of
  the GS process, so a GS-thread spike is absorbed. A READ_FIFO waits until the GS process has executed every earlier
  record and the GPU has finished, so each GS-thread spike lands in the game frame of that readback. In the night race
  there is one per frame.
- **The spikes are shader and pipeline compiles** (Confirmed for the mechanism, headless): the hidden-window check
  with one 2 KiB readback per frame (like the night race) paced at 60 Hz, 3x, gives a mean wait of 1.07 ms (the owner's
  value) with a cold cache and 10 readbacks of 16 ms or more (max 144 ms); with the cache warm the mean is 0.69 ms and
  2 readbacks reach 16-17 ms. The owner's PCSX2 GS-thread time rose to 5.7-7.2 ms/frame at race start
  (vsync 2,500-3,500) and the session had 5 frames-in-flight waits of 26 ms each.
- **The shader cache was not where we put it** (Confirmed): `gt4-gs.exe` 0.2.0 ignored `[Folders] Cache` and used the
  PCSX2 user folder `Documents\PCSX2\cache` (the owner's log: "Read 274 entries from ...Documents\PCSX2\cache"). It
  persisted there, so the hitches were compiles of pipelines not yet seen.
- **Headroom**: the MTVU privileged-register sync (3.4 ms/frame, the EE waits for the VU1 worker when it writes CSR
  after a FINISH) is the largest EE wait in the night race; it is an MTVU property (TASK-011), not a GS process cost
  (High confidence). With it, the EE frame is about 13 ms of 16.7 ms, so a 4-5 ms readback spike makes a frame late.
- **Not the cause**: stream size and IPC write (smaller than at Nordschleife); `GS records started inside another
  path's packet` is 0 in every run.
- **Unknown**: whether FIFO presentation blocked the GS thread in the owner run (the early swap chain acquire after a
  present blocks until the display refresh; with hidden windows it never blocks). slice194 defers that acquire and
  reports the acquire time, so the next owner F8 shows it.

### 6.2 Changes

| Area | Change |
|---|---|
| Readback mode | `GT4_GS_READBACK=accurate` (default, PCSX2 `HWDownloadMode` 0 Enabled) or `unsynchronized` (3): `play_performance.py --gs-readback ...`; written to `gt4-gs.ini`, recorded in `result.json` (`gs_readback_mode`). Unsynchronized answers on the GS process CPU thread from the GS local memory copy, without waiting for the GS thread or the GPU; `gt4-gs.exe` captures BITBLTBUF/TRXPOS/TRXREG with a GIF tag scanner for it. |
| Readback timing | Game F8: `[gs-ipc] interval readback waits: count, mean, max, buckets(ms)` and a session line. `gt4-gs.exe`: readback service times every 500 vsyncs and at exit, swap chain acquire time, GS thread frame times. |
| Folders | `[GT4GS] DataRoot` and `[Folders]` applied before the GS opens: dev runs use `private/slice193-gt4gs-cache` and `private/slice193-gt4gs-data` (of the worktree the launcher runs from; the worktree cache was seeded once from the owner's `Documents\PCSX2\cache`, 365 entries). Default without a config: `%LOCALAPPDATA%\GT4Recomp\gt4-gs`. PCSX2's pipeline cache (`vulkan_pipelines.bin`) is on (written at exit). SPU2 now uses the Null backend (0.2.0 opened a WASAPI stream with an MMCSS thread). |
| Presentation | Deferred swap chain acquire in live mode (`GT4_GS_EAGER_ACQUIRE=1` is the A/B switch). |
| Spin | `[GT4GS] SpinUs` (transport spin before sleeping); default unchanged (30 us), not needed by the measurements. |
| Window | Input queues detached after `SetParent` (log: `input queues detached`); a click on the picture brings the game window forward; zero-size resizes (minimized window) ignored on both sides; in pcsx2 mode `WM_CLOSE` halts the game and the window is destroyed after the GS process was shut down (DestroyWindow of the parent waited 994 ms for a stalled GS window thread). |
| Crash | GS process death: the guest runs 180 more VSYNCs (records dropped, readbacks zeros) so a changed memory card is written by the runtime (50 quiet VSYNCs), then halts; the message box asks "Restart with the built-in renderer (CoreGS 1.1)?"; on yes the launcher starts a new session of the same build with `GT4_RENDERER=donor`. Handshake failures show the same box in a windowed run; the timeout message names the likely causes. |
| Display | Checked, no change needed: at `AspectRatio = 16:9` the drawn rectangle is the whole 1280x720 window and 960x540 at y 90 in a 960x720 window, the same rectangles as the built-in renderer's `presentation_rect` (TASK-009): no stretch, no crop. "Auto 4:3" (640x480) is only used for the PoC comparison. |

### 6.3 Checks (2026-10-16)

- CTest 29/29 on `private/slice194-gs-window-build/build`; `native_gs_stream_client` adds handshake timeout (refused
  after 1.5 s, silent GS process ended by the job), game killed (the job ends a GS process that ignores the game) and
  the grace VSYNCs before the halt. Python 350 OK, 14 skipped (`tests/python/test_gs_window.py` new).
- Headless slice157 schedule: donor 42/42 identical to slice157; pcsx2 1x (`GT4_GS_ASPECT=auto`) 41/41 PNGs identical
  to the slice193 offline `--replay-gscap` frames; GS process killed: `the game halts after 180 more VSYNCs`, then
  `[ee] halted: GS process exited`.
- `tools/gs_live_check` (hidden parent window, 3x, FIFO, menus capture, paced 60 Hz, one 2 KiB readback per frame):

| Run | Readback wait (game side) | Notes |
|---|---|---|
| accurate, cold cache | mean 1.12 ms, p99 6.8, max 143.8; 10 of 1,096 at 8 ms or more | readback check: sprite 512/512 pixels and upload 2,048/2,048 bytes exact |
| accurate, warm cache | mean 0.69 ms, p99 1.55, max 17.4; 2 at 8 ms or more | same |
| accurate, eager acquire | mean 0.71 ms, p99 1.57, max 16.6 | no difference with hidden windows |
| unsynchronized | mean 0.11 ms, p99 0.26, max 0.49 | sprite 0/512 (zeros), upload 8/2,048: stale data, as documented by PCSX2 |

- Window: `--stall-check` (GS window thread stalled 3 s): 450 WM_NULL answered by the parent, longest 17 ms, longest
  pump 0.2 ms, SetWindowPos 1.4 ms; `--soak 3000` (1,000 x 1920x1080, 960x540 and 0x0): no hang, no ERROR, child keeps
  its size; `--aspect-check` passed; attach/resize/snapshot/shutdown as before.
- Shader cache, offline `--replay-gscap` of the 4,200-frame menus stream at 3x twice with a fresh cache folder: GS
  thread frame times cold max 84.2 ms, 4 frames over 16.7 ms; warm max 6.3 ms, none over 16.7 ms.

### 6.4 Not verified

The visible window (fullscreen toggle, Alt+Tab, minimize/restore, focus after a click, RTSS), whether detaching the
input queues holds with real input, the message boxes, the restart flow, the night race with slice194 (late frames,
lens flare and night lighting with each readback mode), FIFO acquire blocking on a visible window, race scenes
headless (no race capture exists).

## 7. Owner test of slice194 and the click fix (slice195)

Owner night race, 5 AI, 3x (2026-10-16): **accurate** 59.97 FPS, 69 late frames (213 with slice193), readback wait
mean 0.78 ms; **unsynchronized** 60.00 FPS, 1 late frame, but the car headlight flares do not render and some elements
never reach the screen. GPU power and clock were the same. The owner chose **accurate** as the default; unsynchronized
stays an option labelled "faster, may drop effects".

Bug found by the owner: a click inside the picture deactivated the game window (F11, keyboard and controller stopped
until the title bar was clicked). Cause (Confirmed with a visible fixture): with the input queues detached (slice194),
the click makes the gt4-gs window thread the foreground thread; that thread has no active window, so the foreground
became NULL. `HTTRANSPARENT` only forwards to windows of the same thread, so across processes it just discarded the
click. A disabled child (`WS_DISABLED`) was tried first and gives the same NULL foreground. With slice193 (queues
attached) the check passes.

Fix (gt4-gs 0.3.1, fork commit `0099d77e1`, tag `gt4gs-v0.3.1`; slice195): every button-down and `WM_MOUSEACTIVATE`
on the child posts a message to the gt4-gs window thread, which gives the foreground back to the game window (allowed:
that thread received the click); the child returns `MA_NOACTIVATE` and no longer answers `HTTRANSPARENT`; the input
queues stay detached. The game sets the keyboard focus to its own window on every activation in pcsx2 mode
(`WM_ACTIVATE`, `native/window.cpp`).

Check: `gs_live_check --click-check` (VISIBLE, about 1 s: a 336x228 parent at the top-left corner, a SendInput click on
its title bar, then one on the picture; cursor restored): 0.2.0 passes, 0.3.0 fails (foreground NULL), 0.3.1 passes
(foreground and keyboard focus stay on the parent). The stall check still passes with 0.3.1 (longest answer 18 ms).
slice195: CTest 29/29, Python 350 OK (14 skipped), donor 42/42, pcsx2 1x 41/41 identical to slice194.
Not verified: the click with the real game window (owner).

Release flow (2026-10-16, owner decision): gt4-gs releases are built and published from the owner's PC with
`gt4gs/release-local.ps1`; GitHub Actions is a manual fallback. slice195 pins the published `gt4gs-v0.3.1` zip
(downloaded back and checked, `private/slice195-gt4gs-release-pkg`, tree sha256 `243fa487...`, `gt4-gs.exe` sha256
`9650ff3c...`). With it: click check and stall check pass, headless pcsx2 1x 41/41 identical, Python 350 OK.
