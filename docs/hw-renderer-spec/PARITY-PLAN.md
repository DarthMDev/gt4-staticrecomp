# CoreGS 2.0: performance parity plan (TASK-027)

Hand-over document for the next implementer of TASK-027. Written on 2026-10-23 by the previous writer (Claude) when
the owner moved the work to another agent. Read it together with `AGENTS.md`, `docs/hw-renderer-spec/PROGRESS.md`,
`docs/hw-renderer-spec/IMPLEMENTATION-NOTES.md` (IN-371 to IN-382) and the ai-memory page
`decisions/coregs2-implementation` (workspace `default`, project `kaezzey-gt4-staticrecomp`).

## 1. Goal and owner decisions

- **Goal (owner, 2026-10-23):** CoreGS 2.0 must reach at least GSdx's performance at the same internal scale. This
  is a requirement for the beta and for the launcher.
  - Owner reading at 6x on the Fuji Test Course start: gt4-gs at 37% GPU load, 1,357 MHz, about 41 W.
  - CoreGS 2.0 candidate 271 at 6x with AA: about 105-120 W at about 3,300 MHz.
- **Item 1, fast scaled raster: approved.** It may relax only the bit-exactness of the presentation-only scaled
  image, with an explicit tolerance. The 1x words, the GLM, memory, readbacks, and every 1x and memory gate stay
  exact and unchanged.
- **Item 2, tone-sequence HLE at 1x: approved.** It must be memory-exact.
- **Items 3 and 4:** do them if time allows.
- **Item 5, skipping the 1x redraw: rejected.**
- Already met (owner test 8): races render at real resolution at 6x with FXAA; the beta minimum (upscaling and AA)
  is met in races.

## 2. Current state

- **Branch:** `task/027-coregs2`, last green commit **ce15f2a** (pushed). Nothing of items 1 and 2 is committed.
  - The working tree is clean.
  - The build tree `O:\gt4recomp-scratch\coregs2-slice270-present\build` contains binaries of reverted experiments,
    so rebuild before using it.
- **Candidate 272:** `O:\gt4recomp-scratch\coregs2-candidate-272`.
  - `gt4vulkan.exe` sha256 `6a0506e174944bcac7a830d422d90fb6615518f90c249f540534aba142145c4d`.
  - Not yet tested by the owner (owner test 9 is pending).
  - Contents: 480i menus at scale (IN-381) and no scaled repeat of the tone sequence (IN-382), on top of
    IN-371..IN-380.
- **Last gates (slice272):**
  - CTest 243/243 with and without interlock.
  - Memory hashes identical on all five captures (night `fifo_hash=3a2fe617a174e7e7`).
  - Menu dumps 42/42 = slice228; race 42/42 = slice235; default renderer 42/42 = slice214.
  - Race `--check-presented`: 0 low-detail frames at 2x/4x/6x.
  - Scaled memory equal to 1x.

### Measurement (clean machine, 2026-10-23)

GPU ms per frame on the 480p race replay (`private/slice242-capture/race480p-10000.gscap`, `--gpu-timing`; the
timestamps add a little), with candidate 272's code:

| Scale | Busy | 1x pass | Scaled pass | Refresh | Decode | CLUT | Swizzle | Upload | Presentation compute |
|---|---|---|---|---|---|---|---|---|---|
| 1x | 7.1 | 3.4 | - | - | 1.65 | 0.53 | 1.03 | 0.49 | 0.01 |
| 2x | 8.7 | 3.3 | 1.5 | 0.12 | 1.54 | 0.53 | 0.98 | 0.49 | 0.26 |
| 4x | 11.9 | 3.5 | 4.2 | 0.35 | 1.53 | 0.52 | 0.96 | 0.49 | 0.43 |
| 6x | 17.1 | 3.7 | 8.5 | 0.72 | 1.52 | 0.57 | 0.89 | 0.50 | 0.67 |

CPU in the game (headless, launcher settings, last 600-VBlank interval):

