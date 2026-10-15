# 14 — GT4 specifics and validation

This chapter lists what GT4 (SCUS-97328, NTSC-U, CORE.GT4 identical to the
gt4-decomp target) needs from HWR, the oracle used to validate it, the scenes,
the metrics and thresholds, and the catalogue of test vectors referenced by the
other chapters.

## 14.1 GT4 facts relevant to HWR

| Fact | Status | Chapter |
|---|---|---|
| PCSX2's game database gives GT4: half-pixel mode Native, auto-flush sprites-only, a Polyphony channel-shuffle hook, recommended blending High; nothing else (no texture-in-target, no native scaling, no sprite fixes, default mipmapping) | Confirmed (research) | 06, 02, 09, 08 |
| Same engine features in GT2000, GT3, GT4 Prologue, GT4 Online, Tourist Trophy (shared hook and auto-flush) | Confirmed (research) | 09 |
| Boot: one SetGsCrt(interlace 1, NTSC, field 1); the game writes SMODE2 = 0x3 every frame from VBlank 5 | Confirmed (our logs) | 13 |
| Menus: 480i, two 640x224 CT24 field buffers, FBW 10, FBP 0x00 and 0x8C, DISPLAY DH 447, one buffer per VBlank as a field | Confirmed | 13 |
| Fields rendered with a half-line XYOFFSET difference (OFY 1936.5 vs 1936.75) | Confirmed | 13 |
| Races: in-game option 480i / 480p / 1080i; menus stay interlaced; 480p is the most stable on the owner's PC | Confirmed | 13 |
| Menu background: T8 640x480 at DBP 0x1A40, REGLIST quads, TEX0 T8 TW 10 TH 9, CLUT CBP 0x1EA8 via TEX2 CLD 1 | Confirmed (capture) | 04, 07 |
| Car model textures upload as CT32 | Confirmed | 04 |
| Brightness/fade channel shuffles (variants A and B) | Confirmed in PCSX2's handling; scenes not yet identified in our captures | 09 |
| Lens flare / sun intensity need per-primitive sprite feedback | High | 02.6 |
| Night race: donor GS wait 4.1–4.3 ms per frame; paraLLEl-GS steady 60 FPS with page-tracked readbacks | Measured | 10 |
| Showroom: ~46,000 primitives per frame | Measured | 03 |
| GIF data arrives split at arbitrary qword boundaries (DMA chunks, VIF DIRECT) | Confirmed | 02 |
| Striped double clear (named for GT4 Online's engine) | High for retail GT4 | 05.8 |
| SIGNAL/FINISH/LABEL usage | Unknown | 10.7 |
| GT4's 480p DISPLAY register values | Unknown (to be logged in M11) | 13 |

## 14.2 Oracles

1. **Donor renderer at 1x (primary, MIT)**: executes GS semantics on an exact
   4 MiB memory image, rasterizes in order per tile. It is the oracle for memory
   semantics, formats, CLUT, blending formula, alpha/DATE tests, shuffles and
   uploads. Known gaps (Confirmed): no dithering, no mipmapping/LOD, no AA1, no
   PMODE merge (one circuit only), line-doubling of field buffers in dumps,
   feedback snapshotted per batch rather than per primitive. Comparisons against
   the donor therefore run HWR with `GT4_HW_DITHER=off`, `GT4_HW_MIPMAP=off`,
   `GT4_HW_AUTOFLUSH=sprites` and compare **VRAM-decoded display buffers**
   (`GT4_HW_DUMP_SOURCE=vram`), never scanouts.
2. **paraLLEl-GS at 1x (secondary, when TASK-012 is merged)**: a full GS
   emulation including dithering and mipmapping; used for mip/dither scenes
   with its own VRAM dump (`GT4_PGS_DUMP_SOURCE=vram`). Earlier measurement:
   donor vs paraLLEl-GS on menus differs in 0–0.18% of pixels by more than 8
   (PSNR 47–58 dB), which bounds what "equal" can mean.
3. **HWR reference mode** (`GT4_HW_SYNC_MODE=writeback`, chapter 00): the full
   cache must equal it at 1x on every scene (stronger threshold, 14.4).
4. **GLM path** for shuffles (`GT4_HW_VERIFY_SHUFFLE=1`, chapter 09).
5. **Owner's eyes**: upscaled scenes and features no oracle has (2x-6x, post-fx,
   replacements); the owner plays and reports with F7/F8.

