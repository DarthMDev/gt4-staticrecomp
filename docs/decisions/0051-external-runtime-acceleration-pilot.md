# 0051 — Prioritize an execution pilot with an external runtime

Date: 2026-10-12. Status: accepted for study and isolated pilot;
replacement of the production runtime not yet decided.

## Context

The owner requested studying gt4-decomp, ps2-web-recomp, and PS2Recomp prior
to the next slice to significantly accelerate the path to a functional game.
Production remains at the timing/interrupt frontier of slices 96–109.
The objective is stable gameplay before broad manual decompilation.

## Decision

Prioritize the complete MIT ps2-web-recomp runtime in an experiment with
our hash-pinned CORE/disc, using separate private identity and files.
Use gt4-decomp as a semantic map: its published CORE hash matches
our input, and its expanded inventory explains the more than 30,000 targets.
Use PS2Recomp as a reference for interfaces/tests/Windows, observing
GPLv3 and previously identified contract divergences. EE Reloaded is an
analysis aid; it is neither a prerequisite nor a replacement for the execution environment.

Do not alter the timing/IRQ/checkpoint policy of the current runtime to mimic
the external runtime. Do not adopt generic success responses as a fix for
unknown services. The pilot must reveal and record these cases.

## Alternatives and consequences

Continuing all small slices on the current base preserves maximum contract
continuity, but requires implementing many subsystems that already exist in
the other project. Migrating immediately loses the independent reference and
assumes an NTSC/Windows boot not yet observed. The limited pilot preserves
existing evidence and tests broader gains with minimal new code.

Any eventual integration must reuse coherent subsystems, including
timing/IOP/transport, rather than combining incompatible models via handlers.
Direct Vulkan, XInput, audio, and real saves remain product goals; the
wgpu-native path is a test/reuse foundation, not certification that
these goals are already met on Windows.

## Acceptance and review

The study concludes with clones/pins, count explanations, audited sources, and
reproducible experiments. Choosing the final base requires a cold boot,
first guest screen, navigable menu, and identification of services actually
used. Racing, saves, and fidelity have separate gates. Review this decision
once the initial pilot reports either a blocker or concrete progress.

Evidence: `docs/reverse-engineering/external-recomp-acceleration-2026-10-12.md`
and `docs/inputs/external-recomp-references.json`.