| Measure | Value |
|---|---|
| GS thread | 10.6-11.3 ms per frame at every scale (budget 6 ms) |
| Process | 27-28 core-ms |
| Render passes per frame | 344 at 1x, 403 at scale |
| Barriers per frame | about 1,640 (race), 1,990 (night) |

Raw results: `O:\gt4recomp-scratch\coregs2-measure\{before,after-in382}\results.json`.

### Where the costs are

- **Scaled pass (the largest GPU cost: 8.5 ms at 6x, half the frame).** It runs the exact uber pixel shader
  (`native/hw_gs/shaders/hw_pixel_uber.frag` + `hw_pixel_core.glsl`) for each of the s x s samples.
  - Colour and depth are read-modify-written in R32_UINT storage images (raw GS words), under fragment-shader
    interlock.
  - Textures are fetched with `imageLoad`; bilinear means 4 loads.
  - Experiment (not committed): dropping the interlock saved only 0.5-1 ms of the 8 ms. The cost is the per-sample
    shading and the storage read-modify-write, not the ordering. Only real attachments (fixed-function blend,
    early depth, compression) will cut it.
- **1x work: 3.4 ms pass + 1.5 decode + 0.5 CLUT + 1.0 swizzle + 0.5 upload, about 7 ms, mostly GT4's tone
  sequence.**
  - The tone sequence is spec 09.4 variant A; IN-379 describes it.
  - About 1,540 of the 1,928 batches per race frame and about 280 of the ~320 runs belong to it.
- **GS-thread CPU (about 11 ms):** mostly the per-batch front end, translate and execute of the same tone
  sequence.

### What was started on items 1 and 2 (nothing committed)

- **Item 1:** only the interlock experiment above.
- **Item 2:** the full analysis of the tone sequence, recorded in section 4.

## 3. Item 1: fast scaled raster (approved, presentation-only tolerance)

**Design**

- Give every scaled colour slot image a second view: create `scaledFrames[i]` with
  `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT` and `COLOR_ATTACHMENT` usage, with an `R8G8B8A8_UNORM` view next to the
  `R32_UINT` storage view.
  - For CT32/CT24 the packed word layout (R in the low byte) is exactly RGBA8 in memory, so both views see the same
    pixels.
  - CT16/CT16S targets keep the exact path.
- Give every scaled depth slot a `D32_SFLOAT` twin attachment, with a per-slot flag saying which representation is
  newer.
  - Converting r32ui -> D32 is a full-screen draw writing `gl_FragDepth = word / 2^32`.
  - Converting D32 -> r32ui is a compute pass that reads the depth as a sampled image.
  - Convert lazily, only when the other path needs that slot.
  - `refresh_scaled` for depth writes the r32ui image, so it marks it newer.
- **Fast pipeline:** a new simple fragment shader (colour from vertex/texture/fog with TFX, alpha test only as
  discard when AFAIL = KEEP) plus fixed-function blend and depth.
  - Use a hardware sampler on an RGBA8 view of the texel atlas, so bilinear is free.
  - Cache pipelines by (blend equation, depth test/write, colour write mask, topology).
- **Classification per draw** (CPU, in `hw_gs_writeback_gpu.cpp` where `ScaledDraw` entries are built):
  - Fast when all of these hold:
    - the frame format is CT32/CT24;
    - the blend maps to fixed function: Cs/Cd/0 with factors As, Ad (CT32), FIX as a constant, the common modes
      (Cs-Cd)*As+Cd, (Cs-0)*As+Cd, (Cs-0)*FIX+Cd, opaque;
    - no DATE, no PABE;
    - FBMSK is byte-aligned (use the colour write mask), or 0;
    - COLCLAMP = 1;
    - the depth format is Z24/Z32 or depth is unused;
    - AFAIL is KEEP, or alpha test is off.
  - Exact otherwise: ATE with FB_ONLY/ZB_ONLY/RGB_ONLY, DATE, PABE, wrap colour, Z16, FBMSK not byte-aligned,
    CT16 targets.
  - Dither: ignore it on the fast path (it is inside the tolerance).
