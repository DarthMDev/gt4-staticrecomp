# 0060 — Qualify EE integration with separate baseline and recovered runtimes

Date: 2026-10-13. Status: accepted within authorized P0/P1 scope.

## Context

Slice 116 reproduced the EE library; slice 117 recovered one bounded callback
pair offline. Compilation of changed units did not establish complete archive
membership, runtime linkage or gameplay. The owner authorized separate candidate
builds while Luna continues one-car research and OpenCode owns performance work.

## Decision

Create a fresh slice 118 capsule. Use the verified rebuilt baseline library as a
control, and compile the recovered source into a complete separate library with
the unchanged donor/toolchain. Require all 327 members, exactly 18 reviewed object
differences and 309 unchanged objects under timestamp normalization. Compare the
copied baseline directly against its authoritative producer; mutable receipts
cannot repin it. Keep the default production hash unchanged.

Link and test both runtime profiles in separate caches. Route existing Python
native fixtures to each actual candidate without weakening assertions. Publish
metadata-only library/executable/shader pins, with owner gameplay still pending.
Keep executable/shaders in the managed checkout and ISO/card in the assets checkout;
reuse both existing locks and card backup rules. Preserve active owner sessions.

## Consequences and limits

Both native profiles pass CTest 3/3 and Python 59/59, with zero skips/warnings.
This qualifies archive delta, runtime linking and native fixture behavior, not
game boot, racing, EE semantics or PS2 fidelity. No performance gain is claimed,
and the OpenCode performance branch is not merged into these candidates.
The owner tests baseline then recovered through normal gameplay.

Luna/parent bounded volume research qualifies header/table metadata only. Page
decoding and named-car mapping remain open; no copied parser implementation or
bulk asset extraction. P0/P1 remain in progress; no dedicated renderer, timing,
input fix or production switch follows from this decision.

Evidence: [EE Candidates](../EE-CANDIDATES.md),
[Car Asset Research](../CAR-ASSET-RESEARCH.md),
[library metadata](../inputs/slice118-ee-libraries.json).
