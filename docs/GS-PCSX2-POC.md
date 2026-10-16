# GS PCSX2 proof of concept (TASK-034)

Status: **done, offline only** (2026-10-15). Branch `task/034-gs-replay-compare`. Labels: **Confirmed** (read in a
log, a file or a measurement), **High confidence**, **Hypothesis**, **Unknown**.

Question: can the PCSX2 GS (GSdx lineage, GPL-3.0+) render GT4 correctly from OUR GS stream, replayed offline
through a stock `pcsx2-gsrunner`? Decision 0070 (separate `gt4-gs.exe` process) and plan
GSDX-PLAN section F (TASK-034) are the context.

Clean-room note: this work is PCSX2-tainted (agents read and built PCSX2). Nothing here touches CoreGS 2.0
(TASK-027, `native/hw_gs`, `task/027-*`). No PCSX2 code is copied into MIT files. The converter is written from
the `.gs` dump layout (facts listed in section 3, with citations). The one local PCSX2 change (a boot-error print in
`pcsx2-gsrunner/Main.cpp`, section 2.4) is a diagnostic in the private clone and is not committed anywhere.

## 1. Result and recommendation

- The PCSX2 GS renders the menu capture (1,101 frames, SCUS-97328) from our converted stream with no errors,
  1,100 frames dumped at 1x and at 3x. Confirmed.
- On the sampled frames, 1x against the donor-equivalent references: frame 300 and frame 700 match at 0.00% of
  pixels above 8 (700: PSNR 52.4 dB against cpuref, 52.9 dB against the donor). Frame 60 (title card text): 1.10%.
  Frames 900 and 1100 differ strongly (23-60% of pixels; section 5). Confirmed for the measured frames only.
- The GameDB GT4 fixes are applied from the replay log, but one of them (`recommendedBlendingLevel`) is only a
  warning. Confirmed.
- Blocking problems before the live MVP: the PCSX2 output is 640x480 and the references 640x448; the best match
  suggests the PCSX2 image is stretched vertically (section 5.6), and the 900/1100 differences are unexplained
  (section 8).

**Recommendation: GO for TASK-033 (fork, reproducible build) and TASK-035 (MIT stream protocol on main, which does
not depend on PCSX2 output). CONDITIONAL GO for TASK-036 (live `gt4-gs.exe` MVP)**, conditions: (a) explain the 480-versus-448
vertical scale and make the 1x comparison pixel-exact in the static scenes; (b) an owner side-by-side
check of frames 900 and 1100 against the real game, to decide which renderer is right there. Reasons in section 8.

## 2. Build recipe

### 2.1 Tools (this machine, Windows 11 Pro, Ryzen 7 5700X, RX 9060 XT, AMD driver 32.0.32015.2008)

| Tool | Version |
|---|---|
| Visual Studio 2022 BuildTools, MSVC | 19.44.35222 (toolset 14.44.35207), `cl.exe` x64 |
| CMake | 4.3.1 |
| Ninja | 1.13.2 |
| Vulkan SDK | 1.4.341.1 (headers, `shaderc_shared.lib`, `shaderc_shared.dll`) |
| Git for Windows `usr/bin` | gawk 5.3.2, sh, tar, patch, Git 2.53.0 |
| 7-Zip | system copy (`C:\Program Files\7-Zip\7z.exe`) |
| Python | 3.14.2 (converter and tests; stdlib only) |
| ffmpeg CLI (WinGet Gyan) | 8.0.1 (used by the compare script to decode PNG) |

### 2.2 PCSX2 source

- Fresh clone in `private/slice190-pcsx2-poc`: `git init`, `git remote add origin https://github.com/PCSX2/pcsx2.git`,
  `git fetch --depth 1 origin 685637d30b5eaa8e5d7a587673a17577a92dcb88`, checkout. Confirmed (151 MB).
- The read-only `private/slice189-pcsx2-ref` clone was not modified.

### 2.3 Dependencies (trimmed; versions and SHA-256 from the upstream Windows recipe)

Built with MSVC, Ninja, Release, into `private/slice190-deps/install`. Fetched archives are checked against the
upstream SHA-256 values (14 archives, all matched).

- zlib 1.3.2, zstd 1.5.7, LZ4 1.10.0, libpng 1.6.58 (APNG patch not applied), libjpeg-turbo 3.2.0,
  libwebp 1.6.0, FreeType 2.14.3 (no HarfBuzz), SDL3 3.4.18, DirectX-Headers 1.619.5, rapidyaml 0.12.1,
  plutovg 1.3.3, plutosvg 0.0.8.
