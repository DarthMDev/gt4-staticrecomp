# Conditional VIF packet framing

Slice 123 adds original offline `scripts/gt4_vif_framing.py` and hand-authored
controls before extracting any shape channels. `frame_packet(bytes,
initial_cycle=(CL, WL))` bounds standard VIF command spans. It does not execute
VIF/VU, reconstruct geometry, interpret GIF, or replace the existing runtime.
Evidence: [slice 123 metadata](inputs/slice123-vif-framing.json).

## Contract and explicit assumption

The caller must supply initial CL/WL in 1–255. These are framing assumptions,
not defaults established for the live model pipeline. Zero-field cycle semantics
and VL=3 UNPACK forms are rejected. VL=3 includes valid packed V4-5, deliberately
outside this subset; it is not uniformly a reserved encoding. A complete packet under one
assumption does not prove the incoming hardware state or the VU result.

VIF words contain LE u16 immediate, u8 NUM and a byte with 7-bit opcode plus IRQ.
The supported subset includes known no-payload commands, STMASK/STROW/STCOL,
MPG, DIRECT/DIRECTHL and ordinary 32/16/8-bit UNPACK forms. Commands consume
four header bytes followed by word-aligned payload. Unknown opcodes, unqualified
UNPACK forms, command-count excess and packet overrun reject explicitly.

| Payload rule | Framing behavior |
|---|---|
| STMASK | 4 bytes |
| STROW / STCOL | 16 bytes |
| MPG | NUM × 8; NUM 0 expands to 256 |
| DIRECT / DIRECTHL | Immediate × 16; immediate 0 expands to 65,536 QW |
| Ordinary UNPACK | Source vectors × components × 4/2/1 bytes, padded to a word |
| UNPACK NUM 0 | 256 destination vectors |
| Fill cycle WL > CL | Only rows with index modulo WL < CL consume a source vector |
| Skip cycle WL ≤ CL | All destination vectors consume input |

STCYCL updates the framing cycle from its immediate. It has no payload, but
its fill mode **does change later source byte consumption**. The initial worker
report incorrectly described this as destination-only. The parent caught and
corrected it against the pinned MIT donor's `unpack_words` implementation.
The report also misstated V4-5 as 32-bit storage; the donor uses a packed 16-bit
word. Packed forms are deliberately unqualified/rejected in this public subset.
Frozen research is retained with errata; it is not adopted as an acceptance oracle.
[Decision 0063](decisions/0063-explicit-vif-cycle-assumptions.md) explains the
parent's controlled conditional scope beyond the worker's deferral recommendation.

## Selected observations and limits

All 466 selected shape packets from [slice 122](MODEL-RECORDS.md) begin with
word `0x6801C0C0`: ordinary V3-32 UNPACK of one 12-byte vector. Nenkai's model
reader gives that immediate a special GIF interpretation. This probe uses the
standard 12-byte length and makes no GIF interpretation or alternate payload rule.

**Confirmed, conditional on CL/WL=(1,1):** all 154 garage and 312 racing packets
reach their exact declared QWC end, with no truncation, unknown opcode or packed
UNPACK rejection. The encountered opcodes are NOP, STMOD, STMASK, STROW and
ordinary UNPACK variants. No STCYCL appears in these packet spans, so **the
incoming cycle state remains material and Unknown**. Data ownership, channel
meaning, topology, VU execution and live camera conventions remain unqualified.

Only exact frozen car payloads are read after published decoded hash/size checks;
fresh private slice 123 outputs contain derived framing metadata. Packet payload
bytes are not distributed. Input cap is 1 MiB per packet and 65,536 commands.
NOP padding is parsed explicitly; no guessed end marker skips unexplained bytes.

Primary pinned code sources: MIT
[donor vif.cpp](https://github.com/snuri00/ps2-web-recomp/blob/1b4808793a12c45125262fd2d5f50a9ec9f7c020/runtime/src/vif.cpp),
[Nenkai VIFCommand](https://github.com/Nenkai/PDTools/blob/ffe0bb26377ac9ff62f40951c1c0b16c5d9a380f/PDTools.Files/Models/PS2/VIFCommand.cs)
and [model template](https://github.com/Nenkai/GT-File-Specifications-Documentation/blob/05e52347890541758222d173dbb62d2b40581b19/Formats/PS2/PS2ModelCommon.bt).
Donor source observations are not an independent PS2 fidelity oracle. No source
implementation is copied or linked by this offline tool.

## Controls and next boundary

Eleven public controls cover IRQ/no-payload words, fixed payloads, variable
widths/alignment, zero counts, fill/skip cycles, marker framing, MPG/DIRECT units,
explicit cycle preconditions, unknown/packed opcodes, truncation and work caps.
Baseline CTest passes 3/3; Python 120/120, zero skips. No native rebuild.

Next qualify where the game establishes incoming cycle/mode and how the model
format maps payloads to channels. Then decode a bounded channel/topology subset
with independent fixtures and reference comparisons. Framing alone cannot justify
native car substitution, high-refresh rendering or any performance claim. Owner
session, OpenCode, runtime, input, saves and timing remain unchanged.

[Slice124 static state research](VIF-STATE-CONTRACT.md) finds locally buffered
0/4 then 4/4 cycle commands and the shape helper's conditional matrix/state
effects. It does not qualify their live channel or the initial-cycle assumption.
