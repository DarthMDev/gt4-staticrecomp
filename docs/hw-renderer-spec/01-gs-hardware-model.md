# 01 — GS hardware model

This chapter fixes the GS facts every other chapter relies on: registers,
local memory and its layouts, the pixel pipeline arithmetic, primitives, the
CLUT buffer and the display registers. Facts here are hardware facts; where the
public sources are thin, the label says so and a test is named.

Notation: `field[lo..hi]` are bit positions, LSB = 0. "12.4" means unsigned
fixed point with 4 fractional bits. 0x80 as an alpha or blend factor means 1.0.

## 1.1 General-purpose registers (Confirmed)

Written through the GIF (A+D, PACKED or REGLIST, chapter 02). Two drawing
contexts exist; registers with suffix _1/_2 are per context and PRIM.CTXT
selects which one a primitive uses.

| Addr | Register | Fields |
|---|---|---|
| 0x00 | PRIM | type[0..2] (0 point, 1 line, 2 line strip, 3 triangle, 4 triangle strip, 5 fan, 6 sprite, 7 reserved), IIP[3], TME[4], FGE[5], ABE[6], AA1[7], FST[8] (1 = UV, 0 = STQ), CTXT[9], FIX[10] |
| 0x01 | RGBAQ | R[0..7] G[8..15] B[16..23] A[24..31] Q[32..63] (IEEE float) |
| 0x02 | ST | S[0..31] T[32..63] (floats) |
| 0x03 | UV | U[0..13] V[16..29] (10.4 texels) |
| 0x04 | XYZF2 | X[0..15] Y[16..31] (12.4), Z[32..55], F[56..63]; vertex kick with drawing |
| 0x05 | XYZ2 | X[0..15] Y[16..31] Z[32..63]; vertex kick with drawing |
| 0x06/07 | TEX0 | TBP0[0..13] (blocks), TBW[14..19] (64 texels), PSM[20..25], TW[26..29], TH[30..33] (log2 size), TCC[34], TFX[35..36], CBP[37..50] (blocks), CPSM[51..54], CSM[55], CSA[56..60], CLD[61..63] |
| 0x08/09 | CLAMP | WMS[0..1], WMT[2..3] (0 repeat, 1 clamp, 2 region clamp, 3 region repeat), MINU[4..13], MAXU[14..23], MINV[24..33], MAXV[34..43] |
| 0x0A | FOG | F[56..63] |
| 0x0C | XYZF3 | as XYZF2, kick **without** drawing |
| 0x0D | XYZ3 | as XYZ2, kick without drawing |
| 0x14/15 | TEX1 | LCM[0], MXL[2..4], MMAG[5], MMIN[6..8], MTBA[9], L[19..20], K[32..43] (signed, 4 fraction bits) |
| 0x16/17 | TEX2 | writes only TEX0's PSM, CBP, CPSM, CSM, CSA, CLD |
| 0x18/19 | XYOFFSET | OFX[0..15], OFY[32..47] (12.4) |
| 0x1A | PRMODECONT | AC[0] (1 = attributes from PRIM, 0 = from PRMODE) |
| 0x1B | PRMODE | as PRIM bits 3..10 |
| 0x1C | TEXCLUT | CBW[0..5], COU[6..11] (16-pixel units), COV[12..21] |
| 0x22 | SCANMSK | MSK[0..1] (0 normal, 2 skip even rows, 3 skip odd rows) |
| 0x34/35 | MIPTBP1 | TBP1[0..13] TBW1[14..19] TBP2[20..33] TBW2[34..39] TBP3[40..53] TBW3[54..59] |
| 0x36/37 | MIPTBP2 | same layout for levels 4..6 |
| 0x3B | TEXA | TA0[0..7], AEM[15], TA1[32..39] |
| 0x3D | FOGCOL | FCR[0..7] FCG[8..15] FCB[16..23] |
| 0x3F | TEXFLUSH | any value |
| 0x40/41 | SCISSOR | SCAX0[0..10] SCAX1[16..26] SCAY0[32..42] SCAY1[48..58] (inclusive window pixels) |
| 0x42/43 | ALPHA | A[0..1] B[2..3] C[4..5] D[6..7] FIX[32..39] |
| 0x44 | DIMX | 16 signed 3-bit entries (-4..+3); entry (row i, column j) at bit 4·(4i+j) |
| 0x45 | DTHE | DTHE[0] |
| 0x46 | COLCLAMP | CLAMP[0] (1 clamp, 0 wrap) |
| 0x47/48 | TEST | ATE[0], ATST[1..3], AREF[4..11], AFAIL[12..13], DATE[14], DATM[15], ZTE[16], ZTST[17..18] |
| 0x49 | PABE | PABE[0] |
| 0x4A/4B | FBA | FBA[0] |
| 0x4C/4D | FRAME | FBP[0..8] (pages), FBW[16..21], PSM[24..29], FBMSK[32..63] (1 = bit kept) |
| 0x4E/4F | ZBUF | ZBP[0..8] (pages), PSM[24..27] (Z formats, high bits implied), ZMSK[32] |
| 0x50 | BITBLTBUF | SBP[0..13] SBW[16..21] SPSM[24..29] DBP[32..45] DBW[48..53] DPSM[56..61] |
| 0x51 | TRXPOS | SSAX[0..10] SSAY[16..26] DSAX[32..42] DSAY[48..58] DIR[59..60] |
| 0x52 | TRXREG | RRW[0..11] RRH[32..43] |
| 0x53 | TRXDIR | XDIR[0..1] (0 host->local, 1 local->host, 2 local->local, 3 none) |
| 0x54 | HWREG | 64 bits of image data |
| 0x60 | SIGNAL | ID[0..31] IDMSK[32..63] |
| 0x61 | FINISH | any |
| 0x62 | LABEL | ID[0..31] IDMSK[32..63] |
| 0x7F | NOP | — |

