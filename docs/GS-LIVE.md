# Live PCSX2 GS process (TASK-036)

Status: **built and checked headless; owner windowed test pending** (2026-10-16). Performance of this path: section 9
(TASK-041, slice198; on gt4-gs 0.4.0 from slice199, section 10.5). JIT-free package: section 10. Branch `task/036-gs-live` (main
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

Field timing (superseded by TASK-047, section 8): until gt4-gs 0.3.1 the consumer applied the previous VSYNC
record's field, reasoning that PCSX2's EE swaps CSR FIELD about 3.5 scanlines after posting the vsync. That reasoning
missed that the donor flips the field before the game's VBLANK_S handler reads it, and the menus seemed not to depend
on it only because the stream had no SYNCV, so PCSX2 never deinterlaced. Since slice196 / gt4-gs 0.3.2 the consumer
presents each frame with its own VSYNC record's field (protocol 1.1).

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

## 8. 480i deinterlacing (TASK-047, slice196, gt4-gs 0.3.2)

Owner report (2026-10-16): the 480i menus look poor in the PCSX2 GS mode (soft, flickering); races (480p held) are
fine; in real PCSX2 with deinterlace Automatic the menus are perfect. `gt4-gs.ini` already had Automatic.

### 8.1 How PCSX2 v2.8.2 decides (fork sources, PCSX2-tainted reading; no code copied here)

- The GS treats the output as really interlaced only when SMODE1 CMOD is non-zero **and SYNCV VFP bit 0 is set**;
  otherwise it never deinterlaces and never applies the field offset. Confirmed (GS state code).
- With Automatic, field mode (SMODE2 INT=1, FFMD=1: each field is its own half-height picture) selects the
  motion-adaptive weave; frame mode without per-field tricks selects no deinterlacing. Confirmed.
- The field the GS uses for a presented frame is the CSR FIELD bit copied at the EE's vsync post, inverted
  (`field = FIELD ? 0 : 1`). The EE flips FIELD about 3.5 scanlines later, so the GS sees the field that was being
  displayed when the vsync started. Confirmed.

### 8.2 What our stream gave it (slice191 menus recording, `pcsx2-recorded.gscap`)

- SetGsCrt(1, NTSC, 1) posted SMODE1 `0x4100` (CMOD 2) and SMODE2 `0x3` (INT, FFMD) only. **SYNCV was never written**
  (the BIOS writes it; the donor HLE does not), so PCSX2's GS saw a non-interlaced output: each half-height field was
  stretched to 448 lines and the next field, rendered half a line lower (XYOFFSET y .25 / .75 alternate per frame),
  bounced by one line. Confirmed (register scan of the stream and replay).
- GT4's VBLANK_S handler (`0x004AB430`, AddIntcHandler cause 2, registered at `0x004A0508`) samples CSR FIELD and
  stores it. The donor flips the field when it raises VBLANK_S, so the handler reads the next field, one field
  earlier than on a PS2 or PCSX2 (where it still reads the old one). DISPFB is written first thing after each
  VSYNC (the buffer drawn in the previous interval). The 0.3.1 consumer used the previous VSYNC's field; with SYNCV
  present that weaves the fields in the wrong order. Confirmed by replay (below); the handler timing reasoning is
  High confidence (translated code read, not traced at run time).

### 8.3 Fix

- Game (MIT, `native/gs_stream/gs_stream_client.cpp`): SetGsCrt also posts SYNCV for NTSC and PAL (GS User's
  Manual timings, VFP 1), between SMODE1 and SMODE2. Progressive, HDTV and VESA modes are unchanged.
- Protocol 1.1 (`docs/GS-STREAM-PROTOCOL.md` 6.1, 7): VSYNC `field` is the display field of the frame that ends at
  that VSYNC. Our producer's values are unchanged (the field after the donor's flip is exactly that), so this only
  fixes the meaning.
- gt4-gs 0.3.2 (fork `gt4gs/deinterlace`, commit `08d394a87`, prerelease `gt4gs-v0.3.2`): CSR FIELD at the vsync post
  = the record's own field (no one-VSYNC shift).

### 8.4 Evidence (headless, `gt4-gs.exe --replay-gscap`, 1x, deinterlace Automatic)

