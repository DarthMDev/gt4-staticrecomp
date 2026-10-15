# 0066 — GPL-3.0 code reuse is accepted

Date: 2026-10-14. Status: Accepted (owner decision, slice 151); constrained by
[0067](0067-mit-license-and-release-model.md) for the prebuilt release executable.

The owner accepts distributing the project under GPL-3.0-compatible terms; the
project will be open source. Reusing PCSX2 code (for example the GS hardware
renderer, texture cache, upscaling, dithering and texture replacement) is
therefore allowed when it is the best working option.

Conditions: import only what is needed, pin the PCSX2 commit and file hashes,
preserve copyright notices, record provenance in `native/THIRD_PARTY.md`, and
update the project license before distributing a binary that contains GPL code.
MIT donor code remains compatible. No PCSX2 JIT (EE/VU/IOP recompilers) is
imported: the no-JIT rule is unchanged.

Long-term owner direction, not authorized as current work: after the
recompiled port is fast and stable, decompile the game, write a native renderer
that removes the VU/GS models, and decouple physics from frame rate.
