# 0069 — Own MIT hardware GS renderer by clean room; prebuilt release again

Date: 2026-10-15. Status: Accepted (owner decision). Supersedes
[0068](0068-build-on-user-pc-and-pcsx2-gs.md); restores the release model of
[0067](0067-mit-license-and-release-model.md).

**Decision.**
- **Release model:** the installer ships the prebuilt game executable (with the
  translated game code) as in 0067. Compiling on the user's PC is dropped.
- **No PCSX2 code in our renderer.** The PCSX2 hardware GS renderer (GSdx
  lineage) is GPL-3.0+ and cannot be combined with the game code in a
  distributed executable.
- **We write our own hardware GS renderer under MIT**, aiming at the PCSX2
  hardware renderer's feature set (internal resolution 1x-6x and beyond, texture
  cache, texture replacement packs, GT4 fixes, post-processing), by a
  **clean-room process**:
  1. A *specification team* (TASK-026, study only) reads the PCSX2 renderer and
     public documentation and writes a behavioral specification in plain
     English: data flow, algorithms, state machines, heuristics, formats and
     test cases. No code, and no pseudocode copied or translated from PCSX2;
     identifiers only where needed to name a concept.
  2. An *implementation team* (TASK-027) writes the renderer from the
     specification only. Implementers never open PCSX2 GS sources; the PCSX2
     checkout is off limits for TASK-027 agents. Questions go back to the
     specification team, which answers in prose.
  3. Every specification document records its sources; every implementation
     commit records that it was written from the specification.
- **Renderer options:** donor compute renderer (fallback, MIT), paraLLEl-GS
  (LGPL, DLL), and the new MIT hardware renderer once it qualifies.
- Branding, MIT license, GitHub Releases and the "Gran Turismo 4" name are
  unchanged from 0067.

**Why.** The owner wants to ship the compiled game and let the user pick the
renderer, with PCSX2-level image quality (3x-6x, texture packs). paraLLEl-GS
caps high-resolution output at 2x and has no texture replacement; the PCSX2
renderer itself is GPL.

**Limits (not legal advice).** Clean room reduces but does not remove
copyright risk; the specification must describe behavior and ideas, not
expression. GT4-specific behavior should be derived from the game and the GS
hardware documentation where possible.