Streams: the first 1,500 frames of the slice191 recording, unchanged (`before`), with SYNCV inserted after SMODE1
(what slice196 sends), and with SYNCV plus inverted fields (equivalent to the 0.3.2 field handling). Metrics:
`tools/gs_frame_metrics` on the static Garage menu, frames 1000-1199 (luma; comb = mean adjacent-line difference /
mean same-field-line difference; flicker = mean frame-to-frame difference).

| Case | comb | flicker (mean/max) | Look |
|---|---|---|---|
| slice195 (0.3.1, no SYNCV) | 0.499 | 3.226 / 3.226 | line-doubled, one-line bounce every frame |
| SYNCV only, old field (0.3.1) | 0.999 | 0.000 / 0.000 | steady but combed (fields in the wrong order) |
| **slice196 (0.3.2, SYNCV)** | **0.567** | **0.000 / 0.000** | full 448-line picture, steady |
| GSRunner `.gs` dump path (converter `--field-from-stream`, SYNCV) | 0.547 | 0.000 / 0.000 | same as slice196 |
| GSRunner `.gs` dump path (no SYNCV) | 0.524 | 3.061 / 3.061 | same as slice195 |

- 0.3.2 with the SYNCV stream gives frames pixel-identical to 0.3.1 with SYNCV and inverted fields: 1,486/1,486.
- 0.3.2 with the old stream (no SYNCV) is pixel-identical to 0.3.1: 1,486/1,486 (field unused without SYNCV).
- Progressive control: the same stream turned into 480p (SMODE1 CMOD 0, SMODE2 0, no SYNCV), 0.3.1 vs 0.3.2:
  1,186/1,186 pixel-identical.
- The published 0.3.2 zip replays pixel-identical to the iteration build (1,486/1,486).
- The GSRunner `.gs` path differs in pixels from the stream replay (its own default settings) but agrees on the
  metrics: the dump's CSR FIELD from the record (`--field-from-stream`) is the right order.
- Samples (before/after PNG pairs, zoom sheet, metric JSON): `private/slice196-deinterlace-samples`.
- Live game (slice196 headless schedule, pcsx2 1x, auto aspect): frames 100-1400 are pixel-identical to the offline
  0.3.2 replay of the SYNCV stream with the same settings (14/14), so the game really sends SYNCV and the fields as
  measured above; slice195 live equals the offline 0.3.1 replay of the old stream (14/14). 40 of the 41 live frames
  differ from slice195. Frame 400 (a flat grey fade) shows a 1-2 luma step between the fields: the fade moved between
  the two fields and the difference is below the motion detector's threshold (Hypothesis: a property of the
  deinterlacer, not of the field order; static frames show no such step).

### 8.5 Not verified

The visible window (owner), interlaced races (480i held, no race capture), PAL and 1080i, the FastMAD look at 3x
and the menu transitions with motion (the metric covers a static menu; 840-880 transitions were checked by eye
only).

## 9. Performance of the PCSX2 GS path (TASK-041, slice198)

Status: **owner tested on gt4-gs 0.3.2** (slice198, 2026-10-16 night race with 5 AI: 59.97 FPS, 53 late frames, was 69;
EE critical path 11.81 of 16.68 ms). Branch `task/041-gs-perf` (main repo only; the fork is unchanged). slice198 pins the
`gt4gs-v0.3.2` package as slice196; **slice199 runs the same changes on the JIT-free gt4-gs 0.4.0** (section 10.5, pin
[`slice199-pcsx2.json`](inputs/slice199-pcsx2.json)). `--mode pcsx2` now selects slice199; `--mode pcsx2-slice198` and
`--mode pcsx2-slice197` keep the A/B pins; `--mode pcsx2-slice196` keeps TASK-047.

### 9.1 A headless race

The slice157 schedule never leaves the menus, so TASK-041 adds a race schedule (`private/slice198-scripts/
headless-race.sh`, card seed `slice110-menu-pilot/name-control4`): Arcade, Single Race, Original Circuits, El Capitan,
Ford Ka '01, 5 AI cars, cross held from frame 8800; the race runs from about frame 9000 to 16000 (dumps every 100
frames show the car on track). It is a day track, but it has readbacks (294 in the run) with the same privileged-write
pattern as the owner's night race. All timings below are headless, unthrottled and at 1x: they compare variants with
each other, not with the owner's paced 60 Hz runs.

### 9.2 Privileged writes and the MTVU sync (goal 1)

