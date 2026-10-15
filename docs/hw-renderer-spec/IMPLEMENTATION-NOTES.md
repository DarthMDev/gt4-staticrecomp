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
