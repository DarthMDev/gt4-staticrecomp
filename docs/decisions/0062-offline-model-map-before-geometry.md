# 0062 — Resolve a named model before geometry migration

Date: 2026-10-13. Status: Accepted for bounded P1 research.

Use CAR_VARIATION → VARIATION → localized ModelCode rather than guessed filename
prefixes. Preserve repeated physical DBT IDs and verify all selected row hashes
with the independent code-list decoder. Extract only the resulting two exact
menu/LOD leaves into a fresh private capsule after unchanged ISO verification.

Adopt an original header-only CAR4/MDLS/Tex1 probe with public hand-authored
controls. Actual bytes reveal signed -1 VM words and zero-size texture headers:
record unqualified fields without dereference and bound the exact observed
header-only subset. Do not reinterpret unknown VM fields as container pointers
or infer a texture body size. Detailed subrecord, geometry and live contracts
remain separate gates; a parsed header is not a renderable car.

Evidence and limits: [Model Asset Probe](../MODEL-ASSET-PROBE.md) and
[slice 121 metadata](../inputs/slice121-model-file-map.json). Runtime, existing
producers, owner gameplay and OpenCode's performance branch remain unchanged.