Per-register counters (new F8 lines, section 9.5) give the night-race pattern (Confirmed):

- every frame, at the VBlank: PMODE, SMODE2, DISPFB2, DISPLAY2, BGCOLOR (5 writes, worker idle, wait 0);
- per readback: CSR (FINISH clear) and BUSDIR twice (to host before the FIFO read, back after). The owner's night race
  has one readback per frame: 5 + 3 = **8.00 privileged writes per frame**, exactly the owner F8 value.
- With the blocking sync, the whole wait sits before the CSR write: in the readback-dense race interval CSR 2.063 of
  2.067 ms/frame, BUSDIR 0.004. The EE kicks the frame's VIF1 work, then clears CSR for the flare readback while the
  worker still runs the VU1 programs; the sync waits for all of them.

None of the registers needs the blocking sync in pcsx2 mode: CSR/IMR/SIGLBLID reads are answered from the EE copy,
which the donor writes at the program point either way, and the GS process only needs the PRIV record at its stream
position. `GT4_MTVU_PRIV_ASYNC=1` (default in slice198) skips the sync and lets the stream client queue the write in
the MTVU journal (`EeJournal::append_privileged`) when worker output is outstanding; the journal drain posts it after
the earlier GIF output (when nothing is outstanding it is posted at once, as before). Donor mode is unchanged (no
ordered consumer is registered there). `GT4_MTVU_PRIV_ASYNC=0` restores the slice196 path.

Equivalence (Confirmed, headless):

| Check | Result |
|---|---|
| Menus, 4,200 frames: `.gscap` of donor slice198, pcsx2 slice198 (async), pcsx2 slice196 | byte-identical, sha256 `4ff3993d...` (0 writes queued in the menus) |
| Race, 16,000 frames: stream hash (`GT4_GS_STREAM_HASH=1`) async 1 vs 0 | identical, 31,709,575 records, `9b88c23e984ff940`; 588 writes queued (all BUSDIR before a FIFO read) |
| Race frames (gs-frames every 100), async 1 vs 0 | 159/159 identical |
| Menus frames, slice198 pcsx2 1x vs slice196 | 41/41 identical (two runs) |

Effect (Confirmed headless, the night race by inference): **the new gs_privileged wait is 0.000 ms/frame, but the wait
moves to the download sync** a few microseconds later (READ_FIFO must follow the worker's GIF output in the stream,
in both readback modes). Same race interval: privileged 1.323 -> 0, download 0 -> 1.503 ms/frame; over the race,
gs_privileged 0.286 (async 0) vs download 0.307 (async 1). So in the night race the EE critical path should not
change (High confidence); the wait is the readback's dependency on the VU1 worker, not a cost of the sync itself.
The switch stays on because it is proven byte-equivalent and removes waits where a privileged write is not followed
by a readback; the owner A/B (`GT4_MTVU_PRIV_ASYNC=0`) checks it on the real night race.

### 9.3 Readback tail (goal 2)

Race schedule twice with a new, empty pipeline cache folder (`[Folders] Cache`), then with a GS-side spin:

| Run | Readback waits (game side) | GS thread frames over 33 ms |
|---|---|---|
| cold cache | mean 1.56 ms, max 58.0; 12 of 294 at 8 ms or more (5 at 16 ms or more) | 129 |
| same cache, warm | mean 0.60 ms, max 2.7; none at 8 ms or more | 125 |
| warm, `[GT4GS] SpinUs = 300` | mean 0.72 ms, max 3.2 | 128 |

- The long readbacks are pipeline compiles (Confirmed: they disappear on the second run with the cache that the first
  run filled). The owner's 23 ms tail at race start is the same mechanism; TASK-046 ("prepare shaders") is the fix.
  The worktree cache shipped with slice198 already holds the El Capitan race pipelines (1x).
- A longer GS-side spin does not help (mean 0.72 vs 0.60 ms; it only keeps the GS CPU thread awake), so the fork is
  unchanged. Accurate stays the default.
- Headless GS-thread frames over 33 ms are mostly loading screens (same in every run).

### 9.4 Stream copies and wake-ups (goal 3)

Profile of `gs_ipc_write` with the new counter: on the race the producer signalled the GS process's data event
**322 times per frame** (one `SetEvent` system call and one thread wake-up for most GIF records, because the GS CPU
thread drains the ring and sleeps after its 30 us spin). Changes:

