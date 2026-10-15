# 06 — Upscaling (internal resolution)

HWR renders targets at an integer **scale** s (`GT4_HW_SCALE`, 1..8, default 1).
At s = 1 every rule in this chapter must be a no-op (acceptance: the 1x
comparisons of chapter 14 do not change when this chapter's code is enabled).

## 6.1 The multiplier (Decision, based on Confirmed behaviour)

- Integer scales only in phase 1 (fractional scales complicate box filtering
  and sprite fixes; the studied reference falls back to nearest stretches for
  them). Values outside 1..8 stop the process (chapter 12 option rule).
- Device ceiling: s_max = max(1, floor(maxImageDimension2D / 1280)) (1280 GS
  pixels is the largest practical target side). A requested scale above s_max
  is clamped with a log line. Vectors: 16384 → 12 (then capped at 8 by the
  option range), 8192 → 6, 2048 → 1.
- Per target, the image is ceil(unscaled·s) per axis; if that exceeds the device
  limit for an unusually large target, that target is created at the largest
  integer scale that fits and flagged `downscaled` (counter).
- Changing the scale at run time is not supported in phase 1 (restart).
- Memory: a 640x448 target costs 1.1 MiB at 1x, 4.4 MiB at 2x, 9.8 MiB at 3x,
  17.5 MiB at 4x, 39 MiB at 6x, per colour or depth image.

## 6.2 Which targets are scaled (Confirmed behaviour, adopted)

- Default: every new colour and depth target uses s.
- Exceptions that use scale 1 (flag `downscaled`):
  1. Palette construction: no depth buffer, the source is a target already at
     1x, and the source format is T8 or the source is the frame itself — such
     intermediates are reinterpreted as indices and must not be interpolated.
  2. Targets created by native post-effect classification (6.6) when enabled.
- A channel-shuffle draw renders at its source target's scale; a 1:1 "scaled
  copy" (position range equals UV range, into a small target ≤ half the display
  width, or a shuffle) keeps the source's scale.
- A lookup that finds a target at another scale resamples it (chapter 05.3):
  to 1x with the box filter (6.7), otherwise a stretch (bilinear for colour,
  nearest for depth).

## 6.3 Position mapping and half-pixel modes