- Keep `record_scaled_draws` grouping. Draws of one run that switch between fast and exact need a pass break and a
  representation sync.

**Files and functions**

- `native/hw_gs/writeback/hw_gs_writeback_gpu.cpp`:
  - `make_image`, plus a mutable variant for the scaled colour images;
  - `create_scaled_resources` (descriptor sets, framebuffers, new render pass);
  - `record_scaled_draws` (path split);
  - the two places that build `ScaledDraw` (run path near `kTimeScaled` in `flush_run`, chunk path in `Execute`);
  - `refresh_scaled`.
- `native/hw_gs/writeback/hw_gs_writeback_gpu.hpp`: stats `scaledDrawsFast` / `scaledDrawsExact`.
- New shaders in `native/hw_gs/shaders/` (register them in `CMakeLists.txt` and `hw_shader_blobs.hpp`).
- `native/hw_gs/hw_gs_stream_sink.cpp` and `writeback/hw_writeback_replay.cpp`: print the fast/exact split.

**Read first:** spec chapters 06 (upscaling, 06.3 native mode), 08 (blending), 03.4/03.5 (texturing); IN-371,
IN-372, IN-374, IN-376.

**A/B switch:** add `GT4_HW_SCALED_RASTER=fast|exact` (default fast after acceptance) and a replay flag
`--scaled-exact`.

**Tolerance rule.** Record it as a new IN note that names this relaxation and the owner approval of 2026-10-23.
Against the exact scaled path, on the same capture and scale:

- max per-channel error <= 8 on at least 99.9% of the presented pixels;
- presented-frame PSNR >= 40 dB;
- `--check-presented` still 0 low-detail frames on the race;
- the 06.3 fixtures (`hw_scale_exact`, `hw_scale_display_shadow`, `hw_scale_fullscreen_passes`,
  `hw_scale_interlaced_present`) keep running on the exact path, and get a fast-path variant with that tolerance;
- memory hashes and every 1x gate stay byte-identical. The fast path never touches 1x images.

**Fixtures to add:** `hw_scale_fast_raster` (alpha blend, additive, depth-tested overlapping triangles, a texture
with bilinear, at 2x and 4x: compare to the exact scaled path within the tolerance, memory identical), plus a
classification test that an exact-only state (DATE, PABE, ATE FB_ONLY) takes the exact path.

**Expected gain:** scaled pass 8.5 -> about 2-3 ms at 6x, about 4.2 -> 1-1.5 at 4x.

**Measure:** `--gpu-timing` replays at 1/2/4/6x (the `scaled` category), plus the game's `[hw] cost` and `[hw] gpu`
lines. Compare with gt4-gs only as a black box: power and GPU load, final frames only.

## 4. Item 2: tone-sequence HLE at 1x (approved, memory-exact)

### The sequence (Confirmed on the race capture; replay `--trace-frame 10050` with all vertices)

Per CT32 page k (140 pages of the 640 x 448 frame at BP 6720, FBW 10) there are 11 batches.

**Three temp batches**, all with:

- FRAME: BP 13600, FBW 10, CT32, FBMSK 0xFF000000 (a 64 x 32 temporary page);
- TEX0: TBP0 = 6720 + 32k, TBW 2, T8, TW 7, TH 6, TCC 1, TFX modulate, CBP 13632 (the ramp CLUT), CPSM CT32, CSM 0,
  CSA 0, CLD 5;
- TEX1 nearest, TEST 0x31002, ZBUF masked;
- 16 sprites of 8 x 16 px (8 across, 2 rows), FST UV.