- Wake batching (`GT4_GS_WAKE_BYTES`, default 262,144; 0 = every record as before): GIF/PRIV records wake a sleeping
  GS process every 256 KiB; VSYNC, READ_FIFO, RESET and CONTROL wake it at once, and the producer wakes it before it
  waits for ring space ([protocol](GS-STREAM-PROTOCOL.md) section 5). Race: 322 -> 11-14 wake-ups per frame;
  `gs_ipc_write` 0.169-0.180 -> 0.114-0.126 ms per MB of stream (about -30%; slice196 1.10 ms/frame at 6.1 MB, slice198
  0.79 ms/frame at 6.3 MB). Readback mean unchanged (0.68 -> 0.61 ms with the privileged sync on).
- MTVU journal: a worker GIF record that arrives in one chunk (the usual case) is moved into the drain queue instead of
  copied a second time (`EeJournal::pump_output`); one copy of the worker's GIF bytes less on the EE thread.
- Not done: writing the worker's GIF data straight into the shared ring. The bytes must wait in the journal for their
  stream position (EE PATH3 and privileged writes interleave), so one copy into the ring remains.

### 9.5 F8 `[gs-ipc]` section (goal 4)

New lines after the existing ones (ms per frame unless noted):

```
[gs-ipc] privileged writes/frame (total 8.00): PMODE=1.00 SMODE2=1.00 DISPFB2=1.00 DISPLAY2=1.00 BGCOLOR=1.00 CSR=1.00 BUSDIR=2.00
[gs-ipc] EE wait for the VU1 worker before privileged writes ms/frame (total ...): ... CSR=... BUSDIR=...
[gs-ipc] privileged writes queued behind VU1 worker output/frame: 1.00 (GT4_MTVU_PRIV_ASYNC on)
[gs-ipc] GS process woken from sleep per frame: 12.3 (records posted while its CPU thread slept; one event signal each)
[gs-ipc] GS process CPU thread (STATS vsyncs 2500-9500): busy ms/frame=0.948 wait ms/frame=15.199 records/frame=... readbacks served ...
[gs-ipc] summary: EE critical path 13.02 ms/frame of 16.68 budget (78%) = EE CPU 8.76 + waits for the VU1 worker 3.48 + readbacks 0.78 + GS back-pressure 0.00; headroom 3.67 ms; GS process CPU thread 0.95 ms/frame
```

(Example values: the owner's slice194 night race fed into the summary formula, checked by the client fixture.) The
GS process CPU thread line is now the interval (difference of the STATS records), not the cumulative value. The GS
thread and GPU times stay in the `(GSStreamReplayer)` lines every 500 VSYNCs of the same log; carrying them in STATS
would change the fork and its vendored protocol copy, which TASK-042 is rebuilding, so it is left for later.

### 9.6 Donor versus pcsx2 in the same build (goal 5)

slice157 menus schedule, 4,200 frames, headless, two runs each, totals over the F8-equivalent reports:

| Renderer | EE thread CPU | GS work on the EE | Elapsed |
|---|---|---|---|
| donor (CoreGS 1.1) | 7.60 / 7.53 ms/frame | gs_host_copy 0.32 + gs_execute 0.07 + journal GS front end 0.11 | 33.2 / 32.7 s |
| pcsx2 (1x) | 7.30 / 7.10 ms/frame | gs_ipc_write 0.08 + journal drain 0.04 | 33.6 / 32.8 s |

The GS stream costs the EE about 0.35 ms/frame less than the built-in renderer's front end in the menus (Confirmed for
the menus; both are small next to the 6.4 ms of EE game code). Races are the owner's A/B.

### 9.7 Checks and not verified

- CTest 29/29 (new: privileged entries in the random journal scripts, `pcsx2-ordered` stream scenario byte-identical
  to `pcsx2`, register table and summary line, wake batching counts and the back-pressure wake-up); Python 416 OK
  (16 skipped; new `tests/python/test_gs_perf.py`); donor 42/42 identical to slice157 (two runs); pcsx2 1x 41/41
  identical to slice196 (two runs); race stream hash and frames as in 9.2.
- Not verified: the owner's night race (paced, 3x, visible window): whether the late frames change, the new F8 lines
  with real values, and whether wake batching changes GS-thread smoothness at 60 Hz. The headless race is a day track
  at 1x.

