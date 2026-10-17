# 0071 — Native renderer on a separate branch of this repository

Date: 2026-10-17. Status: accepted (owner). Narrows the "native (non-GS) renderer is out of scope" line in
CLAUDE.md.

## Context

- A read-only feasibility study ([native-renderer/STUDY.md](../native-renderer/STUDY.md)) shows that GT4 sends every
  3D mesh through one PGL call (`pgluCallShape`), and that each race frame splits cleanly into the 3D world pass
  and the 2D/HUD passes.
- A native renderer fed at that call can render at any resolution and above 60 FPS, and can support true
  ultrawide. That is the freedom the GS path cannot give.
- The study suggested a fork with a renderer plugin.

## Decision

- The native renderer is developed **in this repository**, on the long-lived branch `native-renderer/main`, not
  in a fork. `main` keeps the GS path (PCSX2 GS, CoreGS 1.1) as the shipping renderer.
- It is **hybrid**: menus, 2D, HUD and movies stay on the GS. The 3D world moves to the native renderer piece by
  piece, each piece behind shadow mode and image-diff gates, with the game playable throughout.
- It is designed from the start for high frame rates with untouched physics, ultrawide, any resolution, and mobile.
- Work is planned as TASK-065 to TASK-073 ([native-renderer/PLAN.md](../native-renderer/PLAN.md)). It starts after
  release 1.0.0.
- MIT only, by untainted agents (no PCSX2 sources, no celosia source code).

## Consequences

- `main` and its releases are unchanged by this decision.
- Renderer-neutral infrastructure (hook registry, scene capture, GS layer markers) may later be proposed to `main`.
- Whether the native renderer ever ships from `main` is a later owner decision.
