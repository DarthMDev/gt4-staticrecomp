# 13 — Output stage and post-processing

The output stage runs once per guest VBlank (chapter 12.5): it reads the
display circuits, merges them, deinterlaces, applies optional post-processing
and draws the result into the 16:9 presentation rectangle of the swapchain.

```
display regs ──> (1) display state ──> (2) circuit sources (cache, 05.10)
                         │
                         v
                 (3) merge into the "merge image" (fs = mode size x scale)
                         │
                 (4) deinterlace (if really interlaced and mode ≠ off)
                         │
                 (5) ShadeBoost  (6) FXAA  (7) CAS   (optional, in that order)
                         │
                 (8) present pass: crop/aspect → 16:9 rectangle, black borders,
                     optional scanline/CRT filter, swapchain present
```

A "current" pointer names the latest image; each stage reads it and writes a
stage-owned persistent image (merge, weave/bob, blend, deinterlace history,
temporary, CAS), resized only when sizes change (weave and adaptive
deinterlace depend on previous frames).

## 13.1 Video mode (Confirmed)

| SMODE1.CMOD | Condition | Mode |
|---|---|---|
| 2 | — | NTSC |
| 3 | — | PAL |
| 0 | INT = 1, LC = 22 | 1080i |
| 0 | INT = 0, LC = 22 | 720p |
| 0 | INT = 0, LC = 32 | 480p (576p reports as 480p) |
| 0 | otherwise | VESA |

- "Really interlaced" = SYNCV.VFP bit 0 = 1 and CMOD ≠ 0; analogue modes = NTSC,
  PAL, 1080i. Interlaced flag = SMODE2.INT and analogue. 480p never
  deinterlaces.
- HWR produces no image when SMODE1 describes no known mode (chapter 12.4).
- GT4: boots NTSC 480i (SMODE2 = 0x3: INT = 1, FFMD = 1, written by the game
  every frame from VBlank 5), menus always interlaced, races optionally 480p or
  1080i (Confirmed, our logs).

Per-mode base table (width, field height, DX base, DY base; Confirmed):
NTSC 640, 224, 642, 25; PAL 640, 256, 676, 36; VESA 640, 480, 276, 34; 480p
720, 480, 232, 35; 720p 1280, 720, 302, 24; 1080i 1920, 540, 238, 40.
Horizontal clock dividers: NTSC/PAL 4, VESA/480p 2, 720p/1080i 1; vertical 1.

## 13.2 Circuit rectangles (Confirmed)

For circuit n with DISPFBn {FBP, FBW, PSM, DBX, DBY} and DISPLAYn {DX, DY,
MAGH, MAGV, DW, DH}, every VBlank:

1. Remember the previous DBX/DBY (for anti-blur alternation).
2. FBW = 0 and DW = 0 and DH = 0 and MAGH = 0 → circuit disabled even if PMODE
   enables it.
3. render size = ((DW + 1) / (MAGH + 1), (DH + 1) / (MAGV + 1)) (integer
   division); final size = min(render size, ((DW + 1)/hdiv, (DH + 1)/vdiv)).
4. Read rectangle = (0, 0, render size); with FFMD = 1 and interlaced, the height
   is (render height + 1) >> 1 (half-height buffer read per field).
5. Read offset (DBX, DBY); display offset (DX, DY).
6. Same-source check: both enabled, same FBP and FBW, bit-compatible PSM.

Vectors: NTSC 480i, DW 2559, DH 447, MAGH 3, MAGV 0 → 640x448 (fs at 3x =
1920x1344); 480p DW 1439, MAGH 1, DH 479 → 720x480.

**Display offsets** (default "offsets off" mode, Confirmed): the picture is
shown at its framebuffer position, not its raster position, except: (a)
anti-blur vertical: both enabled, no SCANMSK, |DY1 − DY0| < 4 → lower the larger
DY to the smaller; (b) per-circuit field parity: line offset = (DY − DYbase·
(interlaced + 1)) / vdiv; if ≤ 4 add its low bit to the display y; (c) relative
offset between two enabled circuits ((other − reference DX)/hdiv) applied when
≥ 4 or anti-blur is off or SCANMSK is active; (d) a global vertical shift
v = (reference DY − base)/vdiv when 0 < |v| ≤ 4 (minus parity).