| Channel | Vertex colour | ABE | ALPHA | CLAMP | Sprite j UV (texels) |
|---|---|---|---|---|---|
| R | (128,0,0,0) | off | - | REPEAT | u = 16*(j/2) .. +8, v = j%2 .. +64 |
| B | (0,0,128,0) | on | 0x8000000068 (Cs + Cd, FIX 0x80) | REPEAT | u + 8 |
| G | (0,128,0,0) | on | 0x8000000068 | 0x3f73 (WMS region repeat, MINU 1015) | u + 4, v + 2 |

**Eight strip batches** i = 0..7, each one sprite of 8 x 32 px:

- FRAME: BP 6720, FBW 10, CT24, FBMSK 0xFF000000, rect x = page_x + 8i;
- TEX0: TBP0 13636, TBW 1, T8, TW 3, TH 5, TFX decal, CPSM CT32, CSM 0, CLD 5, with CBP = 13600 + {0, 4, 16, 20, 8,
  12, 24, 28}[i] (the 16 x 16 quadrants of the temporary page used as a CLUT);
- UV (0,0)-(8,32), one texel per pixel.

**Net effect:**

- Each frame pixel gets RGB' = (ramp[R].r, ramp[G].g, ramp[B].b), alpha unchanged (spec 09.4 variant A).
- The temporary page ends holding the last page's intermediate.
- The CLUT ends as loaded by the last strip.

### Design (exact by construction)

1. **Strip executor.** This removes 8 of the 11 batches per page; it is the safe first step.
   - A strip is a decal T8 sprite with one texel per pixel, so its exact effect is
     `dest(x, y).rgb = CLUT_i[index(x % 8, y)]`.
   - The index comes from the T8 texture at 13636; `CLUT_i` is the temporary page quadrant at CBP_i (CSM1 layout,
     as in `hw_glm_clut.comp`).
   - Defer the eight strips of page k and run one compute dispatch for them:
     - the dispatch reads the temporary page and the index texture from the GLM, and writes page k as CT24 with
       `glm_write`, which leaves alpha untouched;
     - flush it before the next temp R batch (which overwrites the temporary page) and before any other event
       (non-matching batch, upload, local-to-local move, readback, VBlank, compose, GS reset, auxiliary frame,
       VRAM clear);
     - before the dispatch, `CoherentForWrite(page span, nullptr, true)`; after it, mark the pages GLM-owned and
       call `MarkWritten`.
   - Leave the strips' CLUT loads to the normal path at first (exact). Later, suppress them and replay only the last
     one before any consumer.
2. **Temp batches.** Keep them on the normal path at first (exact). The generic sampling positions (pixel-centre
   rules, v steps of 4 texels selecting lanes) are not worth guessing.
   - If you HLE them later, use a deferral log: record each batch and each CLUT load in order (copies of `Batch`
     and `clut::Tex0Write`).
   - When a page step completes and matches the template, commit it with the kernel, keeping the last step's three
     temp batches to replay at the flush (they produce the exact final temporary page).
   - When the pattern breaks mid-step, replay the log in order through the normal path (exact).
3. **Matching.** Match on raw `Batch` state before `Translate`:
   - registers as listed above;
   - exact vertex arrays generated from the template;
   - DTHE off, FBA 0, PABE 0, FGE off.
   - Any difference means the normal path.
4. **Interaction with IN-379/IN-382.**
   - The HLE must run at 1x as well: today `TrackToneRun` returns early at scale 1, so decouple the HLE from the
     scale.
   - Flush the HLE inside `FinishToneRun` before the carry, while the scaled work is still paused.

**Files and functions**

- `native/hw_gs/writeback/hw_gs_writeback.cpp`:
  - `Renderer::Impl::OnBatch` (hook before `Translate`);
  - `TrackToneRun` / `FinishToneRun`;
  - `OnClutLoaded` (split into record and load);
  - `InvalidateDestination`, `CommitUpload`, `LocalToLocalMove`, `BeginLocalToHost`, `Renderer::Flush`,
    `ComposeFrameGpu`, `ResetGs`, `BeginAuxiliaryFrame`, `ClearVram` (flush points).
