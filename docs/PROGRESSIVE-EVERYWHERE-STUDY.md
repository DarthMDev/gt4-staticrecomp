# Progressive output everywhere: study for TASK-078 P5 / TASK-080 P3 / CoreGS 2.0

Date: 2026-10-21. Read-only research. Clean-room status: **not PCSX2-tainted** (no PCSX2, gt4recomp-gs, ARMSX2,
`private/tooling/pcsx2-*` or `private/slice189-pcsx2-ref` opened). Sources: gt4-decomp at identical addresses
(`private/slice151-research/gt4-decomp`), the reconstructed ELF `private/reconstructed/SCUS_973.28.elf` (words read and
decoded by a small script; no external disassembler), `docs/ULTRAWIDE-NATIVE-STUDY.md`, `docs/INGAME-SETTINGS-STUDY.md`,
`docs/journal/2026-10-13.md`, `docs/hw-renderer-spec/`, ai-memory (`decisions/ultrawide-native-plan`,
`decisions/coregs2-implementation`). The SilentWarrior pnach (no licence) was read for ideas only; nothing copied.

Labels: [C] Confirmed (code/ELF word read), [H] High confidence, [Hy] Hypothesis, [U] Unknown.
"Words" are 32-bit units (GS VRAM is 1,048,576 words = 4 MiB). FBP unit = 2048 words.

---

## 0. Summary

- The game has **one display state object** at `0x006184D0` and a **CRT mode table** at `0x00618648`. Every screen asks
  for a mode through `func_001074D0(state, psm, row, w, h)`; the per-frame flip `func_00107878` → `func_00107368`
  rebuilds the frame buffers and issues the CRT change. The **buffer layout follows the mode automatically**: 480i
  "field" rows get half-height 640x224 buffers, 480p and 480i "frame" rows get full 640x448 buffers [C].
- The owner's lead is right, and it is the same mechanism as the ultrawide study's P5: the 480p **"Try..." pop-up has
  no special code**. The Screen page script calls `MOption.applyRaceDisplayMode` (`func_00436640`, index 1 →
  `func_001074D0(&D_006184D0, 2, 6, 640, 448)`), shows the pop-up at 480p, and after the answer/timeout calls
  `MOption.applyMenuDisplayMode` (`func_00436740` → `(1, 0, 640, 448)`, row 0 = default row 1 = 480i field) [C].
  So **"keep the pop-up's mode" = make `func_00436740` request `(2, 6, 640, 448)`: two instruction words** [C words].
  That is VRAM-neutral (same 0x69000-word footprint and same FBPs 0x00/0x8C as the 480i menu layout) and is already
  exercised by the pop-up and by the developers' own debug switch (`func_00100A40` mode 4) [C].
- The pnach "Progressive scan in menus" patch is **not** progressive output: it zeroes the field-parity byte read by
  the viewport setter, so the half-height field image stops jumping; the picture is still 224 lines [C code].
- Progressive **everywhere** needs about **4 small patch groups** (menus, race "normal" index, CRT table row 2 for
  photo pause/after movies, boot init) plus a riskier fifth (movie/photo-develop buffers). All are ELF patches before
  translation (producer). The game then runs at 480p from the first frame, and menu↔race switches stop causing the
  game's 1-second black (the 60-frame CRT-change countdown) [C code; visual effect H].
- CoreGS 2.0 can then **drop its deinterlacers and field-phase compose from the main path** (most of M15 and part of
  13.3/13.4), and every screen renders a full 448-line frame (much better upscaling of menus). Interlace handling
  **shrinks to a small fallback; it does not disappear** (vanilla/faithful mode, unpatched screens, M6 parity
  references, the odd-field defect is a raster bug that progressive only hides).

---

## 1. What the pnach "Deinterlace menus" patch does

`patch=1,EE,004A2A2C,word,0000102D` (comment `80A202C0`).

| Address | Before | After | Function | Meaning |
|---|---|---|---|---|
| 0x004A2A2C | `80A202C0` `lb v0,0x2C0(a1)` with `a1 = 0x70002000` (scratchpad) | `0000102D` `daddu v0,zero,zero` (v0 = 0) | `func_004A2A10(type)` (decomp `src/network/unit_00494578/func_004A2A10.cpp`) | the viewport setter's "field parity" byte is forced to 0 [C] |

How the byte is produced and used [C unless marked]:

- `func_004A22C0` (VSync wait, called every frame) writes `spr+0x2C0 = spr+0x64` (a per-VBlank value, the field
  parity [H]) **only when `spr+0xE9` == 1**, else 0. `spr+0xE9` is the "interlace" argument stored by
  `func_004A0890(interlace, mode, ffmd)` (the game's SetGsCrt front end). So in **progressive modes the game itself
  already keeps the byte at 0**; the pnach makes 480i behave like 480p *for this one reader*.
- `func_004A2A10(type)` sets the normalised viewport through `func_004A2B58(x, y, 1, h)`: `type 0/5-8` → full frame
  (`y 0, h 1`); `type 1` → half height (`h 0.5`) with `y = 0` or `0.5` depending on the parity byte; `type 2` the
  opposite parity; `type 9` field-pair variant. `type` comes from the draw buffer's flag (`func_001054A0`; 1 for the
  640x224 field buffers, 0 for full frames), e.g. `func_001056A0` → `func_004A2A10(buffer+0x28)`.
- So in 480i menus the game **renders each field separately**: the 448-unit UI layout is drawn at half height into a
  640x224 CT24 buffer, shifted by half a line on alternate fields (true field rendering, 60 distinct images/s). The
  pnach removes the alternating half-line shift; with an emulator showing one field line-doubled, the jitter vanishes
  but the image keeps **224 lines of real resolution** [H].
- Not covered by the pnach (still read the parity byte): the frame-blend/flicker passes in `func_004A3998` (cases 8/9:
  extra passes offset ±16 = ±1 pixel in 12.4, alpha 0.5) and `func_004A7C60` (selects 3/4/9 by parity) [C].

Verdict: confirms the previous study (row 1 of the ultrawide map) and adds the producer of the byte: the patch is a
"stable field" cosmetic, not progressive output.

---

## 2. How the game chooses its video mode

### 2.1 CRT mode table `D_00618648` (8 rows + fallback, `{id, interlace, mode, ffmd}`) [C, ELF words]

| Row id | interlace | mode | ffmd | Meaning | Used by |
|---|---|---|---|---|---|
| 1 | 1 | 0x02 NTSC | 1 | 480i **field** (half-height buffers) | default row (`D_0081EB00`), menus |
| 2 | 1 | 0x02 NTSC | 0 | 480i **frame** (full 448-line buffer read every other line) | photo pause, after movies |
| 3 / 4 | 1 | 0x03 PAL | 1 / 0 | PAL field / frame | not on this disc |
| 6 | 0 | 0x50 | 1 | **480p** | race index 1, the Try pop-up |
| 8 | 0 | 0x52 | 1 | (DTV 0x52) | no caller found |
| 7 | 1 | 0x51 | 1 | 1080i | race index 2 |
| 5 | 0 | 0x00 | 1 | [U] | no caller found |
| fallback (0x6186C8) | 1 | 0x02 | 1 | 480i field | unknown id |

- `func_00106058(id)` looks a row up; `func_001060A0(i, m, f)` does the reverse.
- `D_0081EB00` (default row) is computed once by a static constructor: `0x0010763C..0x0010764C` calls
  `func_001060A0(1, 2, 1)` and stores the result (row 1) [C].

### 2.2 The request and its consumer

- `func_001074D0(state, psm, row, w, h)` (`src/startup/unit_00101400/func_001074D0.cpp`) [C]:
  - `row 0` → `D_0081EB00` (row 1);
  - if `(psm, row, w, h)` differs from the last request: `f0/f4/f8 = interlace/mode/ffmd`, `pending f24 = 1`;
  - **blank countdown `f28 = 0x3C` (60 frames) when the CRT mode changes, else 3**.
- Consumer: the flip `func_00107878` → `func_00107368(state)` every frame [C]. It closes the open question of the
  previous studies ("who consumes the pending request"):
  - stage 1: rebuild the buffers by mode:
    - NTSC/PAL with ffmd = 1 → `func_001063F0`, 640x224 field layout (or `func_00106748`, 512x224, when w = 512);
    - NTSC/PAL ffmd = 0 and **0x50** → `func_00106EE8(state, psm, w, h, 0)`, full w x h frames;
    - 0x51 → `func_00106EE8(..., h/2, 1)`;
  - stage 2: `func_004A0808(interlace, mode, ffmd)` (CRT change; display regs rebuilt by `func_004A0970`).
