# 11 — Texture dumping and replacement

HWR lets the user replace game textures with higher-resolution images. Two key
formats are specified:

- **Format G** (GT4Recomp's own, milestone M16): simple, robust, independent
  of emulator settings.
- **Format P** (exact compatibility with PCSX2 texture packs, milestone M18,
  later): reproduces the PCSX2 pack naming and hashing byte for byte so existing
  community packs for SCUS-97328 load unchanged.

Both share the load pipeline (11.4) and the eligibility rules (11.1).

## 11.1 Eligibility (Confirmed behaviour, adopted)

- Only **memory sources** (decoded from the SLM) are hashed, dumped or
  replaced. Target views, target conversions, GLM sources, shuffles and
  temporaries are never replaced.
- A texture with TW or TH > 10 and no region on that axis is skipped before any
  lookup.
- Replacement happens at memory-source creation; a replaced source keeps its
  native logical size (2^TW x 2^TH or the region size) and scale 1; the GPU
  texture is the replacement's own size; sampling uses coordinates normalized
  by the native size, so any replacement resolution maps onto [0, 1].
- Region offsets, half-texel offsets and shader bilinear use native texel
  ratios (**Hypothesis**: a high-resolution replacement drawn through the
  shader-filtered paths is filtered as if native; test V-11-1 compares a 4x
  replacement drawn with REGION_CLAMP against the same texture drawn with plain
  CLAMP).
- Alpha is never rescaled: replacement alpha uses the PS2 scale (0x80 = 1.0,
  up to 0xFF ≈ 2.0). Its min/max (base level) feeds alpha-test and blend
  simplification.
- Indexed textures: the replacement replaces the **expanded** image (palette
  applied); a source that has a replacement for its (data, palette) pair is
  created as an RGBA texture instead of an index texture.

## 11.2 Format G (Decision)

**Key**: XXH3-64 (seed 0) over the logical texel stream of the base level, plus
XXH3-64 of the palette for indexed formats, plus the logical size.

- Logical texel stream = rows top to bottom, texels left to right, of the
  base-level rectangle (region if present, else 2^TW x 2^TH), with:
  - direct formats: 4 bytes R, G, B, A per texel after TEXA expansion (CT24/16
    alpha per TEXA; CT32 raw);
  - indexed formats: 1 byte per texel = the index (T4 low nibble = even texel;
    T8H/T4HL/T4HH extracted bits).
- Palette hash = XXH3-64 of the 16 or 256 decoded RGBA8 entries (chapter 07.5,
  i.e. TEXA applied for 16-bit palettes), 0 for direct formats.
- Mip levels are never part of the key; replacements supply their own mips
  (DDS) or HWR generates them (PNG) when `GT4_HW_MIPMAP` ≠ `off`.

**Files**: `<GT4_HW_TEXTURES>/SCUS-97328/gt4/` searched recursively, plus any
`.zip` there. Names: `t-<tex:016x>-<pal:016x>-<W>x<H>.<png|dds>` (lower-case hex,
zero padded to 16 digits, decimal size; pal = 0000000000000000 for direct
formats). Dumps go to `<root>/SCUS-97328/gt4-dumps/` with the same names.

**Why**: the key depends only on what the texture looks like at the native
resolution, not on block layout, base address, region offset quirks or emulator
mipmap settings, so dumps from any GT4Recomp configuration agree.

## 11.3 Format P: exact PCSX2-pack compatibility (Confirmed rules; implement later)

### Name record

| Field | Width | Meaning |
|---|---|---|
| TEX0Hash | u64 | XXH3-64 (seed 0, default secret) of the hashed texel bytes of the base level, followed by mip levels when mipmapping is active (11.3.4) |
| CLUTHash | u64 | XXH3-64 (seed 0) of the expanded 32-bit CLUT (16 or 256 entries, little-endian u32, entry 0 first); 0 for direct formats |
| region width, height | u32 each | width and height of the region on each axis, 0 when that axis has no region |
| bits | u32 | packed PSM/TW/TH/TEXA (below) |
| mip level | u32 | 0 for lookups; dumps of level N ≥ 1 use N |

Streaming XXH3 equals one-shot XXH3 of the concatenated bytes, so only the byte
sequence matters. Any conformant XXH3-64 (xxHash ≥ 0.8.0, whose output is
frozen) is acceptable; the reference bundles 0.8.3.

**bits** (LSB first): PSM in bits 0..5; TW 6..9; TH 10..13; bit 14 unused (0;
cleared when parsing); TA0 15..22; AEM 23; TA1 24..31. The TEXA fields are
non-zero only for CT24, CT16, CT16S, Z24, Z16, Z16S (direct formats that are
not 32-bit); for CT32, Z32 and all indexed formats they are 0.
Vectors: CT32 128x128 → 0x00001DC0; T8 128x128 → 0x00001DD3; T4 256x256 →
0x00002214; CT16 64x64 with TA0 = 0x80, AEM 0, TA1 = 0x80 → 0x80401982;
T4 16x16 → 0x00001114.

### 11.3.1 Region

Per axis (U shown; V the same with MINV/MAXV/TH):

- REGION_CLAMP with MAXU ≥ MINU: w = MAXU − MINU + 1; if w < 2^TW or
  MAXU ≥ 2^TW, the region is [MINU, MAXU + 1).
- REGION_REPEAT with MINU ≠ 0 (MINU = mask, MAXU = fix): w = ((MINU | MAXU) −
  MAXU) + 1; if w < 2^TW, or (MAXU ≠ 0 and w ≤ 2^TW), the region is
  [MAXU, (MINU | MAXU) + 1).
- The reference has three further region sources: an "estimate texture region"
  option (off by default), fixed CLAMP in some target paths (not hashed), and the
  upload-size inference of chapter 04.6 (sets [0, newW) x [0, newH) when the read
  is smaller than TW x TH under clamp-clamp and an exact upload happened within
  the last 10 batches). Format P must apply the upload-size inference exactly as
  chapter 04.6 states, or keys differ for those textures.
- Mipmapped draws: CLAMP values are shifted right by the base LOD before the
  region is built (High).
- Region width/height in the record = max − min (0 if no region). The region
  offset (min) is not in the key; it changes which texels are hashed.
- Hashed size per level i: region size >> i (at least 1) on region axes, else
  2^TW >> i (2^TH >> i).

### 11.3.2 Bytes hashed for one level

Let tw x th be the level's hashed size and rect = (minX or 0, minY or 0,
maxX or tw, maxY or th) relative to the base pointer; block_rect = rect expanded
outward to the PSM's block size (CT32/CT24/Z32/Z24/T8H/T4HL/T4HH 8x8;
CT16 family 16x8; T8 16x16; T4 32x16).

