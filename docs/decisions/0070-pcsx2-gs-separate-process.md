# 0070 — PCSX2 GS as the release renderer, in a separate GPL process

Date: 2026-10-15. Status: Accepted (owner decision, 2026-10-15: all recommendations of
[GSDX-PLAN](../GSDX-PLAN.md) approved; answers to its section G recorded below). Limits
[0066](0066-gpl-reuse-accepted.md) and [0067](0067-mit-license-and-release-model.md) (paraLLEl-GS as a
shipped DLL), and [0069](0069-own-hardware-renderer-clean-room.md) (renderer options). Drops the
paraLLEl-GS option named in those decisions. [0068](0068-build-on-user-pc-and-pcsx2-gs.md) stays
superseded by 0069.

**Context.** paraLLEl-GS (TASK-012) runs GT4 at full speed but its high-resolution output is capped at 2x
and it has no texture replacement; the owner judged it too limited. CoreGS 2.0 (TASK-026/027, our MIT
clean-room hardware renderer) is paused and will take months. The PCSX2 hardware GS renderer (GSdx
lineage) has the full feature set the owner wants (upscaling to 8x and beyond, texture dump/replace, post
shaders, FXAA, CAS, filtering, dithering, deinterlace modes, shader cache, GT4 GameDB fixes) but is
GPL-3.0+, and our release executable contains translated game code (0067, 0069).

**Decision.**
- **Release renderer:** the PCSX2 GS, run as a separate process **`gt4-gs.exe`** (working name), built from
  our public fork **`github.com/moesuito/gt4recomp-gs`** (the fork is created in TASK-033). The release is
  based on PCSX2 stable **v2.8.x**; the proof of concept (TASK-034) may use the 2.9.x nightly at `685637d`.
- **Renderer order (Windows):** PCSX2 GS (downloaded; default when present) / **CoreGS 1.1** (MIT, built
  in; the fallback and the offline default) / **CoreGS 2.0** (planned; greyed out until it qualifies).
- **paraLLEl-GS is dropped from the release.** Its LGPL code is not merged. Branch `task/012-parallel-gs` is
  archived. Only its MIT renderer seam and capture code are salvaged (into TASK-035).
- **Two executables.** The game executable (MIT code plus translated game code) never contains or links GPL
  code, directly or through a DLL. `gt4-gs.exe` is built from the fork (GPL-3.0+) and published only as
  GitHub Releases of that fork, with its full corresponding source, license and notices.
- **Distribution:** the MIT installer does not bundle `gt4-gs.exe`. The launcher downloads it during setup
  (one click; on by default) from the fork's releases, pinned by version and SHA-256, and can update it to
  a newer pinned version.
- **Interface:** the processes exchange only a generic GS stream over shared memory: GIF packets per path,
  privileged GS register writes, vsync with field, and local-to-host FIFO readback requests and replies,
  plus control messages (resize, config reload, snapshot, shutdown). This is the same data as a PCSX2 GS
  dump and as our `.gscap` capture. The protocol header is MIT and public. SIGNAL/FINISH/LABEL, CSR and IMR
  stay in the game process. No game code, guest RAM or CPU state crosses the boundary.
- **Window:** `gt4-gs.exe` renders into its own child window inside the game window; input, focus, fullscreen
  and frame pacing stay in the game process (approved). Fallback: a GS-owned window with input forwarding,
  behind a switch (TASK-038).
- **Readbacks:** accurate by default. "Unsynchronized" is offered as a speed option, not the default.
- **Internal resolution:** the default is **3x**.
- **Kept:** MTVU (VU1 worker) on the game side; the game process remains the clock owner.
- **No JIT:** `gt4-gs.exe` runs no guest code. A hardening task (TASK-042) builds it without the PCSX2
  recompilers, RWX memory and the software scanline JIT. **Required before release 1.0.0** (owner).
- **Go/no-go gate:** after the proof of concept (TASK-034) the owner decides whether to invest in TASK-035
  and TASK-036. The gate is recorded in [GSDX-PLAN](../GSDX-PLAN.md) section F.
- **Clean room:** agents that read PCSX2 or the fork are PCSX2-tainted and never work on TASK-027. TASK-027
  agents never open PCSX2 or the fork. They may use `gt4-gs.exe` final frame images only, as a black-box
  reference (owner, answer 9); never its debug dumps or logs. The wording is in CLAUDE.md.
- **Later phases:** TASK-045 (high frame rate) is redone on top of the PCSX2 GS after 1.0.0 (TASK-031 is
  paused). Mobile renderer routes stay open (GSDX-PLAN E.4).

**Why.** It gives release 1.0.0 the PCSX2 feature set now, keeps the prebuilt game executable of 0069, and
keeps GPL code out of it. PCSX2 already replays GS streams (`GSDumpReplayer`, `pcsx2-gsrunner`), so the
fork diff stays small and can follow upstream.

**Consequences.**
- New tasks TASK-032..044 (decision docs, fork, PoC replay, protocol, live MVP, readbacks, window, settings,
  textures, performance, no-JIT hardening, launcher download, packaging) and TASK-045 (high frame rate, after
  1.0.0). Workstream W8 in [RELEASE-1.0.0-PLAN](../RELEASE-1.0.0-PLAN.md).
- The launcher gains a renderer download, verification and update step; the dev launcher pins
  `gt4-gs.exe` in `docs/inputs/`.
- Each night-race readback becomes a cross-process round trip; measured in F8 (`[gs-ipc]`).
- TASK-019 (launcher) is paused until the GS work is ready to integrate (TASK-043).

**Limits (not legal advice).** Separate processes, separate distribution and a generic hardware-level
protocol are the usual arm's-length arguments, but they are arguments, not a guarantee: a tightly coupled
pair built for one game could still be viewed as one combined work. The owner accepts this risk as with
0067. Keep GT4-specific logic out of the protocol and never copy PCSX2 code into MIT files. iOS cannot
spawn executables, so this model does not carry to phase 3 as is.