- FFmpeg 9.0.1, minimal shared build (`--disable-all`, avcodec/avformat/avutil/swresample/swscale, encoders
  ffv1/qtrle/aac/flac/pcm, muxers avi/matroska/mov/mp4/wav, file protocol, `--toolchain=msvc`,
  `--disable-x86asm`). Needs GNU make 4.4.1 (ezwinports build). CMake 4.x needs
  `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` for the older CMake files.
- Skipped on purpose: Qt, KDDockWidgets, QtApng, Agility SDK, DXC, AMF, libvpl, NVENC headers, x264, SVT-AV1,
  Opus, HarfBuzz. Shaderc is not built; the Vulkan SDK's `shaderc_shared` is used through
  `-DSHADERC_PATH_INCLUDES` and `-DSHADERC_PATH_LIB`.
- Upstream's `cmake/SearchForStuff.cmake` requires FFmpeg 7.1 or newer with five components (line 25), so FFmpeg is
  built even though `USE_LINKED_FFMPEG=OFF` loads it at run time. Not tried with headers only.
- **Friction (Confirmed):** the FFmpeg MSVC build fails under this MSYS setup: the dependency filter in
  `ffbuild/common.mak` passes an awk program that Git's gawk cannot parse, and `make` from `cmd` ignores `SHELL`.
  Workaround used for the PoC only: `shim-bin/awk`, a wrapper that discards the dependency filter (writing empty `.d`
  files, so no header-based rebuild tracking) and passes every other awk call to the real awk (configure's
  `print_config` needs it). This must be fixed properly in TASK-033 (for example a Linux-style `sh` build or a
  pinned MSYS2 toolchain).
- Time (approximate, wall clock): fetch 50 s; the small dependencies 2-3 min; FFmpeg configure 10-12 min and build
  4-5 min; about 40 min in total, including five failed attempts and their reruns. Measured here, not a clean-machine
  number.

### 2.4 pcsx2-gsrunner

Configure and build (`private/slice190-pcsx2-poc/build-gsrunner.bat`, run from `vcvars64.bat`):

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
  -DCMAKE_PREFIX_PATH=<deps>\install -DENABLE_QT_UI=OFF -DENABLE_GSRUNNER=ON -DENABLE_TESTS=OFF
  -DUSE_LINKED_FFMPEG=OFF -DDISABLE_ADVANCE_SIMD=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5
  -DSHADERC_PATH_INCLUDES="C:/VulkanSDK/1.4.341.1/Include" -DSHADERC_PATH_LIB="C:/VulkanSDK/1.4.341.1/Lib"
cmake --build build --target pcsx2-gsrunner --parallel
```

- Configure 20 s; build 746 steps in 2 min 17 s (16 threads). Output `pcsx2-gsrunner.exe` 9.9 MB. Confirmed.
- Runtime: the exe needs the dependency DLLs next to it, `resources/` (copied from `bin/resources`), the Vulkan SDK
  `shaderc_shared.dll` and `SPIRV-Tools-shared.dll`. Confirmed.
- Local diagnostic (not committed): `VMManager::Initialize` was called without an `Error` object, so its failure text
  was dropped. The private clone's `Main.cpp` now writes the boot error to `slice190-gsrunner-out/boot-error.txt`.
  This is how the forward-slash path problem below was found.
- Upstream PCSX2 `AGENTS.md` restricts LLM-assisted contributions to PCSX2 itself. Nothing here is proposed upstream.

## 3. Converter (`tools/gscap_to_gsdump/gscap_to_gsdump.py`, MIT)

### 3.1 Input (our capture, `GT4_PGS_CAPTURE` format, `native/parallel_gs_backend.cpp` on `origin/task/012-parallel-gs`)

| Record | Layout |
|---|---|
| `G` | u8 path, u32 bytes, data (GIF bytes, multiple of 16) |
| `P` | u32 guest address, u64 value (privileged GS register; 0x12000000-0x12001FFF) |
| `R` | u32 qwords (local-to-host FIFO read) |
| `V` | u64 frame (one per VSync, written after the frame's records) |

### 3.2 Output (`.gs`), format facts and where they come from

Facts are from PCSX2 `pcsx2/GS/GSDump.cpp` (`AddHeader`, `Transfer`, `VSync`, `ReadFIFO`), `pcsx2/GS/GSLzma.cpp`
(`GSDumpFile::ReadFile`), `pcsx2/GSDumpReplayer.cpp` (`GSDumpReplayerCpuStep`, `GSDumpReplayerLoadInitialState`) and
`pcsx2/MTGS.cpp` (`GSPacket` dispatch). PCSX2 is GPL-3.0+; these are read as a format description, no code copied.

```
u32 0xFFFFFFFF                           new-header marker
u32 header_size                          = 36 + serial length (no screenshot)
GSDumpHeader: 9 x u32                    state_version (9, GSState::STATE_VERSION), state_size, serial_offset (36),
                                         serial_size, crc, screenshot width/height/offset/size (0)
