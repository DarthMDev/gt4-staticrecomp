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