## 14.3 Inputs: headless runs and capture replay

- **Headless schedule** (Confirmed, existing): `scripts/external_runtime_probe.py`
  with 4,200 frames, 12 presses (start at frame 600 for 30 frames, then
  cross/start/down every 300 frames until 3,900), card seed
  `private/slice113-video/widescreen-seed/card.ps2`, `--frame-every 100` →
  42 dumps. Reference hashes for the donor exist (e.g.
  `private/slice157-smoke/headless-157/frames`). It covers boot, logos, menus.
- **GS capture streams** (`GT4_PGS_CAPTURE=file`, `GT4_PGS_CAPTURE_FRAMES=a:b`,
  on branch `task/012-parallel-gs`): records 'G' (u8 path, u32 bytes, data),
  'P' (u32 address, u64 value), 'R' (u32 qwords FIFO read), 'V' (u64 frame).
  The owner records them during play for scenes outside the headless schedule.
  Captures are private (game data) and stay under `private/`.
- **Replay tool** (`tools/gs_replay`, built in M1): feeds a capture into a
  chosen backend (donor GS or HWR) without the game, honours 'R' records by
  performing the FIFO read (so readback paths run), and writes VRAM-decoded
  display buffers per frame (PPM) plus optional raw VRAM. Deterministic:
  replaying twice gives identical bytes (calibration).
- Capture set to obtain from the owner (open question Q-01): S-03 to S-11
  below, 120–300 frames each, at 480i and 480p where applicable.

## 14.4 Metrics and thresholds

Per frame, on the VRAM-decoded display buffer (RGB, alpha ignored):
`pct_bad` = percent of pixels whose max channel |Δ| > 8; `max_delta`; PSNR.

| Comparison | Gate | Notes |
|---|---|---|
| donor vs donor, HWR vs HWR (same input) | identical | calibration; failure = nondeterminism bug |
| HWR cache vs HWR writeback mode, 1x | pct_bad ≤ 0.01% and PSNR ≥ 60 dB (Decision) | cache bugs |
| HWR vs donor, 2D/menu scenes, 1x | pct_bad ≤ 0.5% and PSNR ≥ 40 dB | report-only until M8, gating after |
| HWR vs donor, 3D scenes, 1x | pct_bad ≤ 3% and PSNR ≥ 32 dB | report-only until M10, gating after |
| HWR vs paraLLEl-GS, mip/dither scenes, 1x | pct_bad ≤ 3% and PSNR ≥ 32 dB | from M13 |
| HWR Nx box-downsampled vs HWR 1x | PSNR ≥ 30 dB (3D), ≥ 35 dB (menus) | Hypothesis on values; tuned in M12 |
| HWR vs donor, Δ over frames | the number of frames whose pct_bad exceeds the gate must be 0 | |

Thresholds are recorded with each run in `result.json` and in the journal.
A scene that fails gets a minimal capture slice and a CTest replay test.

## 14.5 Scenes (owner tests)

Reference scene from the project rules: Time Trial, Nordschleife, Toyota Supra
Wood One, in-game video 480p.

| Id | Scene | What it exercises | Oracle |
|---|---|---|---|
| S-01 | Boot, logos, title (headless) | 480i FFMD output, uploads, simple blending | donor (headless) |
| S-02 | Main menu, garage, car select (headless + capture) | T8 + TEX2 CLUT loads, CT24 field buffers, fades | donor |
| S-03 | Time Trial Nordschleife Supra 480p | reference scene: geometry, depth, textures, sky, 480p output | donor, paraLLEl-GS |
| S-04 | Arcade race, 5 AI, 480i | many cars, interlaced output in race | donor |
| S-05 | Night race | lens flare, sprite feedback, readbacks | donor + F8 waits |
| S-06 | Rally stage | particles, dust, alpha blending | donor |
| S-07 | Menu fades and transitions | channel shuffles, variant B alpha stashes | donor + GLM verify |
| S-08 | Race start transition | variant A then B back to back | donor + GLM verify |
| S-09 | Replay | camera cuts, post-processing | donor |
| S-10 | Showroom / car viewer | 46k primitives, blending accuracy ("banding", "sheen") | donor, owner at 3x |
| S-11 | Race in 1080i | 1080i mode, LC 22 | donor |
| S-12 | Upscaled pass of S-03, S-05, S-07, S-10 at 3x and 4x | chapter 06 acceptance | owner |

