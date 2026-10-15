# 0067 — MIT license and release model

Date: 2026-10-15. Status: Accepted (owner decision).

- **License:** the project's own code is MIT (`LICENSE`). Third-party code keeps
  its license; paraLLEl-GS (LGPL-3.0+) ships as a separate DLL so users can
  replace it.
- **Releases:** installers are published as GitHub Releases of
  `moesuito/gt4-staticrecomp`; the launcher source lives in this repository.
- **Distribution:** the installer ships prebuilt executables (they contain
  statically translated game code); the user supplies their own disc image.
  The owner accepts the takedown risk. Fallback if Sony objects: build the
  game code on the user's PC from their disc with a bundled toolchain and a
  de-branded launcher.
- **Branding:** the launcher bundles official Gran Turismo 4 artwork and logos
  with a trademark notice; Windows shows the name "Gran Turismo 4".
- **Consequence for GPL code:** a prebuilt executable that contains game code
  cannot also contain GPL-3.0 code. GPL components (PCSX2 GS, ARMSX2 pipeline,
  decision 0066) must stay out of that executable or be shipped separately;
  resolve before TASK-021.