Public-header errata to avoid (Confirmed by the research team): one public
page lists FOG at bits 0..7 and ALPHA.FIX at bits 8..15; one SDK header masks
DIMX entries to 2 bits. The positions above are correct. TEXCLUT.COV is 10 bits
in the manual and 12 in one header (**Hypothesis**: 10 bits; mask to 10 and log
a counter if a game writes bits 22..23; test V-01-1).

Enumerations:

| Field | Values |
|---|---|
| ATST | 0 NEVER, 1 ALWAYS, 2 LESS, 3 LEQUAL, 4 EQUAL, 5 GEQUAL, 6 GREATER, 7 NOTEQUAL (source alpha vs AREF) |
| AFAIL | 0 KEEP (nothing), 1 FB_ONLY (colour incl. alpha, no Z), 2 ZB_ONLY (Z only), 3 RGB_ONLY (RGB only) |
| ZTST | 0 NEVER, 1 ALWAYS, 2 GEQUAL, 3 GREATER (larger Z is nearer) |
| MMIN | 0 nearest, 1 linear, 2 nearest-mip-nearest, 3 nearest-mip-linear, 4 linear-mip-nearest, 5 linear-mip-linear |
| TFX | 0 MODULATE, 1 DECAL, 2 HIGHLIGHT, 3 HIGHLIGHT2 |
| TRXPOS.DIR | pixel order for local->local: 0 UL->LR, 1 LL->UR, 2 UR->LL, 3 LR->UL |

Write-time normalisation that HWR applies (High; behaviour recorded from the
studied reference, where it is stated as tested on hardware):

- ALPHA selector value 3 behaves as 2 (zero / FIX).
- FRAME.FBW is limited to 32.
- If FRAME.PSM is a Z format, the same context's ZBUF is treated as using the
  colour layout, and vice versa (colour FRAME forces ZBUF into the Z layout).
  This coupling is applied when either register is written.
- FRAME.PSM = T8H, T4HL, T4HH: treated as CT32 with FBMSK forced to 0x00FFFFFF,
  0xF0FFFFFF and 0x0FFFFFFF respectively (only the alpha byte or nibble is
  written). **Hypothesis** for the two 4-bit cases (only T8H was tested by the
  reference); counter `frame_psm_4h` in F8, test V-01-2 if GT4 ever uses it.
