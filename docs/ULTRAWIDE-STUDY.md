# Ultrawide and tall-phone aspect study (GT4Recomp, read-only)

Scope: change the aspect ratio on the guest side (game's own projection and HUD anchoring), so it works with every GS renderer. No checkout was modified, no build or game was run. Labels: **Confirmed** (read in source/ELF-derived decomp with file:line), **High confidence**, **Hypothesis**, **Unknown**.

Source caveat: the decomp at `private/slice151-research/gt4-decomp` is named from the pinned ELF (medium/high confidence per `config/gthd_names.txt`). Names and guest addresses are its claims, not ours, until checked against our translated code.

## Summary

- **Feasibility: good, but the work is mostly in the presentation and HUD, not one constant.** The game already has a 16:9 option (a global flag at guest `0x6184F0`). It drives a 4:3 to 16:9 aspect factor in one place and a ±96 px HUD offset in several. A 21:9 or 32:9 mode is a new value fed through the same paths, plus a race-camera aspect source that is **Unknown** (not yet located).
- **Pixel count does not grow.** Widening the frustum only shows more of the scene. The GS output stays 640 wide. Filling a 21:9 window without stretching needs a wider framebuffer, which our current path cannot provide. The honest first version is a horizontally stretched 640-wide image, as PS2 16:9 patches do.
- **Recommended PoC:** `GT4_ASPECT=21:9` (also 32:9 and 20:9). It needs (a) a located race-camera aspect value, (b) a presentation rect driven by the chosen aspect, (c) the same guest-side HUD offset the game uses for 16:9, and (d) a headless frame check that the default stays byte-identical.
- **Community patches:** I could not find a GT4 NTSC-U widescreen pnach. The PCSX2 patch listing is truncated at 1,000 files, so the SCUS-97328 entries were not visible (see section 1).

## 1. Community work (PCSX2 patches)

- **Confirmed:** `github.com/PCSX2/pcsx2_patches` (`patches/`) holds a directory of pnach files keyed by serial and CRC. The WebFetch of `https://github.com/PCSX2/pcsx2_patches/tree/main/patches` returned only the first 1,000 entries (alphabetically from `6D980D22.pnach` to `SCES-51428_29B5FDB9.pnach`), with 3,710 entries omitted. SCUS entries sort after SCES, so they were in the omitted part. **Unknown:** whether a GT4 SCUS-97328 entry exists. I did not verify the CRC `77E61C8A` either; it is an owner-supplied guess.
- **Confirmed:** the PCSX2 forum states that widescreen patches need a `[Widescreen 16:9]` header for auto-detection, and that since PCSX2 1.7.4546 widescreen and no-interlace patches go into the single `cheats` folder ([PCSX2 forum: Important Patching Notes 1.7.4546](https://forums.pcsx2.net/Thread-Important-Patching-Notes-1-7-4546-Pnach-2-0), [PCSX2 forum: How PNACH files work 2.0](https://forums.pcsx2.net/Thread-How-PNACH-files-work-2-0?page=1)). The search did not show what GT4 patches change.
- **Licensing:** pnach files in that repo are generally community-authored and of unclear license. Treat them as **reference only**. Re-derive from the ELF and decomp, do not copy. Record the source URL if a patch is used as a reference.
- **Next step for the owner (read-only, offline):** `git clone --depth 1 https://github.com/PCSX2/pcsx2_patches` and `ls patches | grep -i 97328`. This avoids the 1,000-file truncation.

## 2. Our ELF and decomp: where the aspect lives

### 2.1 The game's own 16:9 option

- **Confirmed:** `src/race/ResultLicense/func_003E13E8.cpp:19,31-34` (result/photo view):
  - `aspect = width/height`, then `fixAspect(aspect, *(int*)(g-0x1c))`, where `fixAspect` multiplies by `0x1.555554p+0` (= 4/3, approx 1.3333) when the flag is set.
  - It then calls `func_0049AC00(50.0f, aspect, 0.1f, 100.0f)` (fovy 50, near 0.1, far 100).
  - `g = &D_0061850C` (`Globals*`), so the flag is at guest **`0x0061850C - 0x1C = 0x006184F0`**. `D_006184F0` is named in `src/race/unit_003AECB0/RaceDisplayUtil__wideOffset.cpp:4`.
- **Confirmed:** `RaceDisplayUtil::wideOffset(int flags)` (`src/race/unit_003AECB0/RaceDisplayUtil__wideOffset.cpp:4-16`, guest `0x003AFDD8` per `config/gthd_names.txt:2719`): if `flags & 4`, returns 0 when `D_006184F0 == 0`, else ±96.0 (sign from `flags & 8`). Callers: `RaceCountDisplay__render_main.cpp:42`, `RaceGasMileageDisplay__render_main.cpp:59`, `RaceLapTimesDisplay__render_main.cpp:30`. **Confirmed:** this is the game's 16:9 HUD anchoring: edge-anchored elements move outward by 96 px.
- **Confirmed:** the flag is written from the options menu through `MOption__get_wide_mode` (`src/gamedata/unit_0017FCB0/MOption__get_wide_mode.cpp:44-54`, which calls `func_00437110` with a value from a vtable call) and `MSystem__GetStaticWideMode` (`src/gamedata/unit_001AC008/MSystem__GetStaticWideMode.cpp`). **Unknown:** the exact store to `0x6184F0`. A runtime read in the headless harness would confirm it.
- **Confirmed:** the presentation side already has a 16:9 switch: `native/gpu_tail.inc:84-87` (`set_widescreen`, log text "match GT4 Screen Ratio option") and `native/window.cpp:336` calls it when the window opens.

### 2.2 Projection functions

- **Confirmed:** `CameraBase::getProjectionMatrix(float*, float) const` at `0x0044F210` (`src/engine/CameraBase/CameraBase__getProjectionMatrix.cpp`; `include/gt4/CameraBase.h:23`). The body calls `func_004A53F8`, `func_004A7454`, the virtual `update(t)` (the repo's `docs/RENDER-CONTRACTS.md:107-110` says this slot is really `applyProjection` at vtable `0x10`), then `func_004A7550`, which copies VU0 vf28-vf31 (a VU0 result, so it is not pure host math). Modelview is `0x0044F280`.
- **Confirmed:** `RaceDisplay::setProjectionMatrix(f32 l, f32 r, f32 t, f32 b)` at `0x0039BB38` (`src/race/RaceDisplay/RaceDisplay__setProjectionMatrix.cpp`) builds an **orthographic** matrix in the object at `+0x70`: `m0 = 2/(r-l)`, `m14 = 2/(t-b)`, `m30 = (l+r)/(l-r)`, `m34 = (b+t)/(b-t)`, `m28 = -1`, `m3C = 1`. It is the 2D/HUD projection, not the 3D race perspective.
- **Confirmed:** split-screen setup (`src/race/RaceSplitDisplay/RaceSplitDisplayBase__initialize.cpp:31-46`). Two branches: one calls `setProjectionMatrix(o, -106.666664, 320, 480, 0)` with size 320x448 (`0x140 x 0x1C0`); the other calls `setProjectionMatrix(o, 0, 640, f+240, f)` with `f = -10` or `0`, size 640x224 (`0x280 x 0xE0`). The pixel-space ortho frame is therefore 640x480 (or 640x448 for the HUD).
- **Confirmed:** `func_0049AC00(fovy, aspect, near, far)` (`src/network/unit_00494578/func_0049AC00.c`) builds a perspective frustum. The half-angle uses `0x1.1df46a0000000p-7` (= pi/360, degrees to radians), and it calls `func_004A59B8(-top*aspect, top*aspect, -top, top, near, far, near)`. This is the only aspect-driven perspective in the decomp that I could confirm. Callers: `ResultLicense/func_003E13E8.cpp:34` and `unit_004271E8/func_00427E90.cpp:49`. **Race camera not confirmed.**
- **Confirmed:** `func_00397DA0` (`src/race/unit_00394D48/func_00397DA0.cpp`) reads `Cam{pos, fov, aspect}` and calls `func_004A40B0(fov*scale, aspect)`. Caveat: `func_004A40B0` (`src/network/unit_00494578/func_004A40B0.cpp`) writes a sprite-like struct (`spr_base()`, fields `u`, `v`, `alpha`). The name is wrong or the function is not a projection setter, so this path is **Unknown** for the race projection.
- **Confirmed:** photo mode passes `1.3333332538604736` as aspect (`src/race/PauseBase_PhotoPause/func_003C5430.cpp:61`, `cam->v25(1.3333, 1.0)`), and `RacePhotoModeCameraManager__virtual_09.cpp:10` uses `0x1.555554p+0` (= 4/3) in a similar call.
- **Hypothesis:** the race camera's aspect is a 4:3 constant (1.3333) inside the camera object, set once at race init, and the 16:9 option multiplies it by 4/3 the same way `func_003E13E8` does. **Unknown until we locate it.** Good candidates: (a) read the ELF `.rodata` for the float bit patterns `0x3FAAAAAB` / `0x3FAAAAAA` (1.3333) and trace references to the camera construction; (b) trace what calls `CameraBase::applyProjection` in our translated code, at the vtable `+0x10` slot.

### 2.3 What our recompiled code allows

- **Confirmed:** `native/race_display.hpp:1-25` and `race_display.cpp` already write guest RAM at VBlank (`ps2::g_ram`, `kRamSize`, through `window.cpp:194-195`), for the 480p race display index at `0x16B0` under owner pointer `0x622F4C`. The pattern is in place.
- Two ways to change projection, each with trade-offs:
  - **(A) Guest RAM write at VBlank.** Works if the aspect or flag is a guest RAM float read per frame. Cheap, no translated code touched, consistent with the existing 480p path. Needs the camera object's address (**Unknown**, which the owner-pointer chain must reach) and a fixed per-frame re-write.
  - **(B) Hook a translated function.** Patch the translated `func_0049AC00` or `func_00397DA0` (the aspect argument) or `RaceDisplay::setProjectionMatrix` (`l, r` for the 2D HUD). Needs a new entry in the unit translation or a runtime override, and it changes translated code. Clean for the HUD path (`l, r` are plain arguments), but the race camera path is still **Unknown**.
- **Recommendation:** use (A) for the first proof and (B) for the HUD `l, r`, once the camera address is known.

## 3. Side effects and fixes

- **Culling and geometry at the new edges:** **Unknown.** The decomp shows no explicit frustum cull in C. The culling is most likely in VU1 microcode (`docs/VU1-MICROCODE-SOURCE.md`) or in EE-side visibility; not checked. Expect objects to pop at the widened edges. The fix is to widen the cull frustum by the same factor, which again needs the cull constant.
- **LOD:** **Unknown**, not studied. LOD distance is likely independent of aspect, so it should not need changes.
- **Shadows, reflections, mirrors:** the viewport sizes are from `RaceSplitDisplayBase__initialize` (640x224 / 320x448). Any viewport based on 4:3 would need the same factor. **Unknown** for the mirror path.
- **Lens flare and sun position:** the sun is projected through the same matrix, so it moves with the widened frustum. **Hypothesis**, not checked.
- **2D HUD and menus:** `RaceDisplayUtil::wideOffset` (±96 px for edge-anchored flagged elements) is the game's own answer. A 21:9 mode should reuse it with the new anchor distance, not stretch the HUD. Menus: **Unknown** (not traced).
- **Replay and photo mode:** `func_003E13E8` and `PhotoPause` use the 4/3 factor. A 21:9 value goes through the same `fixAspect` slot.
- **Split screen:** the wide branch of `RaceSplitDisplayBase__initialize` (320 px wide halves) would need its own pixel width for ultrawide.

## 4. Presentation side in our runtime

- **Confirmed:** `native/window.cpp:249-250` fixes the client area at **960x540**, and `window.cpp:335-336` says "The presentation is fixed 16:9." `native/native_vulkan.cpp:28-40` `presentation_rect(width, height, aspect)` letterboxes to 16:9 (or 4:3) with `fitted = min(...)`. It is not stretched.
- **Confirmed:** `native_vulkan.cpp:962-974` blits the guest frame into that rect with `VK_FILTER_NEAREST`. Nearest-neighbor scaling into a non-integer rect shows uneven pixels. For a stretch this is the wrong filter (use linear, or an integer-plus-stretch path).
- **Confirmed:** `native/gpu_tail.inc:84` `set_widescreen(bool)` only toggles 4:3 or 16:9. The enum `Aspect` (`native_vulkan.cpp`) needs a numeric ratio (e.g. `21/9`, `32/9`, `20/9`) to support new targets.
- **Frame-size note:** the GS output is the game's own 640x448 or 640x480. Our GS display source (`source.width`, `source.height`) is the guest's real size, so widening the FOV does not add horizontal pixels. At 21:9 on a 3440x1440 monitor, showing a 640x480 image stretched to 21:9 (about 1.31x horizontal) is the anamorphic approach. PS2 widescreen patches are essentially the same: a wider frustum is shown on a wide display, and the picture is stretched horizontally. That is **Hypothesis** for GT4 but matches what the patch format implies.
- **To go beyond stretching** (a sharper 21:9 image): a wider internal framebuffer. Only possible if the game draws to a wider target, which it does not. A native renderer or paraLLEl-GS with custom projection (TASK-012/026/027) is the path for that. Out of scope for the guest-side PoC.

## 5. Plan

### PoC (smallest proof)

1. Read-only, no build: find the race camera aspect. Scan the ELF for `0x3FAAAAAB` (1.3333), trace references from camera setup and from `applyProjection` (vtable `+0x10`). Record the address and the 4/3 constant. Owner-side: nothing needed.
2. Add `GT4_ASPECT` (values `4:3` default, `16:9`, `21:9`, `32:9`, `20:9`), parsed like `GT4_FORCE_480P` (`race_display.cpp` `parse_*` pattern, invalid values throw).
3. Guest-side: at VBlank, write the camera aspect float (factor = target / 4:3) and the HUD `wideOffset` anchor. Keep the 16:9 path identical to the game's own option (`D_006184F0`).
4. Presentation: `presentation_rect` gets the numeric ratio. For the PoC, a stretched 640-wide image fills the window (no letterbox), with linear filtering.

### Tests

- **Default identical:** headless frame check, `GT4_ASPECT` unset, same frame hashes as the pinned baseline (the 480p check in `docs/BUILD.md` slice156 is the template: 42 frame dumps identical). Also `16:9` with the game's own flag set should match the native 16:9 output.
- **CTest:** pure functions for parse and factor math (like `race_display` fixtures), plus a presentation_rect test for 21:9 and 32:9 (the tests in `native/fixture.cpp:75-83` are the pattern).
- **Owner checks:** Time Trial, Nordschleife, Supra Wood One, 480p (per CLAUDE.md scene): HUD anchoring at the edges, objects popping at new edges, menus, and one replay.

### Effort (rough, my estimate)

- Locate camera aspect and cull constants: 1-3 days of read-only ELF/decomp work.
- PoC (env var, camera write, presentation ratio): 3-5 days including tests.
- HUD anchor policy and split screen: 1 week.
- Cull and shadow/mirror viewports: 1-2 weeks, depends on the VU1 cull path.
- Menus: 1 week.

### Risks

- The race camera aspect may be computed, not a constant, so a single float write may be wrong.
- The culling in VU1 microcode is not interpreted in C; widening may show holes.
- Stretching looks wrong on 21:9 without a wider framebuffer; the owner must accept it.
- Phones (19.5:9, 20:9) are tall: the presentation must be portrait, which is a larger change.

### Launcher fit

- Expose `display.aspect` in the launcher (TASK-020) as a value like `16:9`, `21:9`, `32:9`, `20:9`, writing `GT4_ASPECT` to the child process. The launcher must not change the card (CLAUDE.md, P5).

## 6. Unknown / to do

- Race camera aspect source (the most important unknown). Also whether `func_004A40B0` is really a projection setter.
- Cull constants and where culling happens (VU1 or EE).
- Writer of `D_006184F0` (the 16:9 flag) in the options menu path.
- Whether any community GT4 widescreen patch exists (need the offline clone to see the SCUS files).

## Sources

- [PCSX2 patches repo](https://github.com/PCSX2/pcsx2_patches/tree/main/patches) (truncated listing, read 2026-10-15)
- [PCSX2 forum: Important Patching Notes 1.7.4546](https://forums.pcsx2.net/Thread-Important-Patching-Notes-1-7-4546-Pnach-2-0)
- [PCSX2 forum: How PNACH files work 2.0](https://forums.pcsx2.net/Thread-How-PNACH-files-work-2-0?page=1)

Local files read (no changes): `docs/NATIVE-RENDERER-STUDY.md`, `docs/NATIVE-PORT-PLAN.md` (P5, lines 266-282), `docs/RENDER-CONTRACTS.md` (lines 95-125), `docs/BUILD.md` (slice156 check, lines 430-450), `native/race_display.hpp`, `native/window.cpp`, `native/native_vulkan.cpp`, `native/gpu_tail.inc`, `native/fixture.cpp`, and the decomp files cited above.