serial bytes                             "SCUS-97328"
state bytes (state_size)                 GS freeze blob; empty in this PoC (section 6, problem 1)
8192 bytes                               initial privileged block (zero)
packets:
  0x00 u8 path, u32 size, data           transfer; path byte 3 ("Path1New") for every packet
  0x02 u32 qwords                        ReadFIFO request (not in this capture)
  0x03 8192 bytes                        privileged block, written before each VSync
  0x01 u8 field                          VSync
```

- Path byte 3: PCSX2's own GS writer records every packet that reaches `GSgifTransfer` (`Transfer<3>`) with index 3,
  and the replayer sends all of them through the same `GSPacket` path (`MTGS.cpp`), so one GIF tag state machine
  receives the whole stream. Our capture's path number (1, 2, 3) is therefore not written. Confirmed in code; the
  effect on output is Hypothesis until a test shows path 1 and path 2/3 interleaving is safe.
- The privileged block is the 8 KiB PS2MEM_GS image: each `P` write is stored at `address - 0x12000000`. The CSR
  FIELD bit (0x2000 of the word at offset 0x1000) is set from the frame number, because the real GS toggles it and the
  capture does not record it. `--field-phase` selects the parity. PCSX2 derives the field from CSR bit 0x2000
  (`MTGS.cpp`, `GSvsync`), so the bit matters.
- Header CRC is 0 (the capture has no ELF CRC); the serial alone selects the GameDB entry (section 4).

### 3.3 Results on the menu capture

`private/slice175-capture/menu-0-1100.gscap` (517,600,834 bytes) to `private/slice190-dump/menu-0-1100.gs`
(526,550,483 bytes), 1.3 s. Stats in `private/slice190-dump/convert-stats.json`:

| Records | Count | Output |
|---|---|---|
| `G` (transfers) | 481,961 (514,627,776 bytes; equal to `replay-menu.log`) | 481,961 packets |
| `P` (privileged writes) | 5,491, none out of range | 1,101 register packets |
| `R` (FIFO reads) | 0 | 0 |
| `V` (frames) | 1,101 | 1,101 VSync packets |
| CSR writes with RESET bit 0x200 | 1 (the start-up reset; not replayed) | |

### 3.4 Tests

`tests/python/test_gscap_to_gsdump.py`: 23 unit tests, all passing (`python -m unittest discover -s tests/python -p
test_gscap_to_gsdump.py`). The reader is independent of the writer. They cover record parsing and refusal of unknown or
truncated records, header and serial, packet order (register block before each VSync), latest-write semantics,
out-of-range and CSR-reset counting, field parity (both phases), `--max-frames`, the optional synthetic capture
(`private/slice173-synthetic`), PPM reading, the PNG writer, the box downscale and the comparison metrics.

## 4. Replay

Command (1x; 3x adds `-upscale 3`):

```
pcsx2-gsrunner.exe -surfaceless -renderer vulkan [-upscale 3] -loop 1 -perf -logfile <dir>\gsrunner.log
  -dumpdir <dir>\f  C:\...\private\slice190-dump\menu-0-1100.gs