- TEX0.CPSM is masked with 0b1010 (CT24 becomes CT32; CT16 and CT16S kept).

## 1.2 Privileged registers (Confirmed for addresses and fields)

| Register | Address | Fields used by HWR |
|---|---|---|
| PMODE | 0x12000000 | EN1[0], EN2[1], MMOD[5], AMOD[6], SLBG[7], ALP[8..15] |
| SMODE1 | 0x12000010 | CMOD[13..14], LC[3..9] (video-mode detection only) |
| SMODE2 | 0x12000020 | INT[0], FFMD[1], DPMS[2..3] |
| SYNCV | 0x12000060 | VFP[0..9] (bit 0 used for "really interlaced") |
| DISPFB1/2 | 0x12000070/90 | FBP[0..8] (pages), FBW[9..14], PSM[15..19], DBX[32..42], DBY[43..53] |
| DISPLAY1/2 | 0x12000080/A0 | DX[0..11] (VCK), DY[12..22] (lines), MAGH[23..26], MAGV[27..28], DW[32..43], DH[44..54] |
| EXTBUF | 0x120000B0 | EXBP[0..13], EXBW[14..19], FBIN[20..21], WFFMD[22], EMODA[23..24], EMODC[25..26], WDX[32..42], WDY[43..53] |
| EXTDATA | 0x120000C0 | SX[0..11], SY[12..22], SMPH[23..26], SMPV[27..28], WW[32..43], WH[44..54] |
| EXTWRITE | 0x120000D0 | WRITE[0] |
| BGCOLOR | 0x120000E0 | R[0..7] G[8..15] B[16..23] |
| CSR | 0x12001000 | SIGNAL[0], FINISH[1], HSINT[2], VSINT[3], FIELD[13] — owned by the donor |
| IMR | 0x12001010 | owned by the donor |
| SIGLBLID | 0x12001080 | SIGID[0..31], LBLID[32..63] — owned by the donor |

SIGNAL: `SIGID = (SIGID & ~IDMSK) | (ID & IDMSK)`, CSR.SIGNAL set, interrupt
unless masked; a second SIGNAL while CSR.SIGNAL is set stalls the path until
the CPU clears it. LABEL: same merge into LBLID, no interrupt. FINISH: CSR.FINISH
once all earlier drawing has been consumed. These live in the donor (chapter 10
and 12).

## 1.3 Local memory (Confirmed)

| Unit | Size |
|---|---|
| Local memory | 4 MiB = 512 pages = 16384 blocks |
| Page | 8 KiB = 32 blocks |
| Block | 256 bytes = 4 columns |
| Column | 64 bytes |

Address units: FBP, ZBP, DISPFB.FBP in pages; TBP0, CBP, SBP, DBP, MIPTBP,
EXBP in blocks; all widths (FBW, TBW, SBW, DBW, CBW, EXBW) in 64-pixel units.
Block addresses wrap modulo 16384; transfer X coordinates wrap at 2048.

### PSM table (Confirmed)

PSM encoding: bits 4..5 = 3 means a Z format; low 3 bits 0/1/2 = 32/24/16 bit;
bit 3 = the "S" variant; `(PSM & 7) >= 3` means indexed.

| PSM | Code | Storage | Transfer bpp | Page (px) | Block (px) | Valid-bit mask (32-bit view) | Palette |
|---|---|---|---|---|---|---|---|
| CT32 | 0x00 | 32 | 32 | 64x32 | 8x8 | 0xFFFFFFFF | — |
| CT24 | 0x01 | 32 (bits 0..23) | 24 | 64x32 | 8x8 | 0x00FFFFFF | — |
| CT16 | 0x02 | 16 | 16 | 64x64 | 16x8 | 0x80F8F8F8 | — |
| CT16S | 0x0A | 16 | 16 | 64x64 | 16x8 | 0x80F8F8F8 | — |
| T8 | 0x13 | 8 | 8 | 128x64 | 16x16 | — | 256 |
| T4 | 0x14 | 4 | 4 | 128x128 | 32x16 | — | 16 |
| T8H | 0x1B | bits 24..31 of a 32-bit word | 8 | 64x32 | 8x8 | 0xFF000000 | 256 |
| T4HL | 0x24 | bits 24..27 | 4 | 64x32 | 8x8 | 0x0F000000 | 16 |
| T4HH | 0x2C | bits 28..31 | 4 | 64x32 | 8x8 | 0xF0000000 | 16 |
| Z32 | 0x30 | 32 | 32 | 64x32 | 8x8 | 0xFFFFFFFF | — |
| Z24 | 0x31 | 32 (bits 0..23) | 24 | 64x32 | 8x8 | 0x00FFFFFF | — |
| Z16 | 0x32 | 16 | 16 | 64x64 | 16x8 | 0xFFFF | — |
| Z16S | 0x3A | 16 | 16 | 64x64 | 16x8 | 0xFFFF | — |

