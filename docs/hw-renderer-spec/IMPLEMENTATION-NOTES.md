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
| IN-07 | M7 cache | Block ranges (04.1) are not monotone inside a page (block numbers follow the in-page table), so a [first, last] range can miss blocks. The range rules are implemented and tested as written, but conflict decisions use exact block sets. Risk for the spec team: ranges are only a coarse test. | to confirm (risk) |
| IN-08 | M7 cache | Draws are clipped to FBW x 64 (valid width, 05.2). Pixels beyond it alias the next row in GS memory, which a one-word-per-pixel target cannot represent. The reference model clips the same way. | to confirm |
| IN-09 | M7 cache | The 04.1 dirty re-expression formula is implemented and tested by its vector, but the cache does not rely on it: dirty areas come from exact block footprints in target coordinates. The scaled formula is not a byte superset across layouts with different block shapes. Risk for the spec team. | to confirm (risk) |
| IN-10 | M7 cache | Channel validity is kept per target. A 16-bit format is one channel (any write covers the halfword). A channel that becomes valid is loaded over the whole valid area, not only the dirty rectangle, otherwise pixels outside it keep garbage in that channel. | to confirm |
| IN-11 | M7 cache | An upload or move destination over drawn data first writes back the drawn part of its footprint, then records the footprint as dirty. Spec 05.6 dirties without a write-back; exact dirtying is a later optimisation. | to confirm |
| IN-12 | M7 cache | Every draw is treated as preserving: the valid growth area is loaded from the SLM. The full-overwrite, clear and partial-coverage shortcuts (05.3, 05.8) are not in M7. | to confirm |
| IN-13 | M7 cache | A target whose memory footprint meets another target's valid area is written back and deleted, instead of the offset, draw-before and reconcile methods (05.3 steps 2 and 3, 05.4). The cache never holds two targets on the same memory. | to confirm |
| IN-14 | M7 cache | Target creation does not preload: the valid area is the draw rectangle, so no SLM data is needed. The transfer-log preload (05.4) is not used in M7; growth loads from the SLM directly. | to confirm |
| IN-15 | M7 cache | Target-view sources only for an exact layout, a read inside the valid area, all channels valid and nothing dirty in the read. Otherwise overlapping drawn data is written back and a memory source is decoded (chapter 00 reference mode; GLM is M10). Memory sources match on an exact key; no hash cache (04.7) in M7. | to confirm |
| IN-16 | M7 cache | Read shortcut (10.2): any exact upload record in the log is enough. Draws drop log records whose blocks they touch, so the shortcut stays safe. The "since last batch" window is not applied. | to confirm |
| IN-17 | M7 cache | Readback selection (10.3): exact layout first (intersection with the read), then overlapping targets (whole drawn area, no translation); the whole drawn area when a target was read more than once since its last draw; stop after an exact target that holds the read. No depth candidates in M7. | to confirm |
| IN-18 | M7 cache | Ageing (04.10) as written for sources and targets. The "another target overlaps" deletion branch never applies because targets never share memory (IN-13). Hash-cache and size-memory ageing are deferred. | to confirm |
| IN-19 | M7 cache | Budget: usage counts 4 bytes per target image pixel and per memory-source pixel. Order: drop all sources, then write back and delete the least recently used targets that are not displayed and were unused for 2 VBlanks. The hash-cache step is absent. | to confirm |
| IN-20 | M7 cache | Display lookup (05.10) implements step 1 (exact base containing the rectangle) and step 5 (SLM after writing back overlapping drawn data). Steps 2 to 4 are deferred. | to confirm |
| IN-21 | M7 cache | V-10-1 is tested at 1x (alpha-invalid write-back of an RGB-only draw). The 2x point-sampling part needs scaled targets (M6/M9). | to confirm |
| IN-22 | M7 cache | Block range: an inverted pair is swapped when the inversion is less than 32 blocks (the spec says "within 32"). Rectangles whose right and bottom edges are page aligned end at the last block of the bottom-right page (page base + 31). | to confirm |
| IN-23 | M7 cache | Dirty re-expression aligns outward to the target block size (the writer block mapped into target coordinates). The vector is satisfied. | to confirm |
| IN-24 | M7 cache | CLAMP region rule (04.3): the REGION_REPEAT row has no vector in the spec; test rows are derived from the table. | to confirm |
| IN-25 | M7 cache | Translation (04.1): implemented for the same layout family with a whole-page, non-negative base difference (page-aligned rectangles when the formats differ). Block offsets, negative offsets and the surface-offset search are deferred. | to confirm |