- `func_00107AD8(f28 > 0)` disables the display while the countdown runs. This is the 1-s black when 480p races start
  from 480i menus [C code; visual H].
- The game writes PMODE, SMODE2, DISPFB2, DISPLAY2 and BGCOLOR itself every frame (`func_004A0C10`); SMODE2 therefore
  always follows the requested row [C].

### 2.3 Buffer layouts (VRAM words; all allocations start with `func_004AA478`, which resets the whole VRAM pool) [C]

| Layout | Function | Display buffers | Total |
|---|---|---|---|
| 480i field (menus, boot) | `func_001063F0` | 2 x 640x224 **CT24** at 0x00000 and 0x46000 (FBP 0x00, 0x8C) | 0x69000 |
| 480p, psm 2 (race idx 1, Try pop-up) | `func_00106EE8(…, 2, 640, 448)` | 2 x 640x448 **CT16** at 0x00000 and 0x46000 (FBP 0x00, 0x8C) + Z16 entry | **0x69000** |
| 480i frame / 480p, psm 1 (photo pause, after movies) | `func_00106EE8(…, 1, 640, 448)` | 2 x 640x448 **CT24** | 0x8C000 |
| movie / photo develop | `func_00106A68(state, 2, 256, 384)` | 2 x 256x384 CT16; it sets the CRT directly with `func_001060F8(state, 1, 2, 0)` (480i frame) | 0x18000 |

The `psm` argument of `func_001074D0` (named "a1" in the earlier studies) is the colour format of the frame
buffers: 1 = CT24, 2 = CT16 [C].

### 2.4 Who asks for what

| Screen | Call | Mode today |
|---|---|---|
| Boot / logos / first frames | `func_00107730` → `func_001062E8(state, 1)` → `func_001063A8` (field layout + `func_001060F8(1, 2, 1)`), then `func_00107788` → `func_001060F8(state, 1, 2, 1)` | 480i field [C]. Our runtime has no BIOS screen (HLE) [H]. |
| Menus, garage/car viewer, loading screens | `func_00436740` (native `MOption.applyMenuDisplayMode` 0x00189A28; direct calls from `func_0010BE20`, `func_001103E0`, `func_00108040`, `func_0010CA50`, `func_001C8778`, `func_005CD298`) | `(1, 0)` → row 1, 480i field CT24 [C] |
| Race, replay, license tests, split screen | `func_00436640` (`applyRaceDisplayMode` 0x001899F0; `func_0010BE20`, `func_00110460`, `func_005CD278`) | index 0 `(1,0)` 480i field; **1 `(2,6)` 480p CT16**; 2 `(2,7,576,960)` 1080i [C]. Our host holds index 1 (`GT4_FORCE_480P`). |
| Split screen | `RaceSplitDisplayBase__initialize`: two 640x224 viewports (4:3) or two 320x448 (wide) inside the race frame | follows the race mode; the 224 rows are viewport halves, not fields [C] |
| Photo pause | `func_003C4928`, `func_003C5430` → `(1, 2, 640, 448)` | 480i **frame**, full CT24 640x448 [C] |
| Movie / "media" player | `func_001C89F0`: start `func_00106A68(2, 256, 384)`; end `(1, 2, 640, 448)` | 480i frame [C]. Which FMVs use it (intro, credits, replay "media") [U]. |
| Photo develop | `RacePhotoDevelop__virtual_15`: `func_00106A68`, then `func_001063A8` | 480i frame, then 480i field [C] |
| Debug menu | `func_00100A40` modes 1-5: 512 field, 640 field, `func_00106DA8(1, 2)`, **`(2, 0x50)` 480p CT16**, `(2, 0x51)` 1080i | developer switch [C] |

### 2.5 The 480p "Try..." pop-up (owner's lead) [C script names + C natives]

- Script `ScreenRoot` (retail `projects/option/option.adc`, see `docs/INGAME-SETTINGS-STUDY.md` 1.4), in `TryModeChange`:
  1. sets `option.race_display_mode`;
  2. calls `option.applyRaceDisplayMode()` → `func_00436640` → `(2, 6, 640, 448)`. The flip switches the CRT
     (after the 60-frame blank) and rebuilds 640x448 CT16 frames; the pop-up page renders at full height
     (journal 2026-10-13: SetGsCrt 0/0x50/1, 640x448 without halving);
  3. waits up to 15 s for the answer;
  4. calls `option.applyMenuDisplayMode()` → `func_00436740` → back to row 1.