- A new GPU entry in `hw_gs_writeback_gpu.cpp/.hpp` and a new shader using `hw_glm_common.glsl`
  (`glm_read`, `glm_write`, `glm_slot_rows`).

**Read first:** spec 09 (09.1 reinterpretation maps, 09.3 channel shuffles, 09.4 GT4 patterns: policy 2 allows this
HLE), 07 (CLUT, CLD modes), 04.9 (GLM); IN-347..IN-351, IN-370, IN-379, IN-382.

**A/B switch:**

- `GT4_HW_TONE_HLE=0|1` and replay `--no-tone-hle`.
- `GT4_HW_VERIFY_SHUFFLE=1` (the option already exists in `hw_gs_options`): execute both the HLE and the batches,
  and compare the written pages (download, or a GPU compare) and the final CLUT; count mismatches.

**Fixtures to add:**

- `hw_tone_hle_exact`: a synthetic capture with two pages of the template, run with and without the HLE. Compare
  per-frame memory hashes and the CLUT buffer; it must fail if one strip quadrant offset is changed.
- Run the memory gates with the HLE on and off.

**Expected gain:**

- Strips alone: about 1,120 of the 1,540 tone batches per frame and most of the 280 runs. GS thread about 11 ->
  6-7 ms; GPU 1x pass, CLUT and barriers about -1.5 to -2 ms.
- With the temp batches as well: GS thread about 4-5 ms.

**Measure:** the game `[hw] cost` lines (`gs_thread_cpu_ms`, `render_passes`, `barriers`) and the replay
`--gpu-timing` (`pass`, `decode`, `clut`).

## 5. Item 3: fewer render passes and barriers

- **Design:** after item 2, about 40 runs per frame remain.
  - Runs end because texture preparation writes the shared texel atlas (IN-352) and CLUT loads need barriers.
  - Use a ring of atlas regions, so consecutive runs that prepare different textures can stay in one render pass.
  - Use `full_barrier` elision (IN-368) more aggressively.
- **Files:** `hw_gs_writeback_gpu.cpp`: `flush_run`, the atlas allocation, `full_barrier*`.
- **Gain:** about 0.5-1 ms GPU and fewer CPU submissions.
- **Measure:** the `render_passes` and `barriers` counters plus `--gpu-timing`.

## 6. Item 4: cheaper refreshes

- **Design:** `refresh_scaled` (0.72 ms at 6x) refreshes every loaded rectangle.
  - Skip refreshing scaled images that no presentation, scaled texture copy or scaled draw will read before the
    next full overwrite. For example, a slot whose next use is a full-rectangle opaque draw, and depth slots with
    no scaled depth test before the next clear.
  - The fast path of item 1 can also refresh with a blit-like draw.
- **Gain:** about 0.3-0.5 ms at 6x.
- **Measure:** the `refresh` category.

## 7. Build, gates, timing

- **Build** (VS x64 env inside the script; the release recipe):

  ```
  build\slice248-coregs2\build.bat <build dir> aspect slice262-frame-cost-runtime slice262-license-guard-runtime
  ```

  - The tree is `O:\gt4recomp-scratch\coregs2-slice270-present\build`. Use a new folder for a fresh tree.
  - `build/` is git-ignored but present in the worktree.
- **Gates:**

  ```
  sh build/gates2/gates.sh <tag> '<build dir>' [noctest nohash nodefault nopresented]
  ```

  It runs:
  - CTest in both interlock modes;
  - memory hashes on five captures (menu, race, car, night, race480p);
  - `--check-presented` at 2/4/6x;
  - menu dumps vs slice228, race frames vs slice235, default renderer vs slice214.

  It refuses to launch the game while any game session runs, so retry later.
- **`--check-presented` (replay):**
  - It counts presented frames from 1x and low-detail frames: mixed s x s blocks below 0.25 x the frame's 1x edge
    pixels.
  - `--no-gpu-compose` is the A/B for 480i frames; `GT4_REPLAY_DETAIL_TRACE=1` prints it per frame.