Code 0x12 (undocumented) is treated as invalid by HWR: draws using it are
skipped and counted (`invalid_psm`).

Format relationship predicates (Decision: implement as 64x64 tables generated
from the rules, unit-tested):

- **Shared bits**: two formats can disturb each other's bits in the same
  32-bit word. All pairs share, except: {CT24 or Z24} with {T8H, T4HL, T4HH};
  T4HL with T4HH.
- **Compatible depth class**: 32 and 24 are compatible with each other; 16
  only with 16 (CT16, CT16S, Z16, Z16S); 8 and 4 only with themselves.
- **Same swizzle**: CT32/CT24/T8H/T4HL/T4HH share one layout; Z32/Z24 share
  one; CT16, CT16S, Z16, Z16S, T8, T4 each have their own.
- **Channel set** (RGBA bit mask): CT24/Z24 = RGB; T8H/T4HL/T4HH = A only;
  everything else = RGBA.

### Block arrangement within a page (Confirmed; settled by the donor tables)

Entries are block numbers within the page, rows top to bottom.

CT32, CT24, T8H, T4HL, T4HH, and T8 (8 x 4 blocks):

```
 0  1  4  5 16 17 20 21
 2  3  6  7 18 19 22 23
 8  9 12 13 24 25 28 29
10 11 14 15 26 27 30 31
```

CT16 and T4 (4 x 8 blocks):

```
 0  2  8 10
 1  3  9 11
 4  6 12 14
 5  7 13 15
16 18 24 26
17 19 25 27
20 22 28 30
21 23 29 31
```

CT16S (4 x 8 blocks):

```
 0  2 16 18
 1  3 17 19
 8 10 24 26
 9 11 25 27
 4  6 20 22
 5  7 21 23
12 14 28 30
13 15 29 31
```

Z32/Z24 use the CT32 table with every entry XOR 24; Z16 uses CT16 XOR 24; Z16S
uses CT16S XOR 24. A public slide summary quoted a different Z32 row
(a left/right half swap); the XOR-24 form is what our donor renderer uses and
what GT4 renders correctly with, and it agrees with a second public source, so
it is **Confirmed for our purposes**.

### Pixel position inside a block (Confirmed; donor tables)

A block holds 4 columns stacked vertically. For a pixel at block-local (bx, by):

- **32-bit formats (8x8 block)**: column = by / 2 (0..3); within the column
  (8x2 pixels) the word index is taken from row `by & 1` of

  ```
  0 1 4 5  8  9 12 13
  2 3 6 7 10 11 14 15
  ```

  byte offset = 64·column + 4·word.
- **16-bit formats (16x8 block)**: column = by / 2; halfword index from row
  `by & 1` of

  ```
  0 2  8 10 16 18 24 26  1 3  9 11 17 19 25 27
  4 6 12 14 20 22 28 30  5 7 13 15 21 23 29 31
  ```

  byte offset = 64·column + 2·halfword.
- **8-bit (T8, 16x16 block)**: column = by / 4 (0..3); r = by mod 4.
  The 32-bit word inside the column uses the 32-bit column pattern above,
  indexed with row `r & 1` and position `bx & 7`, but with the two 4-wide
  halves of each row **swapped** when `(r >> 1) XOR (column & 1)` is 1.
  The byte inside the word is `2·((bx >> 3) & 1) + ((r >> 1) & 1)`.
  byte offset = 64·column + 4·word + byte.