## 10. No JIT (TASK-042, slice197, gt4-gs 0.4.0)

Status: **built, published and checked headless; owner windowed check pending** (2026-10-16). Branch
`task/042-gs-nojit` (main) and `gt4gs/nojit` in the fork (commit `4c8b63a6e`, prerelease `gt4gs-v0.4.0`). `--mode pcsx2`
now selects [`slice197-pcsx2.json`](inputs/slice197-pcsx2.json); `--mode pcsx2-slice196` keeps 0.3.2. The game runtime
is the slice196 build, unchanged (same file and hash); only the GS package changes. Project rule: no JIT ever
(GPU driver shader compilation is allowed).

### 10.1 Audit of 0.3.2 (fork sources and the 0.3.2 binary; PCSX2-tainted reading, no code copied here)

| Site | 0.3.2 | Run in our replay mode (0.3.2) | 0.4.0 |
|---|---|---|---|
| EE recompiler (`pcsx2/x86/ix86-32/*`, `iR5900*`), `recCpu.Reserve` | compiled in | reserved at start (tables and code pointers, no code emitted; High confidence); never executed (the stream replayer is the CPU) | not compiled |
| IOP recompiler (`iR3000A*`), `psxRec.Reserve` | compiled in | reserved, never executed | not compiled |
| microVU VU0/VU1 (`pcsx2/x86/microVU*`) | compiled in | reserved (and opens the MTVU thread), never executed (VU interpreters selected) | not compiled; MTVU thread opened as on ARM64 |
| VIF unpack recompilers (`Vif_UnpackSSE`, `Vif_Dynarec`) | compiled in | **code generated at start** (`VifUnpackSSE_Init`), never called (no VIF in a GS stream) | not compiled; interpreter loops |
| x86 emitter (`common/emitter/*`) | compiled in | used by the generators above | not compiled |
| Recompiler code area (`Memory.cpp`, `PageAccess_Any`) | 305 MB mapped `PAGE_EXECUTE_READWRITE` at start | yes, always | not reserved; `HostSys` refuses executable protections (`pxFailRel`) |
| GS software JIT (xbyak: `GSDrawScanlineCodeGenerator`, `GSSetupPrimCodeGenerator`, 3 ISAs) | compiled in | only with CPU sprite render, CPU CLUT render or the software renderer: none of them in GT4's defaults (0 of 13 runs logged `SW JIT generated`); with both hacks on, 30,523 bytes generated | not compiled; C rasterizer (`CSetupPrim`, `CDrawScanline`, `CDrawEdge`) |
| GS vertex trace, MultiISA templates | compiled | ahead-of-time code per ISA, not a code generator | unchanged |
| Bundled DLLs (SDL3, shaderc, FreeType, HarfBuzz, libpng, libjpeg, libwebp, zstd, lz4, ryml, PlutoSVG) | no code generator markers, no writable and executable sections | shaderc compiles GLSL to SPIR-V (data) | unchanged; no FFmpeg or x264 DLLs (local deps without FFmpeg since 0.3.1) |
| GPU driver (AMD Vulkan, system DLLs) | outside the package | compiles shaders and pipelines (allowed) | unchanged; works under Arbitrary Code Guard |

### 10.2 What changed (fork)