Each scene's owner check lists: no missing effects, no seams, no flicker,
correct brightness, F8 frame time and `gs_wait`.

## 14.6 Test-vector catalogue (normative; each is a CTest unless marked)

| Id | Chapter | Content |
|---|---|---|
| V-01-1 | 01.1 | TEXCLUT COV masked to 10 bits; counter when bits 22..23 written |
| V-01-2 | 01.1 | FRAME PSM T4HL/T4HH treated as CT32 with forced FBMSK (counter; replay if GT4 uses it) |
| V-01-3 | 01.3 | T8 view of a CT32 page: lane and source pixel of every texel |
| V-01-4 | 01.3 | Z-format texture at a non-page-aligned TBP0: XOR after vs before adding BP |
| V-01-5 | 01.4 | sprite edge coverage at fractional edges vs donor |
| V-01-6 | 01.4 | point rounding vs donor |
| V-01-7 | 01.5 | HIGHLIGHT alpha clamp vs donor |
| V-01-8 | 01.5 | CT24 frame with DATE = 1 writes nothing |
| V-01-9 | 01.5, 08.1 | negative blend product shift vs donor |
| V-01-10 | 01.5 | dither on 32-bit frames: counter in GT4 scenes |
| V-01-11 | 01.5 | mip nearest-level rounding (synthetic; owner comparison Q-09) |
| V-01-12 | 01.7, 13.3 | PCRTC per-pixel merge alpha: counter for MMOD = 0 with both circuits |
| V-02-1 | 02.1 | PRE in REGLIST: counter |
| V-02-2 | 02.2 | PRMODECONT.AC = 0 usage: counter |
| V-02-3 | 02.7 | read overlap uses the source base |
| V-03-1 | 03.8 | SCANMSK rows at 2x counted in native rows |
| V-05-1 | 05.8 | striped double clear vs donor at 1x |
| V-07-1 | 07.7 | 16-bit GPU CLUT with CSA = 17 |
| V-08-1 | 08.7 | DATE with overlapping sprites |
| V-09-1 | 09.2 | texture shuffle G → A |
| V-10-1 | 10.4 | 2x CT32 write-back, point sampled, alpha-invalid mask |
| V-11-1 | 11.1 | replacement under REGION_CLAMP vs CLAMP (Hypothesis on filtering) |

Plus the inline vectors of every chapter (swizzle tables, TFX, fog, TEXA, MTBA,
CLD sequence, CSA clamp, CSM1 positions, draw-rectangle table, coverage edges,
blend table 08.1, alpha-test folding table, dirty merge, translation,
overlap with wrap, ageing, half-pixel shifts, mod values, round sprite, PCRTC
rectangles, aspect rectangles, Format P `bits` values).

## 14.7 CTest and Python fixtures (names are binding; added by the milestones of chapter 15)

`hw_swizzle_matches_donor`, `hw_reinterp_maps`, `hw_gif_split`,
`hw_vertex_trace`, `hw_st_rounding`, `hw_flush_rules`, `hw_autoflush`,
`hw_transfers`, `hw_clut_load`, `hw_clut_snapshot`, `hw_cache_geometry`,
`hw_dirty_merge`, `hw_ageing`, `hw_glm_equals_writeback`, `hw_blend_vectors`,
`hw_atest_fold`, `hw_flat_provoking`, `hw_pcrtc_rects`, `hw_present_rect`
(equals paraLLEl-GS's `widescreen_rect`), `hw_options` (unknown values abort),
`hw_replay_menu_1x` (capture replay vs stored donor output), Python
`test_hw_f8_block` (F8 section parser), `test_hw_launcher_options`
(`result.json` records HWR options), `test_hw_replay_compare` (metric script).

## Sources

- Research report `gt4-gamedb-hacks-donor.md` (all sections).
- Research report `upscaling-clut-readbacks.md` A.14 and the acceptance
  checklist.
- Research report `output-postfx-vulkan-perf.md` B.13 (suggested invariants).
- Research report `blending-date-shuffles-dither.md` section 11.
- Research report `texture-cache-v2.md` section 8.
- Research report `draw-translation-ubershader.md` addendum A8.
- Project documents: `docs/STATUS.md`, `docs/BUILD.md` (slices 157, 158, 164),
  `PARALLEL-GS-PLAN.md` on `task/012-parallel-gs` (sections 7-11, comparison
  procedure).
