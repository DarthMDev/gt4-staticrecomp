# 0057 — Plan a game-scene renderer and staged native port

2026-10-13. Planning baseline `88c0e96`.
Status: proposal; owner goals/initial performance target confirmed, executable
phases not started or qualified by this revision.

## Context

The owner wants a GT4 PC remaster: progressive internal resolution, ultrawide,
high-refresh graphics independent of original physics, upgraded textures/effects,
eventual modern lighting, native controllers/wheels and modest hardware support.
Initial selected target: Windows with an entry-level dedicated GPU, 1080p/60.
Exact hardware and achievable performance remain unknown.

Current EE AOT/native Vulkan still depends on interpreted IOP/VU microprograms
and PS2 hardware models. No Windows JIT is active. AOT source matching and native
API usage do not prove full portability or elimination of those models.

## Proposed direction

Use qualified GT4 model/camera/material boundaries to feed native geometry into
a lightweight forward Vulkan renderer. First establish reproducible EE generation
and one-car contracts; prove one live showroom replacement, then racing. Extend
progressive/resolution/time separation, cover all graphics, migrate game-facing
services and remaining computations, then retire hardware models and guest-core
dependencies. Deferred rendering is optional later work with a lightweight preset.

See [Native Port Plan](../NATIVE-PORT-PLAN.md) for P0-P10, dependencies, risks,
claim gates and qualification. It supersedes the ordering of the earlier
[timing roadmap](../ROADMAP.md); that analysis remains useful technical context.

Do not require 100% EE decomp before the first renderer milestone. VU0 macro
operations are already translated with EE; VU microprograms need separate
contracts. Bypass qualified graphics work rather than first implementing a
general-purpose replacement VU/GS machine. Preserve non-graphics calculations
and state-changing callbacks. GT4 content/script VMs are not PS2 CPU emulation.

## Engineering workflow

Use economical workers for bounded evidence/tooling/approved-contract tasks.
Use stronger coordination and independent semantic review for ambiguous hooks,
timing, numerics and concurrency. Qualify models locally; do not assume provider
equivalence or unlimited usage. One integration writer, serial build/test, private
outputs and preserved input/producer/save pins. See [Agent Workflow](../AGENT-WORKFLOW.md).

## Limits and validation

The architectural advantage is high confidence; performance gains and hook
correctness remain unknown until controls and owner-operated tests establish them.
Retain the hybrid reference separately, but hidden fallback cannot satisfy native
release claims. Removing interpreters and removing hardware models are separate gates.

Only documentation and read-only reviews were performed. No build, profiling,
game launch, synthetic input or navigation. Existing OpenCode performance edits
remain untouched; its task is not cancelled. No provider, scheduler or external
worker execution configured. New executable work requires a bounded task package.