**Framebuffer offsets and anti-blur** (Confirmed): if DBX + width > 2048
subtract 2048 (same for DBY); horizontal anti-blur when |DBX1 − DBX0| = 1 with
the same display x → both use the smaller DBX; vertical anti-blur when
|DBY1 − DBY0| ≤ 1: move the circuit that alternates (DBY changed since the last
VBlank) onto the other, or the larger onto the smaller. Purpose: games show the
same buffer on both circuits one line apart and blend them ("flicker filter");
anti-blur cancels it so upscaled images are sharp. Option
`GT4_HW_ANTIBLUR=1|0`, default 1.

## 13.3 Merge (Confirmed behaviour; one Hypothesis)

Output size fs: interlaced (or toggling, or deinterlace off) → full height.
Offsets off: neither circuit → table size (height doubled when full height);
both → union of display rectangles; one → its rectangle; clamp to the table
size; times scale.

Source selection: single read when both circuits read the same source (no
frame wrap, no feedback). Otherwise use RC1 = EN1 and not (MMOD = 1 and
ALP = 0); RC2 = EN2 and (SLBG = 0 and not "RC1 covers RC2 with MMOD 1, ALP 255").
If no source exists while a circuit is enabled, the frame is blank (nothing
presented this VBlank, previous image kept); both disabled → pure BGCOLOR.
Identical duplicate circuits drop RC1.

Composition (merge image cleared to BGCOLOR with alpha ALP):

1. RC2 copied (no blending) into its scaled display rectangle.
2. RC1 blended over its rectangle: colour = src·a + dst·(1 − a), output alpha =
   src alpha. MMOD = 1: a = ALP/255. MMOD = 0: a = RC1 texel alpha·2 clamped to 1
   (0x80 = 1.0) — **Hypothesis** (chapter 01.7, test V-01-12).
3. Interlace offset: when really interlaced, FFMD = 1, not bob, mode ≠ off: the
   destination is shifted down by scale·(field XOR parity setting) lines.
4. Sampling point (bilinear only in the offsets-on mode).

**Feedback write (EXTBUF/EXTWRITE)**: writes the merged output back into GS
memory (YUV conversion by EMODA/EMODC: Y = 219/255·(0.299R + 0.587G + 0.114B) +
16/255, Cb = 224/255·(−0.169R − 0.311G + 0.5B) + 128/255, Cr = 224/255·(0.5R −
0.419G − 0.081B) + 128/255; EMODA alpha: input, Y, Y/2, 0; EMODC colour: RGB,
YYY, YCbCr, alpha). Rare; HWR implements it only if GT4 enables EXTWRITE
(counter `extwrite_frames`; Hypothesis that GT4 never does).

GT4 requirement (Confirmed): frame-mode 480i with 224-line buffers must produce
a 448-line image whose field phase follows CSR.FIELD so that GT4's half-line
XYOFFSET difference between fields cancels (paraLLEl-GS with phase from
CSR.FIELD produced bob-free consecutive VBlanks; a one-line error changes 7.4%
of pixels). The field used is the donor's `g_odd_field` at present time
(chapter 12.4).

## 13.4 Deinterlace (Confirmed behaviour; algorithms Decision)

`GT4_HW_DEINTERLACE=auto|off|weave|bob|blend|adaptive` (default `auto`), field
parity top-first unless a `_bff` suffix is given.

- Runs only when a source exists, the signal is really interlaced and the mode
  is not `off`. Field = VBlank field XOR parity.
- `auto` picks `adaptive` when (a) exactly one circuit changed its DBY since the
  last VBlank (the game deinterlaces by itself), or (b) SMODE2.FFMD = 1, or (c)
  SCANMSK is in use and the two display rectangles are not one line apart;
  otherwise no deinterlacing. GT4 menus (FFMD = 1) therefore use `adaptive`.
- `weave`: write merged rows whose parity equals the field into a persistent
  image, keep the other rows from the previous frame.
- `bob`: draw the whole merged image shifted by scale·(1 − field) lines
  (bilinear).