- **4-bit (T4, 32x16 block)**: column = by / 4; r = by mod 4. The word uses
  the same swapped-or-not 32-bit column pattern indexed with row `r & 1` and
  position `bx & 7`, with the swap condition `(r >> 1) XOR (column & 1)`.
  The nibble inside the word is `2·((bx >> 3) & 3) + ((r >> 1) & 1)` (0..7,
  nibble 0 = bits 0..3 of the word's first byte).
  nibble address = 2·(64·column + 4·word) + nibble.

The 8-bit rule gives the **byte-lane property** GT4's effects depend on (09):
inside a T8 view of CT32 data, texels with `(u & 8, v & 2)` = (0,0), (0,2),
(8,0), (8,2) come from the R, G, B, A bytes respectively. Test vector V-01-3
(chapter 14): fill one CT32 page with R = x, G = y, B = 0x80|x, A = 0xC0|y,
read it back as T8 (TBW doubled) and check every texel's lane and source pixel.

The donor renderer's tables (`runtime/src/gs/gs.cpp`, the block tables, the two
column tables and the 8/4-bit builders) are the reference implementation of
this section and may be reused with MIT attribution. HWR's swizzle library must
reproduce them byte for byte (CTest `hw_swizzle_matches_donor`, all PSMs, a
full 4 MiB random round trip).

### Address computation (Confirmed, with one Hypothesis)

For pixel (x, y) of a buffer (BP, BW, PSM):

1. pages per row = BW·64 / page width (for 8- and 4-bit formats this is
   BW / 2, rounded down; an odd BW with those formats has undefined hardware
   behaviour and HWR uses the rounded value).
2. page = (y / page height)·pages per row + (x / page width).
3. block = BP + 32·page + table[(y mod page height) / block height]
   [(x mod page width) / block width] (colour table).
4. For Z formats the block is XOR 24. **Hypothesis**: the XOR applies to the
   final block number after adding BP (this matters only for a Z-format buffer
   whose BP is not a multiple of 32). The studied reference and the PCSX2 pack
   hash both use "after adding"; the donor applies it to the in-page entry,
   which is equal for page-aligned bases. HWR follows "after adding"; test
   V-01-4 compares both forms on a Z-format texture at a non-page-aligned TBP0
   and the F8 counter `z_unaligned_reads` records whether GT4 ever does this.
5. block mod 16384, then the in-block offset from the previous subsection.

### Formats sharing words (Confirmed)

CT24/Z24 can coexist with T8H/T4HL/T4HH at the same address (disjoint bits), and
T4HL with T4HH. Writes in CT24 leave bits 24..31 untouched; writes in T8H touch
only bits 24..31.

## 1.4 Primitives and coverage (Confirmed unless marked)

- Positions are 12.4 primitive coordinates; window position =
  (XY − XYOFFSET) / 16. Usable window range is 0..2047.
- Pixel centres are at **integer** coordinates; fill rule is top-left: a pixel
  N on an axis is covered by a span [start, end) when start ≤ N < end.
- Triangles: no face culling. Colour/Z/fog/texture coordinates are
  interpolated linearly in screen space; with FST = 0 the texture coordinate is
  S/Q, T/Q computed per pixel (perspective correct because S, T, Q are
  interpolated linearly).
- Sprites: two vertices are opposite corners of an axis-aligned rectangle.
  Colour, Z, fog and Q come from the second vertex; texture coordinates
  interpolate across the rectangle. Covered pixels follow the same rule
  (ceil(x0) .. ceil(x1) − 1 for integer-centred pixels; **Hypothesis** for the
  exact rounding at fractional edges; test V-01-5 against the donor, whose
  sprite rule is "ceil(min) to ceil(max) − 1").
- Lines: Bresenham-like (diamond exit is the closest GPU rule). Points: snap to
  the nearest pixel (rounding unknown; test V-01-6 against the donor).
- Flat shading (IIP = 0): the colour of the last vertex of the primitive.
- Strips keep the last 2 vertices; fans keep the first and the last; lists clear
  the queue. Register writes between vertices do not reset the queue; only a
  PRIM write does.