**Path A — raw blocks**, used only when all hold: tw ≥ block width and
th ≥ block height; the PSM is CT32, Z32, T8 or T4 (formats whose valid-bit mask
is all 32 bits); no X region, and the Y region (if any) starts at 0. Walk the
blocks of block_rect in raster order (block rows top to bottom, blocks left to
right) and feed each block's 256 raw bytes from GS memory at
(block number mod 16384)·256. Block number of block (bx, by):
TBP0 + 32·(pageY·bwPages + pageX) + table[by mod blocksY][bx mod blocksX],
then XOR 24 for Z formats, then mod 16384; pageX = bx / blocks-per-page-x,
pageY = by / blocks-per-page-y; bwPages = TBW >> (log2(page width) − 6)
(TBW/2 rounded down for T8 and T4; TBW 0 or 1 gives 0 pages per row for them).
The bytes are swizzled exactly as stored.

**Path B — expanded texels**, every other case:

1. Read block_rect into a linear buffer, one row per texel row:
   - indexed formats: 1 byte per texel = the index (T8H bits 24..31, T4HL
     24..27, T4HH 28..31; T4 expands each nibble to a byte, low nibble = even
     texel); no palette applied, whatever the palette mode;
   - direct formats: 4 bytes R, G, B, A: CT32/Z32 the stored word; CT24/Z24 low
     24 bits with A = TA0 (0 if AEM and RGB = 0); 16-bit formats R = (c & 0x1F)
     << 3, G = ((c >> 5) & 0x1F) << 3, B = ((c >> 10) & 0x1F) << 3, A = TA1 if
     bit 15 else TA0, A = 0 if AEM and the whole 16-bit value is 0.