- There is no timer or revert logic in native code; the 15-s timeout and the revert are in the script [H, script
  identifiers seen in the decoded adc; the bytecode was not traced].
- The menu renderer is mode-agnostic. Draw code takes the viewport type from the current buffer's flag
  (`func_004A2A10(buffer+0x28)`), and `mRenderContextPS2__virtual_62` scales by the current buffer's size/640 and
  size/480 [C]. So the same menu pages draw correctly at 480p; the pop-up proves it for one page [C].

---

## 3. Feasibility of "progressive everywhere"

### 3.1 Minimal patch set (ELF words before translation, producer patch table)

| # | Screens | Address: before → after | Effect |
|---|---|---|---|
| A | all menus, garage, loading, after the Try pop-up | 0x00436750: `24050001` → `24050002` (psm 2); 0x00436754: `0000302D` → `24060006` (row 6) | `func_00436740` asks exactly what the pop-up asks: 480p CT16, same VRAM footprint [C words; behaviour C by pop-up] |
| B | races with any saved index (replaces the host poke) | 0x0043665C: `8E2316B0` (`lw v1,0x16B0(s1)`) → `24030001` (`addiu v1,zero,1`) | race always takes the index-1 path; with A, menu and race requests are **identical**, so `func_001074D0` sees no change: no buffer rebuild and no 1-s black between menus and races [C] |
| C | photo pause, after movies | data 0x0061865C/60/64: `00000001 00000002 00000000` → `00000000 00000050 00000001` (row 2 becomes 480p) | the same CT24 640x448 buffers (both paths go to `func_00106EE8(…, 1, 640, 448, 0)`); only the CRT changes; photo saves read the same buffer [C code]. Row 2 has no other table user (`D_0081EB00` uses row 1) [C] |
| D | boot (first frame onward), photo-develop return, debug mode 2 | `func_001063A8` body: 0x001063B4 `0C0418FC` (`jal func_001063F0`) and following → `addiu a1,zero,2; addiu a2,zero,0x50; addiu a3,zero,-1; jal 0x00106DA8; addiu t0,zero,-1; ld ra/ jr ra` (≤ 10 words of its 18), i.e. call `func_00106DA8(state, 2, 0x50, -1, -1)` (the debug-menu 480p path) | boot buffers are 640x448 CT16 at 480p [C for the callee; patch Hy until built] |
| D' | boot (direct CRT write) | 0x001077A4: `24050001` → `24050000`; 0x001077B4: `24060002` → `24060050` | `func_00107788` calls `func_001060F8(state, 0, 0x50, 1)` instead of `(1, 2, 1)`; required with D, otherwise the CRT goes back to 480i field over full frames [C words] |
| E (risky, separate) | movies, photo develop | 0x00106AB8: `0000382D` → `24070001`; 0x00106AC0: `24050001` → `24050000`; 0x00106AC8: `24060002` → `24060050` | `func_00106A68` sets 480p instead of 480i frame. 256x384 buffers; their flag is 1 (field-type viewport) and the 0x50 DISPLAY maths in `func_004A0970` differ (centred, MAGH = 1280/dw) [U geometry] |

**Alternative to C (code instead of data):** change `(1, 2)` → `(1, 6)` in `func_003C4928`, `func_003C5430` and
`func_001C89F0` (6 words). The table patch is smaller.

Why slice158's runtime table rewrite "changed nothing" [H]:
- `D_0081EB00` is computed at static-init time;
- `func_001074D0` only reads the table when the request changes, so an in-place rewrite of an already-active row is
  never re-read.
ELF patches before boot avoid both.

**A + B + C + D + D'** = 9 code words + 3 data words. With them, the game outputs 480p from the first frame, except
the screens that use `func_00106A68` (patch E, or they stay 480i frame).

### 3.2 Owner's question: "pop-up path" vs "func_00436740 row" — which is smaller and safer?

They are the **same change**. The pop-up runs `func_00436640` index 1 = `func_001074D0(…, 2, 6, 640, 448)`, and patch A
makes `func_00436740` ask for that same tuple.

