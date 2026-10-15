# 0068 — Build the game code on the user's PC; add the PCSX2 hardware GS renderer

Date: 2026-10-15. Status: **Superseded** by [0069](0069-own-hardware-renderer-clean-room.md) the same day. Amends
[0067](0067-mit-license-and-release-model.md).

**Context.** paraLLEl-GS (TASK-012) runs GT4 at full speed (night race 60 FPS),
but its high-resolution output is capped at 2x and it has no texture
replacement. The owner judged its image quality well below PCSX2's. The PCSX2
hardware GS renderer (GSdx lineage) offers 3x-6x internal resolution, texture
replacement and GT4-specific fixes, but it is GPL-3.0+
([GSDX-STUDY](../GSDX-STUDY.md) on branch `task/012-parallel-gs`). Under 0067 the
release executable contains translated game code, so it cannot contain GPL code.

**Decision.**
- **Distribution becomes plan B:** the installer ships our MIT runtime, the
  translators, the pinned generated-code recipe and a compiler toolchain. The
  game code (EE units, VU1/VU0 programs) is generated and compiled on the user's
  PC from the user's own disc during first-run setup. No translated game code is
  distributed.
- **The PCSX2 hardware GS renderer is added as a renderer option** (new
  TASK-026). It is linked into the executable built on the user's machine, so
  that local build is GPL-compatible. Our MIT code is GPL-compatible; the
  distributed PCSX2 sources keep their GPL notices and their source stays
  available.
- **paraLLEl-GS stays** as another renderer option (LGPL), and the donor
  renderer stays as the fallback.
- Branding (official Gran Turismo 4 art, Windows name "Gran Turismo 4") is
  unchanged from 0067.

**Consequences.**
- The launcher first run becomes "check disc → generate and compile the game
  (several minutes) → ready" (TASK-019, the `GamePayload` build-from-disc path
  of `docs/launcher/LAUNCHER-PLAN.md`). The installer grows by the toolchain
  (about 190 MB unpacked) and must not depend on non-redistributable Microsoft
  runtime libraries (self-contained build or a redistributable runtime).
- The EE producer must become a reproducible, end-to-end, clean build from the
  user's disc (today production uses a pinned private ps2game.lib).
- Mobile (TASK-021/022/023/024): on-device compilation is not practical on
  phones; the mobile licensing route for the GPL ARMSX2 pipeline stays open.