2. Row pitch = (block_rect.right·bytes per texel) rounded up to 32 bytes;
   buffer column 0 is block_rect.left. (Pitch does not affect the hash.)
3. Start = buffer + pitch·(rect.top − block_rect.top) + **(rect.left −
   block_rect.left) bytes** — the offset is in bytes even for 4-byte formats
   (a quirk that Format P must reproduce: a direct-format region starting at a
   non-block-aligned X hashes rows starting dx bytes in, not 4·dx).
4. Feed th rows of tw·bytes-per-texel bytes each, advancing one pitch per row.

Consequences: tiny textures, all CT24/CT16/T8H/T4H* textures and all region
textures are hashed in unswizzled form; 16/24-bit hashes depend on TEXA, which
is why TEXA is in `bits`.

### 11.3.3 CLUT hash

Over the 32-bit expanded CLUT for this TEX0/TEXA (chapter 07.5 rules):
32/24-bit CPSM: 8-bit index → 256 entries, entry group g from halfword
min(g·16 + (CSA & 15)·16, 240), stored 32-bit value with **raw alpha** (no TA0
for CT24); 4-bit → 16 entries at (CSA & 15)·16. 16-bit CPSM: entries from CSA·16
expanded with TEXA. Length 64 bytes (16 entries) or 1024 bytes (256). The
CLUT hash is computed whenever the format is indexed and dumping or replacement
is active.

### 11.3.4 Mip levels in TEX0Hash

When mipmapping is active for the draw (the reference's default: hardware
mipmapping on; MXL > 0, MMIN 2..5, max LOD > 0), the base level is the level
chosen by the base-LOD rule of chapter 03.6 and the levels base+1 .. min(MXL, 6)
are appended to **the same hash stream**, each with its own TBP/TBW from MIPTBP1/2,
TW/TH reduced and the region shifted. Otherwise only level 0 is hashed. Packs
made with mipmapping off (older emulator builds) use base-only hashes for
mipmapped textures (**Hypothesis**); Format P lookups try the full-chain key
first, then the base-only key.

### 11.3.5 File names and folders

- Dumps: lower-case hex, hashes **without zero padding** (`%llx` style), bits
  as 8 zero-padded hex digits, region sizes decimal:
  - direct: `<tex>-<bits>.png`; indexed: `<tex>-<clut>-<bits>.png`;
  - with region: `<tex>[-<clut>]-r<W>x<H>-<bits>.png` (`r0x64`, `r256x0` are
    possible);
  - mip level N: `-mip<N>` before `.png`.
- Parsing replacements (basename only, case-insensitive hex, leading zeros
  allowed, a `.` must follow the bits): try in order
  `tex-clut-rWxH-bits.`, `tex-rWxH-bits.`, legacy `tex-clut-r<packed64>-bits.`,
  legacy `tex-r<packed64>-bits.` (packed64 = minX | maxX << 16 | minY << 32 |
  maxY << 48, converted to width = maxX − minX and height = maxY − minY as
  signed 16-bit), `tex-clut-bits.`, `tex-bits.`. Clear bit 14 of bits. Names
  with `-mipN` do not parse (mip PNGs in packs are ignored; mips come only from
  DDS). A file matches only if every field equals the runtime key
  (a CLUT hash on a direct format, or TEXA bits on CT32, never match).
