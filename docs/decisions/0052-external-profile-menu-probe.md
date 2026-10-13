# 0052 — Qualify profile entry and menu in the external pilot

Date: 2026-10-12. Status: accepted for instrumentation and isolated pilot;
navigable menu observed in pilot, runtime adoption not yet decided.

## Context

Decision 0051 confirmed the first guest screens on Windows using the complete
external MIT runtime and our inputs. The owner authorized continuation.
The prior unnamed entry did not allow measuring progress to the menu.

## Decision

Use the donor's existing input mechanism in controlled coldboots with
fresh private memory cards. Record screens, effects, and stop reasons.
Add only a reproducible launcher to the fork, reusing fingerprinting
and the existing Python suite; no donor runtime code is transplanted.

The launcher validates hashes, including the full ISO on each launch and the ELF
copy, refuses to overwrite evidence, and distinguishes frame-limit from exit0
following a guest stop. Execution acceptance is kept separate from visual
menu acceptance and future service comparison against an independent reference.

## Alternatives and consequences

Modifying guest instructions to skip profile creation loses proof of
input/card handling; a replay loses proof of execution. We maintain the
real sequence, even if this requires qualifying confirmations on a fresh card.
Software GS is sufficient for this probe; Win32/GPU/Vulkan follow in another slice.

The runtime retains the simplifications audited in 0051. Real captures and
responsive menus do not automatically qualify clock timing, VU, default-response
services, reread persistence, racing, or broad stability.
Production MSVC and its contracts/checkpoints remain preserved.

## Evidence

`docs/reverse-engineering/slice110-external-menu-pilot.md` and manifest
`docs/inputs/slice110-external-menu-pilot.json` record controls, pins,
and results. The menu will only be marked confirmed after observed screens and input.