| Variant | Size | Safety |
|---|---|---|
| **A, psm 2, row 6 (recommended)** | 2 words | Identical to the pop-up state and the debug 480p switch; VRAM footprint and FBPs unchanged. Caveat: CT16 menus (16-bit colour, dithered), as races at 480p already are. |
| A', psm 1, row 6 (CT24 480p menus) | 1 word (0x00436754 only) | Better colour. Frame buffers take 0x8C000 words, 0x23000 words (560 KiB) less texture VRAM in menus. Precedent: photo pause and after-movie use that footprint, but in race/movie context [Hy for garage/car viewer]. Test second. |
| Script-only (option.adc: skip `applyMenuDisplayMode`) | needs an adc rewrite | Insufficient: `func_00436740` has 7 direct native callers (boot flow, loading, media). |
| Change the default row (constructor `func_001060A0(1,2,1)` → `(0,0x50,1)`) | 2 words | Gives CT24 480p (psm stays 1) for every row-0 request, including race index 0; no gain over A'. |

### 3.3 Risks

| Risk | Assessment |
|---|---|
| UI layouts built for 224/448 rows | Low. The UI is laid out in 640x448/480 units and halved by the field viewport (`h 0.5`); progressive draws it at full scale [C]. Two hard-coded halvings exist: `func_00218170` (`y*=0.5, h*=0.5`, used by `mColorWindow__virtual_92`, the colour-picker window) and `mRenderContextPS2__virtual_67` (halves when `self+0x1D9C == 0`). They may copy the wrong rectangle at 480p [Hy]; check those screens. |
| VRAM | A (CT16): none (same 0x69000 words, same FBPs) [C]. A' / C (CT24 640x448): 0x8C000 words, already used by photo pause [C]; for menus [Hy]. The pool is reset on each rebuild (`func_004AA478`), so textures are re-allocated after a change, as on the pop-up [H]. |
| FMV decoder output size | Movies decode into 256x384 CT16 buffers via `func_00106A68` [C]. 480p display geometry for that layout [U]. Keep E optional; at worst movies stay 480i frame (the movie image is a full frame anyway, FFMD = 0). |
| Flicker filter | The parity-dependent blend passes (`func_004A3998`, `func_004A7C60`) take the parity-0 branch at 480p, as in races [C]. |
| VBlank/timing | 480p = 59.94 Hz progressive, same VBlank count; races already run this way. `func_004A22C0` keeps the parity byte at 0 [C]. |
| Mode-switch blanks | Fewer: with A+B there is no CRT change between menu and race (3-frame rebuild at most; none if the tuple is equal) [C]. Photo pause (C) still rebuilds CT16→CT24 but without the 60-frame CRT blank [C]. |
| Photo save size | Unchanged: the same 640x448 CT24 buffer [C]. Photo develop (E) [U]. |
| Split screen | Unaffected: race-mode viewports [C]. |
| 1080i | B removes the index-2 path [C]; the launcher already hides it. |
| CT16 look in menus | Gradients/photos in menus may band (dither on/off [U]). Owner judgement; A' is the fallback. |
| Fidelity rule (AGENTS: "as faithful as possible") | Changes the game's original output mode → owner decision; keep it switchable (patch on/off). |

### 3.4 Headless verification per screen

1. **Register gate (automatic):** in every VBlank of a headless run, record SMODE1/SMODE2 from the game's
   privileged writes (they pass through `write_privileged` / the SetGsCrt shim `map_set_gs_crt`). Assert INT = 0 and
   the 480p mode from VBlank 0, DISPFB FBW 10, DISPLAY DH for 448 lines, and no `func_00106A68` mode except where E
   is off. Count CRT changes (should be ~0 per menu↔race).
2. **Image gate:** run the 42-dump schedule (slice214 reference = 480i) with the patch. Compare against the reference
   line-doubled field images (expect higher detail, same geometry; PSNR threshold, not identity). New 480p references
   become the baseline for CoreGS 1.1 and CoreGS 2.0.
3. **Scripted scenes (Computer Use or owner pass), one F7/F8 interval each:**
   - boot/title; main menu; garage + car viewer;
   - colour-picker window (`mColorWindow`); dealer;
   - license test start/result (`ResultLicense` uses `func_004A2A10`); Time Trial race;
   - replay; photo mode in race (pause, shoot, save) and Photo Travel/develop;
   - split screen 4:3 and 16:9; an FMV (intro/credits); the Screen-page Try pop-up.
4. **VRAM guard:** for A', log `func_004AA570` failures (returns < 0) in garage/car viewer [Hy instrumentation].

---

## 4. What CoreGS 2.0 could drop or simplify

With A-D (and ideally E) on by default:

| Spec item | Today | With progressive everywhere |
|---|---|---|
| 13.4 deinterlacers (weave/bob/blend/adaptive), M15 part (4 d total) | needed for menus (FFMD = 1 field rendering, `adaptive` default proposed, Q-13) | **drop from the main path**; M15 keeps post-fx only (~1-1.5 d saved, Hy) |
| 13.3 interlace offset, field phase from `g_odd_field`, history images | needed every menu frame | fallback only |
| 13.2 step 4 FFMD half-height read, NTSC base rows | main path | keep (tiny), fallback |
| 12.x scanout phase for interlaced output | main path | fallback |
| 14 scenes S-01/S-02/S-04 (480i, "CT24 field buffers", "line-doubling of field buffers") | 480i references | re-baseline as 480p scenes; one frame layout (640x448) in menus and races |
| 09 note "224-line CT24 field buffers" (TEX2 CLUT loads into field buffers) | menus | gone from menus with A (CT16 448) |
| Upscaling (06) | menus are 224-line fields with alternating half-line offsets → combing/softness at Nx | full 448-line frames → clean upscaled menus (a real quality gain, not only less work) [H] |
| CT16 frames | races at 480p already | also menus; an optional "render CT16 targets at 8-bit precision" enhancement becomes more valuable [Hy, design question] |

**Must keep anyway:**
- a minimal interlaced path (INT/FFMD read, simple line-double/bob) for the vanilla/faithful mode (patch off), any
  unpatched `func_00106A68` screen (movies/photo develop without E), and robustness against unknown rows;
- correct handling of the parity-dependent passes, which also run in 480p with parity 0.

**The M6 odd-field defect (IN-333/334):** every odd menu frame loses content through a VRAM write problem tied to
field parity [C, ai-memory `decisions/coregs2-implementation`].
- From the game side, odd fields differ by the half-line viewport offset (`func_004A2A10` type 1 with parity 1:
  `y = 0.5` in the normalised viewport → half-pixel Y offsets on every primitive) [C game code]. The defect is
  therefore most likely a fractional-Y raster/sprite-rule mismatch [Hy].
- Progressive (or the pnach's parity-0 trick) removes the trigger in menus, but 3D and HUD draws also use fractional
  coordinates, so **fix the bug anyway**.
- Cheap diagnostic for the CoreGS team: capture the same menu with patch A (or a parity-0 build). If odd frames then
  match the CPU reference, the bug is confirmed as the half-line offset path.

Honest bottom line: interlace handling **shrinks** (deinterlacers and field compose leave the main path, maybe 2-3
days of M15/M6 work and a whole class of field bugs), but it **cannot disappear** while a faithful 480i mode and
unpatched screens exist.

---

## 5. Recommended plan

| Step | What | Where | Effort |
|---|---|---|---|
| 1 | Producer patch table entries A + B (menus and races at 480p CT16, as the pop-up), switchable (`GT4_PROGRESSIVE=1` / pin) | TASK-078 P5 = TASK-080 P3, one slice | 0.5-1 slice |
| 2 | Register gate + 42-dump run; owner pass on menus/garage/colour window/license/race/replay/split | same task | 1 slice + owner |
| 3 | Add C (CRT row 2 → 480p: photo pause, after movies) and D + D' (boot) | same task | 0.5 slice + owner photo-mode pass |
| 4 | Experiment E (movies/photo develop) behind its own switch; keep only if geometry is right | same task, optional | 1 slice |
| 5 | Experiment A' (CT24 480p menus) vs A (CT16): owner picks by look, check VRAM failures | optional | 0.5 slice |
| 6 | CoreGS 2.0: fix the M6 odd-field defect first (it is a raster bug); then re-baseline S-01/S-02/S-04 at 480p, demote M15 deinterlacers to a fallback (`off` + bob), keep 13.2 FFMD read | TASK-027 plan update (spec 13/14/15 note), clean-room team | spec edit 0.5 d |
| 7 | Launcher: "Progressive output (recommended)" on by default; "Original 480i" for purists | TASK-078 | small |

**Owner decisions:**
- progressive by default (fidelity rule)?
- CT16 vs CT24 menus?
- are movies in scope (E)?

**Open items:**
- [U] which FMVs go through `func_001C89F0`;
- [U] the 480p DISPLAY geometry for 256x384 buffers;
- [Hy] the two hard-coded halvings (`func_00218170`, `mRenderContextPS2__virtual_67`);
- [U] meaning of CRT rows 5 and 8.