```

- Windows-style paths are required. With forward slashes the file check fails (`Requested filename ... does not
  exist`, found with the diagnostic patch). Confirmed.
- Output: `f/menu-0-1100_frameNNNNN.png`, NNNNN = 1..1100 (the first VSync is not dumped). 1x PNGs are RGBA 640x480
  (51 MB); 3x PNGs are 1920x1440 (249 MB). Dumps are taken on every frame.
- Run times with dumps on: 1x about 20 s, 3x about 80 s (dump cost included).
- The replay stopped at the end of the dump, exit 0, with `-loop 1`. Confirmed.

## 5. Results

### 5.1 Method

Reference: the task/027 CPU reference replay (`hw_cpuref_replay.exe`, run as a black box on the same capture into
`private/slice190-cpuref`; identical to the earlier copy in `slice175-capture/cpuref-menu`) and the donor frames in
`private/slice157-smoke/headless-157/frames` (640x448 PPM, but only frames 100, 200, ..., 4200 exist). PCSX2 frame
N is compared with reference frame N (offset 0 was the best of -2..+2 in the scan; the scan is weak because the menus
are static). The metric is `compare_frames.py`: pct_bad = percent of pixels where any channel differs by more than 8;
PSNR over all channels; the vertical crop of the 480-row PCSX2 frame is chosen per frame (rows 0-32) to match the
448-row reference, and the chosen top is reported.

### 5.2 Required frames (1x and 3x against cpuref; 3x box-downscaled to 640x480)

| Frame | 1x pct_bad | 1x PSNR (dB) | 1x max delta | 1x crop top | 3x pct_bad (top 13) | 3x PSNR |
|---|---|---|---|---|---|---|
| 60 | 1.10 | 26.07 | 248 | 13 | 2.10 | 25.85 |
| 300 | 0.00 | inf | 0 | 0 | 0.00 | inf |
| 700 | 0.00 | 52.38 | 6 | 0 | 0.00 | 62.27 |
| 900 | 59.99 | 7.56 | 253 | 5 | 68.23 | 7.73 |
| 1100 | 23.60 | 16.46 | 210 | 2 | 29.47 | 16.82 |

Against the donor frames (1x, per-frame crop): 300 0.00 (inf dB), 700 0.00 (52.94 dB), 900 47.53 (14.83 dB, top 6),
1000 23.07 (16.39 dB, top 3), 1100 23.07 (16.39 dB, top 3). The references disagree with each other as well: the earlier
`compare-menu.json` gives cpuref against donor 44% at frame 900 and 7.5% at 1100, and about 0-2% on 100-700. So the
references themselves are not a single truth on 820-1100 (section 8, problem 3).

Frame 60 is the Sony "Presents" title card (black background, white text): the differences are glyph edges and a
small position difference, not a missing layer (side-by-side below).

### 5.3 Frame sweep (1x against cpuref, every 10th frame, crop fixed at 13 rows)

Summary: frames 10-230 between 0.0 and 1.1% (20: 0.64%; 10: 0.00%); 260, 270, 300 and 620-700 exactly 0.00%; 320-480
about 0.45%; 490-600 rising from 0.9 to 1.5%; 710-810 about 0.4-0.7% (820: 33.6%); 830-910 at 47-68%; 920 0.00%;
930-1100 at 26-30%. The fixed crop of 13 is wrong for several of these frames (5.2), so the 830-1100 numbers are an
upper bound. The full JSON is `slice190-compare/report-1x-sequence.json`.

### 5.4 Controls

| Run | Frames 300 / 700 / 900 / 1100 pct_bad | Note |
|---|---|---|
| 1x, field phase 0 (main) | 0.00 / 0.00 / 59.99 / 23.60 | |
| 1x, field phase 1 (control) | 0.00 / 0.00 / 59.99 / 23.60 | identical: the field phase does not change these frames |
| 1x, `accurate_blending_unit = 3` (High) | 0.00 / 0.00 / 59.99 / 23.60 | identical to Basic on these frames |

### 5.6 Vertical scale test (nearest-row resample of the PCSX2 1x frame from 480 to the reference height)

| Frame | vs cpuref, no resample | vs cpuref, resampled | vs donor, no resample | vs donor, resampled |
|---|---|---|---|---|
| 60 | 1.10% (crop 13) | 1.89% (21.5 dB) | not in donor set | |
| 300 | 0.00% | 0.00% | 0.00% | 0.00% |
| 700 | 0.00% (52.4 dB) | 0.00% (62.0 dB) | 0.00% (52.9 dB) | 0.00% (57.4 dB) |
| 900 | 59.99% | 49.02% | 47.53% | 18.00% (24.2 dB) |
| 1000 | | | 23.07% | 8.04% (28.1 dB) |
| 1100 | 23.60% | 11.52% (23.2 dB) | 23.07% | 8.04% (28.1 dB) |

Script used: a local test over `compare_frames` helpers (`private/slice190-deps/resample-test.py`); not part of
the committed tools. A bilinear or area resample has not been tried.

### 5.5 Side-by-side images (left to right)

Each sheet: reference (cpuref, 640x448) | PCSX2 1x (cropped to the reference rows, per-frame crop) | PCSX2 3x
(box-downscaled from 1920x1440 to 640x480, full frame).

- `private/slice190-compare/side-by-side/frame_00060_side_by_side.png` (title card)
- `private/slice190-compare/side-by-side/frame_00300_side_by_side.png` (identical)
- `private/slice190-compare/side-by-side/frame_00700_side_by_side.png` (near identical)
- `private/slice190-compare/side-by-side/frame_00900_side_by_side.png` (garage: cpuref shows noise, PCSX2
  shows the garage UI; the donor at 900 is clean)
- `private/slice190-compare/side-by-side/frame_01100_side_by_side.png` (sort menu)
- Donor sheets: `private/slice190-compare/donor-side-by-side/` (frames 300, 700, 900, 1100; the donor frame at 900 is
  clean, like PCSX2).

Full-resolution frames: `private/slice190-gsrunner-out/1x/f/menu-0-1100_frameNNNNN.png` and
`private/slice190-gsrunner-out/3x/f/menu-0-1100_frameNNNNN.png`. Reports: `private/slice190-compare/report-*.json`.

## 6. GameDB fixes (SCUS-97328) and EE state

From `slice190-gsrunner-out/1x/gsrunner.log` (Confirmed):

```
GameDB: Enabled GS Hardware Fix: halfPixelOffset to [mode=4]          (Native)
GameDB: Enabled GS Hardware Fix: autoFlush to [mode=1]
GameDB: Enabled GS Hardware Fix: getSkipCount to [mode=12]            (GSC_PolyphonyDigitalGames)
GameDB: Enabled GS Hardware Fix: recommendedBlendingLevel to [mode=3] (High; warning only, see below)
GameDB: Changing VU0 clamp mode [mode=2]  /  VU1 clamp mode [mode=2]  (VU emulation not running: not applicable)
```

- The serial is read from the dump header (`Serial: SCUS-97328`), the CRC is 0. The entry is found by serial.
- `recommendedBlendingLevel` does not change the setting. `GameDatabase.cpp` (around lines 925-951) only shows the
  OSD message "Current Blending Accuracy is Basic. Recommended Blending Accuracy for this game is High." The 1x log
  with the default settings shows the message (`HWBlendingWarning`) and the setting stays Basic. The explicit
  `accurate_blending_unit = 3` run gives identical numbers on the sampled frames (5.4). **So the launcher must set
  High itself** (GSDX-PLAN D.1 already lists High as the GT4 default).
- EE state: none of the four GS fixes reads EE state. They are GS-side settings (vertex offset, flush on transfer,
  draw-pattern skip, blending level). High confidence from the names and the GameDB code paths; the GS-side hacks were
  not read function by function in this PoC.
- The VU clamp settings do not apply (our VU code does not run in PCSX2). Confirmed by definition.
- Built-in game patches are not available: `Failed to open ... patches.zip` (the resources folder has no
  `patches.zip`). Not used by this PoC; to be decided for the release.

## 7. Timing (Confirmed where stated; limited)

| Run | GS thread ms/frame | GS thread usage | Notes |
|---|---|---|---|
| 1x, dumps on, `-perf` | 1.641 +/- 0.285 | 15.0% | 85.6 FPS average, 11.7 ms frame; 1,066 readbacks (dump snapshots) |
| 1x, accurate_blending High, dumps on | 1.564 +/- 0.781 | | |
| 3x (`-upscale 3`), dumps on, `-perf` | 6.417 +/- 0.426 | 8.9% | 13.2 FPS, 75.7 ms frame; dump snapshots dominate the frame time |
| 1x and 3x, no dumps | not measured | | the 1,101 frames finished in about 2 s wall, unthrottled; the perf window was too short for the per-second lines; no readbacks (0) |

- The 75 ms 3x frame time is dominated by the per-frame PNG snapshot and its readback, not by the GS. The no-dump
  numbers are not timing results. The owner needs a longer, dump-free measurement (TASK-035/036 gate).
- GPU time: `Average GPU Time 0.000 ms` on every run; GPU timestamps were not captured. Unknown.
- The draw statistics (no dumps): 17,328 draw calls (avg 17 per frame), 3,858 render passes, 1,120 copies, 403 uploads.

## 8. Problems found and what they mean

1. **No GS freeze state.** The dump header has `state_size = 0`, so `GSDumpReplayerLoadInitialState` fails with
   `ReportErrorAsync: GSDumpReplayer: Failed to load GS state.` Hypothesis: the GS keeps the state it had at open time.
   This is acceptable only for a capture that starts at power-on, which this one does. A real freeze blob is needed
   for captures that start later. Hypothesis: our VRAM is zero at power-on (the capture starts at boot).
2. **Vertical scale, not offset (Hypothesis, supported by a test).** PCSX2 writes 640x480 frames, the references
   640x448. A best-offset crop fits each frame differently (rows 0-13), which does not look like a pure offset.
   Nearest-row resampling of the PCSX2 frame to 448 rows (section 5.6) lowers the donor differences a lot (frame 900:
   47.5% to 18.0%; frames 1000 and 1100: 23.1% to 8.0%). So the PCSX2 presentation probably stretches the 448-line
   image to 480 lines, and the references do not. Confirmed only as a test result; the PCSX2 cause is not found
   (display height, PCRTC settings or the dump size). Frame 60 gets worse with the resample (1.1% to 1.9%): unexplained.
3. **Divergence on frames 820-910 and 930-1100.** At 900 the cpuref frame shows noise over the garage window, while the
   donor frame and PCSX2 (1x and 3x) both show a clean garage. At 1100 PCSX2 and both references show the same sort
   menu, with the vertical scale of problem 2 on top. Confirmed as measured differences. **Unknown which side is right
   in the real game**; the owner should look at these frames in the real game (TASK-036 gate).
4. **Field handling not exercised.** The field-phase control shows identical numbers, so these frames do not depend
   on the field bit in this path. Unknown for interlaced races (480i).
5. **Readbacks.** The 1,066 readbacks in the dump runs come from the dump snapshot path, not from the game (no-dump runs:
   0). The night-race readback cost (GSDX-PLAN A.1) is therefore untested here. Unknown.
6. **Path numbering.** All packets go to path 3 in the dump (section 3.2). Hypothesis that this is safe for interleaved
   paths; not tested.
7. **Build friction.** FFmpeg needs the awk shim (section 2.3). TASK-033 must fix this properly.
8. **Silent boot failures.** `pcsx2-gsrunner` drops the boot error text (it calls `VMManager::Initialize` without an
   `Error`). Confirmed. Reported upstream? No (AGENTS.md rules); diagnosed locally only.

## 9. Recommendation for TASK-035 and TASK-036

- **GO, TASK-033 and TASK-035.** The stock GS accepts our stream, runs the GameDB fixes from the serial, and
  renders the menus at 1x and 3x with 0.00% differences on several frames. The protocol and seam work on main does
  not depend on any PCSX2 output. Confirmed for the menus only.
- **CONDITIONAL GO, TASK-036 (live MVP).** Conditions: (a) resolve the vertical scale (problem 2) so 1x static frames
  are exact and moving frames match within the expected filtering; (b) owner check of frames 900 and 1100 (problem 3);
  (c) a freeze blob for captures that do not start at power-on, or a rule that the live path always starts from a
  reset; (d) a dump-free timing run of at least 30 s at 1x and 3x.
- **Not a blocker for the decision:** the blending-High requirement (set it explicitly, section 6), the missing
  `patches.zip`, and the readback cost, which must be measured in TASK-037.

## 10. Not verified

- Interlaced races (480i) and the field bit; the field control did not change the output.
- Texture-heavy and race scenes; only the menus (frames 1-1100) were replayed.
- GPU time, real frame pacing with the game, readback cost of a night race, and the GS thread time without dumps.
- Whether the references or PCSX2 are correct on frames 820-1100.
- Output of the CoreGS 1.1 or any other renderer on this capture (not requested).
- A clean-machine build time and a reproducible FFmpeg build without the awk shim.

## 11. Sample paths

- Converter: `tools/gscap_to_gsdump/gscap_to_gsdump.py`, `tools/gscap_to_gsdump/compare_frames.py`, tests
  `tests/python/test_gscap_to_gsdump.py`.
- Dump: `private/slice190-dump/menu-0-1100.gs`, stats `private/slice190-dump/convert-stats.json`.
- PCSX2 build: `private/slice190-pcsx2-poc/build/pcsx2-gsrunner/pcsx2-gsrunner.exe`; run folder
  `private/slice190-run`; deps `private/slice190-deps/install`.
- Outputs: `private/slice190-gsrunner-out/{1x,3x}` (frames `f/`, logs `gsrunner.log`), `1x-blend-high`,
  `1x-phase1`, `perf-nodump-{1x,3x}`.
- Reference: `private/slice190-cpuref` (cpuref replay, 1,101 frames).
- Comparisons: `private/slice190-compare` (JSON reports, side-by-side sheets).