- A kick latches the current RGBAQ, ST/UV and FOG with the XYZ written.

## 1.5 Pixel pipeline (normative order and arithmetic)

Order: rasterize → texture fetch (CLUT, TEXA, filter) → texture function → fog
→ AA1 coverage → scissor and SCANMSK → alpha test → destination alpha test →
depth test → blend → dither → colour clamp/wrap → format conversion → FBA →
FBMSK merge and write.

### Texture function (Confirmed)

All values are integers 0..255, 0x80 = 1.0. T = texel, C = fragment colour.

| TFX | RGB | A (TCC = 1) | A (TCC = 0) |
|---|---|---|---|
| MODULATE | min(255, (T·C) >> 7) | min(255, (Ta·Ca) >> 7) | Ca |
| DECAL | T | Ta | Ca |
| HIGHLIGHT | min(255, ((T·C) >> 7) + Ca) | min(255, Ta + Ca) | Ca |
| HIGHLIGHT2 | min(255, ((T·C) >> 7) + Ca) | Ta | Ca |

The HIGHLIGHT alpha clamp is **High** (test V-01-7 against the donor).

Test vectors: MODULATE T = 1, C = 255 → 1 (truncation). HIGHLIGHT T.rgb = 100,
C.rgb = 128, Ca = 50 → rgb 150, alpha Ta + 50.

### TEXA expansion (Confirmed)

- 16-bit texels: R = (c & 0x1F) << 3, G = ((c >> 5) & 0x1F) << 3,
  B = ((c >> 10) & 0x1F) << 3 (no bit replication); A = TA1 if bit 15 is set,
  else TA0, except A = 0 when AEM = 1 and R = G = B = 0.
- 24-bit texels: A = TA0, except 0 when AEM = 1 and R = G = B = 0.
- 16-bit CLUT entries use the 16-bit rule (chapter 07 for the exact AEM test).

Test vectors: 0x8000 with TA1 = 0x80 → (0,0,0,0x80); 0x0000 with AEM = 1 →
alpha 0; 0x7FFF → (248,248,248,TA0).

### Fog (High; rounding Hypothesis)

`C' = (F·C + (255 − F)·FOGCOL) >> 8` per RGB channel; F = 255 means almost no
fog; alpha untouched. The studied reference uses an F/256 weight that differs
by at most one LSB. HWR implements the formula above (the donor does too).
Test vector: C = 200, FOGCOL = 0, F = 128 → 100; F = 255 → 199.

### Alpha test (Confirmed)

Integer As compared with AREF per ATST; AFAIL decides what a failing pixel still
writes. RGB_ONLY on a frame that is not 32-bit behaves as FB_ONLY (High).

### Destination alpha test (Confirmed)

With DATE = 1 the pixel is written only if the destination alpha MSB (bit 31
for 32-bit frames, bit 15 for 16-bit) equals DATM. With a CT24 frame and
DATE = 1 nothing is written at all (colour or Z) (High; recorded from the
studied reference as a hardware test result; test V-01-8 replays a capture
with this case if GT4 has one, else a synthetic fixture checks HWR skips it).

### Depth (Confirmed; clamping High)

ZTST NEVER/ALWAYS/GEQUAL/GREATER on unsigned Z; Z32 uses all 32 bits, Z24 the
low 24 (upper 8 untouched), Z16 16 bits. Z above the format maximum is clamped
to the maximum (not wrapped). ZMSK = 1 disables the write. Interpolated Z is an
integer (truncated).

### Blending (Confirmed; negative rounding Hypothesis)

Per RGB channel: `Cv = (((A − B)·C) >> 7) + D`, A, B, D ∈ {Cs, Cd, 0},
C ∈ {As, Ad, FIX}, all 8-bit, intermediate signed; C may exceed 0x80 (up to
0xFF ≈ 1.99). Ad on a 24-bit frame is 0x80 (High). Ad on a 16-bit frame is
0x80 when the A bit is set, else 0. Alpha is not blended: the written alpha is
As (then FBA). With PABE = 1, pixels whose As bit 7 is 0 are written as Cs
unblended.

