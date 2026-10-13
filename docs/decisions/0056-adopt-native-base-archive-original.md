# 0056 — Adopt native base and archive original engine

2026-10-13; explicit owner request, baseline 95f6155.
Status: accepted; current build and gates green, slice 114.

## Decision

Preserve the prior versioned state on origin/codex/archive-original-recomp-20261013
before removing legacy code from main. Use ps2-web-recomp/MIT + our native port
as the sole active path. Move adapters to native/, replace root CMake
with current targets, and retain only tests that cover the maintained code.
Preserve inputs/saves/private evidence and historical producers at their paths.

The owner requests an understandable project and a single development path.
Maintaining two active engines and a README describing an engine that no longer runs the pilot
confuses the build, state, and evidence. The branch preserves the original independent work
for reference without requiring its maintenance across every new delivery.

## Limits and trade-offs

We adopt the donor's execution dependencies: interpreted IOP/VU and software
PS2 hardware models. Native executable/EE/Vulkan does not mean zero emulation.
Contracts/tests from the old engine remain in the archive; they do not automatically validate
this different environment. Fidelity/stability/performance remain open.
Current build reuses private hash-pinned ps2game.lib; full generation from
ISO is not yet an automated public recipe.

No timing/physics/input/rendering adjustments were authorized in this cleanup.
The prior request to only record performance and duplicate keys remains in effect.
Path/manifest updates must preserve validations for inputs, shaders,
seeds/backups/locks, and shutdown criteria; new outputs do not overwrite 113.

## Required verification

Archive branch published and pointing to baseline; new build warning-free;
current CTest/Python green; nine SPIR-V shaders identical to producer 113;
frozen runtime/game-library/seeds/inputs preserved; imports/build free of dependencies on the
removed engine; README/AGENTS/STATUS and claim provenance updated. Report 114
records the counts, hashes, limits, and closure of this decision.