- **Timing rule:** never measure while a game, the GS process or a build runs. Check
  `Get-Process gt4vulkan,gt4vulkan-spec2,gt4-gs,cmake,ninja,clang-cl,cargo,rustc,hw_writeback_replay` before and
  after each run, and repeat any run that overlapped.
  - Another agent (OpenCode, Spec II) builds and runs headless games on this machine for long stretches.
  - A reusable waiter is in the previous writer's scratchpad. Rewrite it: before and after each run, poll
    `Get-Process` and redo the run if anything appeared.
- **Gotchas:**
  - `--slm-hashes` WRITES the file.
  - Bash heredocs and `sed` eat backslashes and turn `\n` into newlines: write Python edit scripts with a file
    tool, or edit directly.
  - `near` is a Windows macro.
  - `ctest -R "a|b"` breaks through `ctest.bat` (cmd pipe): call `"C:/Program Files/CMake/bin/ctest.exe"`
    directly.
  - numpy and PIL are only in `C:\Python314`, not in the tooling venv.
  - Never rebuild a tree while a gate or a measurement uses it.

## 8. Clean room and working rules

- Never open:
  - PCSX2 sources, the `gt4recomp-gs` fork, ARMSX2;
  - `private/tooling/pcsx2-*`, `private/slice189-pcsx2-ref`, `O:\gt4recomp-scratch\slice265-*`;
  - gt4-gs dumps or logs.
  `docs/hw-renderer-spec` is the only design source; gt4-gs.exe final frames, power and GPU load are allowed as a
  black box.
- Never touch the owner's cards (`private/manual-play/card.ps2`) or the main checkout `C:\Antigravity\gt4-staticrecomp`.
- Headless only: the owner runs the visible tests.
- Single writer on `task/027-coregs2` (worktree `C:\Antigravity\gt4-wt-027-coregs2`).
- Commit as `git -c user.email=57041838+moesuito@users.noreply.github.com -c user.name="João Alano" commit`, push the
  branch. No PR, no merge into main, no pins, no launcher mode.
- Update PROGRESS, IMPLEMENTATION-NOTES (a new IN note per change), STATUS, TASKS, the journal and ai-memory with
  every step.

## 9. Owner test 10 (deliverable)

- **Deliver:**
  - a new frozen candidate in a new folder `O:\gt4recomp-scratch\coregs2-candidate-NNN`, with its sha256;
  - `build\hwr-play-scale.ps1` pointed at it (`$Candidate` default and the run folder);
  - the measurement table after items 1 and 2 at 1/2/4/6x (GPU busy per category, GS thread, process, render passes,
    barriers);
  - the remaining gap to gt4-gs.
- **Owner commands:**

  ```
  & "C:\Antigravity\gt4-wt-027-coregs2\build\hwr-play-scale.ps1" -Scale 1
  & "C:\Antigravity\gt4-wt-027-coregs2\build\hwr-play-scale.ps1" -Scale 6 -AA
  ```

  Compare with gt4-gs (GSdx) at 6x in the launcher on the same scene (Fuji Test Course or Nordschleife TT, 480p):
  GPU load, clock and power in RivaTuner, and the picture.

## 10. Open items from the scene sweep

- **480i races** (without the forced 480p): presented at 1x. The display reads 448 rows of a 224-row field buffer,
  so no scaled image covers the read rectangle. Seen on the old captures `race-9000`, `car-6600` and `night-10000`.
- **Car-select and track-select menus:** low detail and combing on the rotating 3D car (cause not confirmed).
- **Menu fades** (about 30 frames per transition) stay 1x. Hypothesis: spec 09.4 variant B alpha stashes, not
  carried at scale.
- **The intro logo movie** stays 1x. Hypothesis: IPU video uploaded as 1x pixels, so 1x by nature.
- **Scenes only the owner can check:** rain, rally/dirt, replays, photo mode, split screen, the in-car mirror view;
  also menus at scale and owner test 9 on candidate 272.