- Folders: `<root>/<SERIAL>/replacements/` recursively (hidden files included),
  `.zip` archives found there (entry basenames only), then `<root>/<SERIAL>.zip`.
  SERIAL for GT4 NTSC-U = `SCUS-97328`. First registered name wins (packs must
  not rely on order). Dumps go to `<root>/<SERIAL>/dumps/`.

Format P test vectors: CT32, TBP0 0x1000, TBW 2, TW = TH = 7, no region, no
mips → path A, 256 blocks x 256 bytes in raster block order with bwPages 2,
name `<hash>-00001dc0.png`; T4 16x16 → path B, 256 index bytes, CLUT 64 bytes,
name `<texhash>-<cluthash>-00001114.png`. End-to-end vectors (hash values) must
be produced by the owner running PCSX2 with dumping enabled on a GS dump replay
of a GT4 scene (open question Q-07): the names PCSX2 writes become CTest
expectations for HWR replaying the same capture.

## 11.4 Image formats (Confirmed for Format P; adopted for both)

- PNG: 8-bit RGB (alpha := 0x80) and RGBA (copied as is). Palette, grey or
  16-bit PNGs are rejected with a log line (the reference silently produces
  garbage; HWR refuses).
- DDS: legacy FourCC DXT1 → BC1, DXT2/3 → BC2, DXT4/5 → BC3; DX10 header with
  BC1/BC2/BC3/BC7 UNORM (sRGB variants and BC4/5/6 rejected); BC base level
  dimensions multiples of 4; uncompressed masks: A8R8G8B8 (swizzle to RGBA),
  A8B8G8R8 (as is), X8R8G8B8 (A := 0xFF), X8B8G8R8 (A := 0x80), R8G8B8
  (A := 0xFF) — these alpha inconsistencies are part of Format P compatibility;
  Format G uses A := 0x80 for every alpha-less input. Mip count from the header
  (0 = full chain), levels read sequentially until a read fails. Compressed
  formats require `textureCompressionBC` (otherwise decode on the CPU).
- No size limit relative to the native texture.

## 11.5 Load pipeline (Decision; behaviour Confirmed)

- At start (and when the game serial changes): scan folders, build a map from
  key to file location. Log the count.
- At memory-source creation, if replacement is on: compute the key; look up.
  - Decoded image in the CPU cache → create the GPU texture now.
  - Else queue an asynchronous load (worker thread; on-demand loads ahead of
    precache loads) and use the normally decoded texture as a placeholder.
  - At each VBlank, finished loads are uploaded and **injected**: the hash-cache
    entry with that key gets its texture swapped, every source using it is
    updated, alpha range replaced; if the entry was evicted, a new one is
    inserted.
- `GT4_HW_TEXTURES_PRECACHE=1` queues every file at start as low-priority
  CPU decode (memory use can be large; logged).
- The decoded cache is cleared on reload; failed loads are dropped (no retry
  until reload).

## 11.6 Dumping (Decision; behaviour Confirmed for Format P)

`GT4_HW_DUMP_TEXTURES=1|mips` (chapter 12.8; `GT4_HW_DUMP_FORMAT=g|p`, default
g): at every memory-source key computation, if not already dumped this session
(in-memory set) and no replacement with that name exists and the file does not
exist: decode the level to RGBA8 **with the palette applied** and write PNG
(8-bit RGBA, zlib level 1) on the worker thread. Format P mip dumps use the
**unadjusted base region** for mips (quirk preserved for compatibility).

## 11.7 Counters (F8)

Keys computed, replacement hits, pending loads, injected textures, replacement
MiB, dumps written, hashing time.

## Sources

- Research report `texture-replacement.md` (all sections).
- Research report `texture-cache-v2.md` 1.4 and 3.6 (hash cache, size
  inference).
- Research report `upscaling-clut-readbacks.md` B.4 (CLUT decode).
- Research report `gt4-gamedb-hacks-donor.md` 1.2 (GT4 profile does not change
  mipmapping, so default-setting packs use full-chain hashes).
- xxHash public specification (XXH3-64, seed 0).