The CMake option `GT4GS_ONLY` (macro `GT4GS_NO_JIT`; details in the fork's `gt4gs/BUILD.md`, section "No JIT") leaves out
the recompilers, the x86 emitter, the GS JIT generators and the xbyak include path; selects the interpreters as PCSX2's
ARM64 build does; maps no code area; refuses executable page protections; forces the C rasterizer; and writes the
recompiler settings off. `gt4-gs.exe` enables Windows **Arbitrary Code Guard** (`ProcessDynamicCodePolicy`,
`ProhibitDynamicCode`) before its window, threads and GS device exist, proves it with a probe allocation that must fail,
and walks its address space at the end of the stream and at exit. The log shows:

```
GT4GS no-JIT: Arbitrary Code Guard on (ProhibitDynamicCode=1); probe RWX allocation refused, error 1655
GT4GS no-JIT audit (end of stream, GS device open): 1438 regions, 0 executable non-image regions
```

`[GT4GS] ProhibitDynamicCode = false` switches only the OS enforcement off (for a graphics driver that needs dynamic
code inside our process); the launcher does not write it. No GS setting needs the JIT, so none is refused: CPU sprite and
CLUT render and the software renderer run the C rasterizer (notes in `docs/launcher/gs-settings.toml`).

### 10.3 Evidence (2026-10-16)

- **Static** (`tools/gs_nojit_check/check-nojit.ps1`, copy of the fork's `gt4gs/check-nojit.ps1`; `dumpbin` 14.44):
  0.4.0 package: 23 modules, 0 code generator markers, 0 writable and executable sections, NX on, `gt4-gs.exe` imports
  `SetProcessMitigationPolicy`. Positive control 0.3.2: 22 markers in `gt4-gs.exe` (xbyak RTTI, JIT classes and
  messages, microVU/EE/IOP/VIF recompiler names, `Recompiler Cache`, emitter and `pcsx2\x86` source paths). The
  Python test `tests/python/test_gs_nojit.py` repeats the marker scan. `release-local.ps1` refuses a package that fails
  the check. Confirmed.
- **Runtime, ACG**: works with the AMD Vulkan driver of this PC (RX 9060 XT): every replay and the live headless run
  completed with ACG on, also with an empty shader cache (shaders compiled by shaderc, pipelines by the driver); the
  probe allocation is refused with `ERROR_DYNAMIC_CODE_BLOCKED` (1655); 0 executable non-image regions in every audit.
  Other drivers: Unknown.
- **Image parity** (offline `--replay-gscap`, 4,200 vsyncs, deinterlace Automatic, 16:9, the GT4 defaults): the slice191
  menus stream (no SYNCV; re-captured with slice195, sha256 `e651bba3...`, identical to the TASK-035 capture) and the
  slice196 stream (SYNCV, sha256 `4ff3993d...`): 1x every 5th frame 837/837 and 3x every 25th frame 167/167 PNGs
  **byte-identical** to 0.3.2, for the iteration build and the published zip. With an empty shader cache, 3x: 41/41
  identical. With CPU sprite render (BW 10, level 2) and CPU CLUT render on (0.3.2 generated 30,523 bytes of JIT code):
  167/167 identical, so the C rasterizer matches the JIT here. Live headless game (slice157 schedule, pcsx2 1x, auto
  aspect): 41/41 frames identical to slice196 live; GS process exit 0; donor 42/42 identical to slice157. Confirmed.
- **Performance** (PCSX2 "Average GS Thread Time", offline replay of the slice196 stream without frame dumps,
  unthrottled, interleaved runs, no compiler running; another process used about one core throughout): 1x 0.42 ms/frame
  (0.3.2, 3 runs) and 0.42 (0.4.0, 3 runs); 3x 0.49 (0.3.2, 9 runs, 0.36-0.64) and 0.56 (0.4.0, 9 runs, 0.45-0.63),
  ACG on or off the same (0.57-0.59). The 3x difference is within the run-to-run spread (Hypothesis: noise; the replay
  path does not touch the removed code). With CPU sprite and CLUT render on: 5.4 (JIT) against 6.5 ms/frame (C).

### 10.4 Not verified

The visible window and owner play with 0.4.0; races and the night race (no race capture); D3D11/D3D12 under ACG;
other GPU vendors' drivers under ACG; the software renderer (`Renderer = 13`) with the C rasterizer.

### 10.5 TASK-041 perf changes on gt4-gs 0.4.0 (slice199)

slice199 keeps the slice198 game build (same file and hash) and pins the JIT-free `gt4-gs.exe` 0.4.0 of slice197
(`gs_package` tree `e9402cf2...`, 140 files; exe sha256 `1c4c45da...`). The privileged-write journal, wake batching
(`GT4_GS_WAKE_BYTES`), F8 `[gs-ipc]` lines and critical-path summary of section 9 therefore run with 0.4.0. The owner
night-race numbers of section 9 were measured with 0.3.2; the 0.4.0 night race and F8 A/B (`GT4_MTVU_PRIV_ASYNC` 1 vs 0)
are pending. Checks (2026-10-16, headless, slice157 menus schedule): donor 42/42 identical to slice157; pcsx2 1x gs-frames 41/41 identical to slice198 (gt4-gs 0.3.2); Python 438 OK, 20 skipped; CTest 29/29; static no-JIT check passes (23 modules).

