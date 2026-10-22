# Notice

**Gran Turismo 4** — GT4Recomp is an unofficial fan project. It is not affiliated with, endorsed by or
sponsored by Sony Interactive Entertainment Inc. or Polyphony Digital Inc. "Gran Turismo", "GT", the
Gran Turismo 4 logo, the GT emblem and the Gran Turismo 4 cover artwork are trademarks or copyrighted
works of Sony Interactive Entertainment Inc. and/or Polyphony Digital Inc., used here only to identify
the game. No game data is included in the source repository; you need your own copy of Gran Turismo 4
(USA, v2.00) to play. If you are a rights holder and want something removed, please open an issue at
github.com/moesuito/gt4-staticrecomp.

## Bundled official assets

The launcher bundles the following files from `launcher/assets/brand/`. Each has its source URL,
retrieval date, size and SHA-256 in `launcher/assets/brand/brand.toml`.

- `source/gt4-logo.svg`: Gran Turismo 4 logo (Wikimedia Commons; the page lists it as public domain,
  credit Sony Computer Entertainment). Owner to confirm.
- `source/cover-front.jpeg`: Gran Turismo 4 front cover, 1000x1500, supplied by the project owner
  (2026-10-15). Owner to confirm the licence of the artwork.
- The derived files `logo-gt4.png`, `emblem-gt.png`, `cover-front.png`, `game.ico` and `launcher.ico`.

A de-branded build replaces that folder with `launcher/assets/brand-neutral/`.

## GSdx (the PCSX2 GS renderer)

GSdx is the PCSX2 GS renderer of the PCSX2 project (https://pcsx2.net; the project made the emulator and its GS). The
launcher offers it as the Graphics renderer "GSdx". It is the separate program `gt4-gs.exe`, built from the GPL-3.0-or-later
fork `moesuito/gt4recomp-gs` (based on PCSX2 v2.8.2), downloaded only when it is selected. It runs in its own process and
is not part of GT4Recomp's MIT code; the release archive carries its licence and source.