**Hypothesis**: the shift of a negative product is arithmetic (floor), e.g.
(−1·129) >> 7 = −2, not −1. Test V-01-9: a synthetic fixture with mode
(Cd − Cs)·FIX + 0, Cd = 0, Cs = 1, FIX = 0x81, COLCLAMP = 0, compared with the
donor (whose WGSL uses an arithmetic shift) — HWR matches the donor; if an
owner-visible difference appears, revisit.

### Dither (Confirmed for 16-bit; 32-bit Hypothesis)

If DTHE = 1: `Cv += DIMX[y & 3][x & 3]` (signed −4..+3) on RGB, after blending
and before clamp/wrap. Effective for 16-bit frame formats, whose conversion
keeps the top 5 bits. **Hypothesis**: no effect on 32-bit frames (the manual
implies 16-bit only); HWR dithers only 16-bit frames; test V-01-10 counts DTHE
draws to 32-bit frames in GT4 scenes (F8 `dither_on_32bit`), and if non-zero the
owner compares against a real PS2 photo (open question Q-08 in chapter 15).
Test vector: 16-bit frame, DIMX entry −4, value 0x13 → 0x0F → clamp → 0x08.

### Colour clamp, format conversion, FBA, FBMSK (Confirmed)

- COLCLAMP = 1: clamp 0..255; = 0: keep low 8 bits (260 → 4, −3 → 253).
- 16-bit frames: R >> 3, G >> 3, B >> 3, A bit = alpha bit 7.
- CT24 frames: bits 24..31 not written.
- FBA = 1: OR 0x80 into the written alpha (sets the 16-bit A bit). The
  blend and alpha test still use the original As.
- Write: `new = (old & FBMSK) | (computed & ~FBMSK)`.

### Texture addressing (Confirmed unless marked)

- Coordinates: FST = 1: texel = U/16, V/16. FST = 0: texel = (S/Q)·2^TW,
  (T/Q)·2^TH. ST floats have their low mantissa bits ignored by hardware
  (chapter 02 gives the rounding rule HWR applies).
- Texel centres at half-texel offsets: bilinear uses (u − 0.5) with floor;
  sub-texel precision is 4 bits; nearest uses floor(u).
- Wrap modes per axis: REPEAT `u mod 2^TW`; CLAMP to [0, 2^TW − 1];
  REGION_CLAMP to [MINU, MAXU] inclusive; REGION_REPEAT
  `u' = (u & MINU) | MAXU` (MINU is the mask, MAXU the fixed bits).
- TW/TH above 10: a texture dimension above 1024 is invalid. HWR treats a
  draw with TW or TH > 10 and no region on that axis as skipped and counted
  (**High**, matches the studied reference's handling).
- Filter selection: LOD ≤ 0 uses MMAG, LOD > 0 uses MMIN.
- LOD: LCM = 0: `LOD = (log2(1/|Q|) << L) + K/16`; LCM = 1: `LOD = K/16`.
  Mip level n has size max(1, size >> n), base and width from TEX0 (n = 0),
  MIPTBP1 (1..3), MIPTBP2 (4..6). Level clamped to [0, MXL]. Rounding of
  `*_MIPMAP_NEAREST` level selection: **Hypothesis** round-half-up (test
  V-01-11, synthetic; no oracle has mipmaps, so the owner compares a scene with
  mip-heavy track textures at 1x against PCSX2 screenshots he takes himself —
  open question Q-09).
- MTBA = 1 on a TEX0 write (TW between 5 and the maximum, not a Z format):
  the GS fills MIPTBP1 with levels packed after level 0. HWR rule (High):
  bw0 = max(1, 2^TW / 64); blocks0 = 2^TW·2^TW·bpp / 2048 (bpp in bits);
  level 1 TBP = TBP0 + blocks0, TBW = max(1, bw0 / 2); each next level adds the
  previous level's block count divided by 4 (minimum 1) and halves TBW (minimum
  1). Maximum TW for MTBA is 9 for 32-bit formats and 10 otherwise.
  Test vector: CT32, TW = 8, TBP0 = 0 → levels 1..3 at TBP 1024/1280/1344 with
  TBW 2/1/1.

