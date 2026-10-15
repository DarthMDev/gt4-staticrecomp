# Implementation notes (TASK-027)

Interpretations made by implementers where the specification was silent or
ambiguous. The specification team should confirm or correct each item; until
then they are binding for the implementation.

| Id | Milestone | Interpretation | Status |
|---|---|---|---|
| IN-01 | M5 pixel | A failed depth test discards the whole fragment (colour and Z). | to confirm |
| IN-02 | M5 pixel | Cd read from a CT16 frame expands 5-bit channels as c << 3 (TEXA rule, no replication); writes truncate >> 3. | to confirm |
| IN-03 | M5 pixel | Dither applies to every blended 16-bit output, including PABE pass-through pixels. | to confirm |
| IN-04 | M5 pixel | Frame words use the 32-bit view of 01.3 (CT16 valid mask 0x80F8F8F8) so FBMSK merges are bit exact. | to confirm |
| IN-05 | M2 memory | Z block XOR applied after adding BP (spec V-01-4). DIFFERS from the donor (in-page XOR) when BP is not a multiple of 32, e.g. BP 8 pixel (0,16): block 8 vs 24. Donor reproduces GT4 correctly; verify against GT4 depth-as-texture reads before relying on it. | to confirm (risk) |
| IN-06 | M2 memory | Pages per row clamped to >= 1 (as donor); 4-bit host data low nibble first; readback qword count rounds up; T8 and T8H not format-compatible; local->local copies raw bits truncated to destination width. | to confirm |
| IN-20 | M6 display | "Analogue" modes are NTSC, PAL and 1080i (13.1 names 1080i analogue; 1080i has CMOD = 0). Used by the interlaced flag and really-interlaced test. | to confirm |
| IN-21 | M6 display | "Really interlaced" = SYNCV.VFP bit 0 and an analogue mode. The table's "CMOD != 0" would exclude 1080i, so it is not used literally. | to confirm |
| IN-22 | M6 display | SMODE1.CMOD = 1 is not in the 13.1 table: no known mode, no image (12.4). | to confirm |
| IN-23 | M6 display | The INT of the 1080i, 720p and 480p rows is SMODE2.INT. 480p needs INT = 0; CMOD = 0 with LC = 32 and INT = 1 is VESA ("otherwise"). | to confirm |
| IN-24 | M6 display | Framebuffer wrap: when DBX (or DBY) plus the read width (height) exceeds 2048, 2048 is subtracted as written; the origin can be negative and the reader applies it modulo 2048. Width and height are the read rectangle. | to confirm |
| IN-25 | M6 display | Anti-blur (13.2) runs only when GT4_HW_ANTIBLUR is 1 and both circuits are enabled. Horizontal: DX equal and DBX differs by 1. Vertical: DBY differs by at most 1 and the circuit that changed alone moves onto the other, else the larger moves onto the smaller. DY rule (a): |DY difference| < 4 and no SCANMSK. | to confirm |
| IN-26 | M6 display | Line offset (b): a value in 0..4 (negative values ignored) adds its low bit to the display row. | to confirm |
| IN-27 | M6 display | Global shift (d) uses the reference circuit (RC1 when enabled, else RC2); its line-offset parity bit is subtracted from the shift before it is applied to every enabled circuit. | to confirm |
| IN-28 | M6 display | Relative x offset (c): RC1 is the reference; the difference of DX over the divider is applied to RC2 when its absolute value is at least 4, anti-blur is off or SCANMSK is in use. | to confirm |
| IN-29 | M6 display | Placement origin is (0, 0) unless a circuit sits at a negative offset; the union is measured from that origin. A one-line parity offset of a single circuit stays on screen. | to confirm |
| IN-30 | M6 display | "Toggling" and the 13.4 rule (a) count DBY changes over both circuits, enabled or not: toggling = at least one change, rule (a) = exactly one. | to confirm |
| IN-31 | M6 display | Output height: the analogue field height is doubled when the output is full height; progressive modes keep their table height. Output is the union clamped to the table, times scale. | to confirm |
| IN-32 | M15 deinterlace | `auto` rule (c): "not one line apart" means that the two enabled circuits do not have screen rows differing by exactly one. | to confirm |
| IN-33 | M6 merge | Same source = equal FBP and FBW and the same PSM family: {CT32, CT24}, {Z32, Z24}, {CT16, CT16S}, {Z16, Z16S}; other PSMs must be equal. | to confirm |
| IN-34 | M6 merge | Identical circuits (same source, rectangle and placement) drop RC1. Scaled copies use nearest sampling of the source (no filtering). | to confirm |
| IN-35 | M6 field | The VBlank field is CSR.FIELD bit 13 read as 0 or 1. The interlace offset is scale times (field XOR parity), applied to the destination of both circuits. | to confirm |
| IN-36 | M6 merge | Blend: colour = (src * a + dst * (255 - a) + 127) / 255 per channel; output alpha = source alpha. a = ALP for MMOD = 1; for MMOD = 0 a = min(255, 2 * texel alpha) (V-01-12 Hypothesis, the counter is open). | to confirm |
| IN-37 | M15 deinterlace | Row parity = field XOR (1 for `_bff`). Weave starts from the first merged image and then overwrites rows of the current parity. Blend taps replicate the edge rows and round as (a + 2b + c + 2) / 4 per channel. | to confirm |
| IN-38 | M15 deinterlace | Bob shifts by scale times (1 - field) rows with clamped edges. The integer shift makes the bilinear draw a copy. | to confirm |
| IN-39 | M15 deinterlace | Adaptive (our algorithm, 13.4): a history of the last four fields; the previous same-parity field is the newest older record with the same row parity. A missing interior line is "moving" when the lines above and below differ from the previous field by more than 0.08 (on any of R, G, B, normalised to 255), or when their average differs from the previous output line (the spec's "line itself"). Moving or no history: bob (average of the lines above and below); still: previous output line. Field rows and edge rows pass through. | to confirm |
| IN-40 | M15 post-fx | ShadeBoost clamps to 0..1 after each step; parameters are integers 1..200 (50 = 1.0). Scanlines halve odd rows with an integer shift. Alpha is kept. | to confirm |
| IN-41 | M6 present | Presentation: r = target ratio times crop over window ratio; r < 1 pillarboxes, r >= 1 letterboxes (equal ratio gives the full window). Integer offsets use floor division. | to confirm |
| IN-42 | M6/M15 options | `_bff` is accepted as a suffix on any deinterlace name; unknown names, empty or extra-comma lists, and bad ShadeBoost values make the parser return no value (the caller aborts). Post-fx names are applied in the fixed order of 13.5 with duplicates removed; `crt` is not accepted yet. | to confirm |
| IN-43 | M13 LOD | K is a signed 12-bit field with 4 fraction bits; |Q| = 0 gives an infinite LOD. The shader form min(K/16 - log2|Q| * 2^L, max) - base is used as written. | to confirm |
| IN-44 | M13 LOD | Base-level order (03.6): FST or LCM = 1 gives the constant K path; LCM = 0 without a vertex trace gives constant K; LCM = 0 with a traced minimum at or above MXL gives constant MXL (k is replaced by MXL). Trilinear rule applies to MMIN 3 and 5 only; MMIN 1 is linear without mips. | to confirm |
| IN-45 | M13 LOD | The 0.01 bias of the non-trilinear rule is returned separately as lod_bias and is not added to the integer base. Constant LOD reports max level = MXL. | to confirm |
| IN-46 | M13 MTBA | MTBA "32-bit formats" = CT32 and CT24. Level n starts after the block count of level n - 1, where the block count of level k is max(1, blocks0 / 4^k). This follows the 01.5 vector (1024, 1280, 1344) and overrides the prose "previous count / 4". TH is taken equal to TW, as the formula does. TBW = max(1, bw0 >> n). | to confirm |
| IN-47 | M13 LOD | Nearest mip level is round half up, floor(lod + 0.5) (V-01-11 Hypothesis, synthetic test only; owner comparison is open question Q-09). | Hypothesis |
| IN-48 | M13 LOD | Clamp bounds and coordinates are shifted right by the base level (integer) or divided by 2^base (float); the base level is clamped to [0, min(MXL, 6)]. | to confirm |