Option `GT4_HW_HALF_PIXEL=native|normal|off`, default `native` (GT4's engine
prefers native alignment: it removes edge shadows and depth bleed at screen
edges; High, from the GT4 entry of the studied reference's game database).

Let P be the GS position in native pixels ((X − OFX)/16) and W the target's
unscaled width. Using the chapter 03.3 mapping `gpu_x = s·P + shift − ε`:

| Mode | shift (scaled pixels) | Effect |
|---|---|---|
| off | 0.5 | GS integer coordinate lands on the centre of the first sub-pixel of the native pixel; geometry slightly up-left of native alignment |
| normal | 0.5·m, m = s + 0.2 (s even), s + 0.3 (s odd with bit 1 of s clear, e.g. 1, 5), s + 0.1 (otherwise, e.g. 3, 7) | about half a native pixel; aligns blur/bloom passes in many games, a few bad edge pixels |
| native | 0.5·s | geometry snapped to the native grid: every native pixel is covered by a uniform s x s block; the top row and left column of sub-pixels of a primitive starting exactly on a native edge stay undrawn |

Vectors (s = 3, W = 640): off → +0.5 scaled pixel; normal → +1.55; native →
+1.5. m values: s = 2 → 2.2, 3 → 3.1, 4 → 4.2, 5 → 5.3.

Native-mode compensations (Confirmed behaviour, adopted):

- Region-clamp margins when the source is a scaled target (chapter 03.5):
  a = 1 + 0.1·s, b = 0.1 texels (avoids reading the undrawn edge).
- The box downsample (6.7) skips the first s sub-pixels (top row, left column).
- A draw that fully overwrites a scaled target marks it Invalidated in native
  mode, but Cleared(0) in `off`/`normal` modes (rows the offset never covers
  must not show garbage).
- Texture shuffles never receive a half-pixel shift.

## 6.4 Sprite fixes (optional, default off)

GT4's profile enables none of them (Confirmed); HWR implements them behind
`GT4_HW_SPRITE_FIX=` (comma list: `round`, `align`, `merge`) for experiments.
They apply only when s > 1, the primitive class is sprite and the target is
scaled.

- **round** (FST sprites; `round` = nearest-filtered only, `round_full` also
  linear): for each sprite compute the native pixels actually covered
  (first = ceil(X0), last = floor(X1 − 1)), interpolate the texel coordinate at
  those centres and truncate to whole texels; nearest: U0 = t0, U1 = t1 + 16
  (U increasing) or U0 = t0 + 15, U1 = t1 (decreasing); linear: only
  U1 −= 8 when 0 < U1 − U0 ≤ (X1 − X0) + 32. Same for V. Vector: X0 = 0,
  X1 = 160, U0 = 0, U1 = 160 → unchanged (0, 160).
- **align**: on the first sprite, if the right edge X (minus OFX) has fraction
  exactly 0.5 and (FST with U fraction 0, or STQ) and there is a gap
  (fewer than 4 vertices or second X ≠ third X): add half a pixel to every
  sprite's right X (and right U for FST). Fixes dark vertical lines between
  abutting sprites.
- **merge**: source is a target, FST, 32/24-bit texture, constant colour/Q/Z,
  identical ΔX/ΔU (or ΔY/ΔV) for all sprites: replace the strip by one sprite
  over the bounding box. Fixes grid lines in tiled full-screen effects.

## 6.5 Points, lines and dither at scale (Confirmed behaviour, adopted)

- Points cover s x s, lines have width s (chapter 03.2), so they keep native
  thickness.
- Dither (`GT4_HW_DITHER=native|fine|off`): `native` (default, Decision:
  faithful look) indexes DIMX by the native pixel (gpu coordinate / s), so each
  dither cell covers s x s sub-pixels like the PS2 pattern; `fine` indexes by
  the upscaled pixel (finer, weaker pattern; the studied reference's default);
  `off` disables dithering. Owner question Q-05.
- SCANMSK counts native rows (chapter 03.8).

## 6.6 Native-resolution post-effects (optional; default off)

`GT4_HW_NATIVE_POSTFX=off|normal|aggressive`. Purpose: bloom/blur chains that
downsample the frame keep their kernel geometry when run at native resolution.

Classification of a textured draw whose source is a target (High; adopted from
the studied behaviour):

- Not applicable: untextured; nearest magnification when the next batch does
  not draw to this texture; points or lines; recursive with overlap; mip draws;
  texture ≤ 8 bpp; zero sizes.
- Keep scale: draw size equals the texture size within 1 texel, or a single
  sprite from a non-target.
- **Downscale**: draw size ≤ 75% of the texture on both axes (or smaller on both
  with a FRAME/TEX ping-pong between contexts), TBW ≥ FBW, draw narrower than
  the display, not recursive, gap-free, and the output discards the destination
  or is additive with FIX = 255 → the destination target is created at scale 1;
  the source is box-downsampled to a native copy when scaled.
- **Upscale**: TBW ≤ FBW, draw/texture ≥ 4 on either axis, gap-free, blended →
  in `aggressive` mode the source is box-downsampled too.
- A series of equal-sized textured sprites from a target with additive 50%
  blending (a recursive bloom pattern) counts as upscale.

Owner question Q-04 (chapter 15): enable `normal` by default for GT4 once
compared at 3x?

## 6.7 Box downsample (Confirmed behaviour, adopted)

Each output native pixel averages the s x s block of sub-pixels; read origin =
max(out·s, clamp minimum) where the clamp minimum is s in native half-pixel mode
(skips the undrawn row/column), divisor s². `aggressive` native post-fx uses a
sample step of 2 (sparser taps).

## 6.8 Scaled targets meeting unscaled data

- Memory sources are always 1x; shaders sample with normalized coordinates, so
  mixing 1x sources and s x targets is transparent.
- Sampling a target directly: the source inherits the target's scale; the
  remap factor is (2^TW / target unscaled width).
- Index reinterpretation of a scaled target (T8 from CT32): reduce to 1x first
  (box filter in native post-fx modes, else nearest), then convert; indices are
  never filtered.
- Hazard copies (texture = target): copy the needed region at scale, enlarged by
  1 pixel on each side for bilinear.
- Dirty uploads into a scaled target: drawn with bilinear filtering for colour
  when the entry's linear flag is set (chapter 05.1), nearest otherwise; depth
  always uses the converting shader with in-shader bilinear on decoded depth
  values, floored to the 2^-32 grid.
- Write-backs, GLM swizzles and display-independent readbacks take the top-left
  sub-sample of each native pixel (point sampling, chapter 10).

## 6.9 Acceptance (chapter 14 scenes at 2x, 3x, 4x)

- 2D overlays and HUD sprites show no one-pixel seams.
- Bloom, glare and the brightness/fade passes line up with the base image (no
  ghost offset of about one native pixel right or down).
- Depth-based effects show no edge halo; no black row/column at screen edges.
- Lines and points keep native thickness.
- Per-scene metric: the s x s box-downsampled HWR frame compared with the HWR
  1x frame: PSNR ≥ 30 dB in 3D scenes and ≥ 35 dB in menus (Hypothesis on the
  thresholds; tuned in M12 and recorded).

## Sources

- Research report `upscaling-clut-readbacks.md` part A (A.1 to A.14).
- Research report `upscaling-clut-readbacks.md` (earlier version) sections A.1
  to A.7 (vectors for mod_xy, vertex shift, round sprite).
- Research report `draw-translation-ubershader.md` 3.2, 3.3, addendum A2.
- Research report `texture-cache-v2.md` 2.3 (scale change resampling).
- Research report `gt4-gamedb-hacks-donor.md` 1.2, 1.3 (GT4 half-pixel mode and
  absent sprite fixes).
- Research report `blending-date-shuffles-dither.md` section 8 (dither modes).