## 1.6 CLUT buffer (Confirmed)

- 1 KiB on chip: 512 halfwords. A 32-bit entry i stores its low 16 bits at
  halfword i and its high 16 bits at halfword i + 256. A 16-bit entry uses one
  halfword.
- CSA (5 bits) offsets in 16-entry units: 32-bit CLUT uses CSA & 15; 16-bit
  CLUT uses all 5 bits (halfwords 0..511).
- The CLUT is loaded at TEX0/TEX2 write time from local memory (CLD rules,
  chapter 07). Later writes to that memory do not change the buffer until the
  next load.
- CSM1 (CSM = 0): the palette is stored as a small image at CBP in CPSM format,
  16x16 pixels for 8-bit indices (index i at x = (i & 7) + 8·bit4(i),
  y = 2·(i >> 5) + bit3(i)) and 8x2 for 4-bit indices (x = i & 7, y = i >> 3).
  Test vectors: i = 0 → (0,0); 7 → (7,0); 8 → (0,1); 16 → (8,0); 24 → (8,1);
  32 → (0,2); 255 → (15,15).
- CSM2 (CSM = 1): a linear row of entries at (COU·16, COV) in a buffer of width
  CBW, CT16 only on hardware (HWR also accepts CT32 and CT16S as the studied
  reference does; counter `csm2_non16`).

## 1.7 Display (PCRTC) model

Two read circuits (RC1 = DISPFB1/DISPLAY1, RC2 = DISPFB2/DISPLAY2), enabled by
PMODE.EN1/EN2. Each reads a rectangle from (DBX, DBY) of its buffer and places
it on the raster at (DX, DY) with magnification (MAGH + 1) VCK per pixel and
(MAGV + 1) lines per pixel; covered source size =
((DW + 1)/(MAGH + 1), (DH + 1)/(MAGV + 1)). Merge:
`out = RC1·α + B·(1 − α)`, B = RC2 if SLBG = 0 else BGCOLOR, α = ALP/255 if
MMOD = 1, else RC1's pixel alpha. **Hypothesis**: per-pixel RC1 alpha uses
0x80 = 1.0 (the studied reference doubles it and saturates) rather than
0xFF = 1.0 (one public writer believes the latter). Test V-01-12: count GT4
frames with both circuits enabled and MMOD = 0 (F8 `merge_pixel_alpha`); if
zero, the question is moot for GT4; otherwise the owner compares a frame with
PCSX2 (open question Q-10).

Interlace: SMODE2.INT = 1 is interlaced. With FFMD = 1 each field reads every
line of a field-height buffer (GT4 menus: 640x224 buffers); with FFMD = 0 each
field reads every other line of a full-height buffer. (Public sources name the
two modes inconsistently; the behaviour stated here is what all sources agree
on and what GT4's register logs show.) CSR.FIELD is the field being displayed.

Chapter 13 gives the full output stage.

## Sources

- Research report `ps2tek-gs-facts.md` (all sections; public sources: ps2tek,
  ps2sdk `gs_gp.h` and `gs_privileged.h`, Themaister's paraLLEl-GS articles,
  jbit's GS fill-rate article, SCEA slides, MAME PSM enum).
- Research report `gs-state-gif-flush.md` sections 1.5, 1.6, 2.2, 2.6, 8.
- Research report `draw-translation-ubershader.md` sections 2.3, 2.12, 2.13
  and addendum A8.
- Research report `blending-date-shuffles-dither.md` sections 1.1, 3.1, 4.1, 8.
- Research report `upscaling-clut-readbacks.md` part B.1, B.2.
- Research report `texture-replacement.md` section 1.5 (block tables, Z XOR
  placement).
- Research report `gt4-gamedb-hacks-donor.md` sections 2.1, 3.1, 4.
- Donor renderer (MIT): `private/references/ps2-web-recomp/runtime/src/gs/gs.cpp`
  lines ~55-115 (block, column and 8/4-bit tables) and `gpu/gs.wgsl`.