- `blend`: weave, then a vertical 3-tap filter (1/4, 1/2, 1/4).
- `adaptive` (Decision: our own algorithm, written from this description, not
  from any GPL shader): keep a history of the last 4 fields; for each missing
  line compare the same position in the current and previous same-parity fields
  on the line above, the line itself and the line below; if any absolute
  difference exceeds a threshold (0.08 normalized, tunable) on any of R, G, B,
  interpolate from the lines above and below (bob); otherwise weave (take the
  previous field's line). Rows of the current field and the top and bottom rows
  pass through. Clear the history when no source exists (avoids a one-frame
  flash of stale content).

## 13.5 Post-processing (Decision; all optional)

`GT4_HW_POSTFX` comma list, applied in this fixed order after deinterlace:

| Name | Behaviour | Provenance rule |
|---|---|---|
| `shadeboost` | c = rgb·brightness; luma = dot(c, (0.2125, 0.7154, 0.0721)); c = mix(luma, c, saturation); c = mix(0.5, c, contrast); c = c^(1/gamma); alpha kept. Parameters `GT4_HW_SHADEBOOST=b,c,s,g` integers around 50 (= 1.0) | generic formula, write our own |
| `fxaa` | edge-only FXAA (subpixel amount 0), linear sampler, into a temporary | NVIDIA's public FXAA 3.11 release or our own; never a GPL-adapted file |
| `cas` | AMD FidelityFX CAS, sharpen-only when the internal resolution ≥ the window in both axes, else sharpen-and-resize to the destination rectangle; sharpness `GT4_HW_CAS_SHARPNESS` percent (default 50) | vendor AMD's MIT `ffx_a.h`/`ffx_cas.h` with notice |
| `scanlines` | at present: odd output rows × 0.5 | trivial, our own |

An optional `crt` present filter (shadow mask, Gaussian scanlines) may be added
later from the public-domain CRT shader by Timothy Lottes or our own.

## 13.6 Presentation (Decision; fixed 16:9 from chapter 12.5)

- Source crop: none by default (`GT4_HW_CROP=l,t,r,b` native pixels, scaled).
- Destination: the 16:9 presentation rectangle `gt4::vulkan::presentation_rect`
  (the game already renders 16:9 on the owner's card; Confirmed project state);
  borders cleared to black; the image drawn with a bilinear sampler (nearest
  when `GT4_HW_PRESENT_FILTER=nearest`, or "sharp bilinear": nearest pre-scale to
  the smallest integer multiple ≥ the window, then bilinear).
- Aspect rule for other options (later, launcher): r = target ratio ·
  crop_adjust / window ratio; r < 1 → width = floor(Ww·r + 0.5); r > 1 →
  height = floor(Wh / r + 0.5); centred. Vectors: 1920x1080 window, 4:3 →
  (240, 0, 1680, 1080); 1280x1024, 16:9 → height 720 at y 152.
- Swapchain: UNORM format (first surface format mapped to its non-sRGB variant),
  colour attachment + transfer destination usage, 2 images for FIFO, 3 for
  MAILBOX (clamped to surface limits), present modes per chapter 12.5.
- **Swapchain recreation on AMD RDNA4** (Confirmed by a separate study and
  recorded in the studied reference as a vendor workaround): recreating a
  swapchain while passing the old one as `oldSwapchain` (with
  swapchain-maintenance1) causes stalls of about 2 seconds in the next 2–3
  acquires on RDNA4 — the owner's RX 9060 XT is RDNA4. HWR therefore destroys
  the old swapchain first (after waiting for its presents) and creates the new
  one with no `oldSwapchain`, at least on AMD RDNA4 (Decision: on all AMD
  devices; logged). Resize/fullscreen toggles only set a flag; recreation
  happens before the next present (chapter 12.5).
- Screenshots and frame dumps read "current" before the present pass.

## 13.7 Counters (F8)

Presented frames, blank frames, merge circuits used, deinterlace mode in use,
post-fx GPU time (with `GT4_HW_GPU_TIMING=1`), swapchain recreations and their
duration.

## Sources

- Research report `output-postfx-vulkan-perf.md` part A (A.1 to A.14) and B.2,
  B.9 (swapchain, RDNA4 quirk, vendor workarounds), plus its licence-safety
  summary.
- Research report `gt4-gamedb-hacks-donor.md` 3.1 (GT4 video modes, field
  buffers, half-line offsets) and 4.3 (donor display path).
- Research report `ps2tek-gs-facts.md` section 6 (PCRTC registers, merge,
  interlace semantics).
- Research report `gs-state-gif-flush.md` section 7 (interlace detection,
  FIELD).
- Coordinator note (2026-10-15): separate study confirming the RDNA4 swapchain
  recreation stall.
- AMD GPUOpen FidelityFX CAS (MIT); NVIDIA FXAA 3.11 public release.
