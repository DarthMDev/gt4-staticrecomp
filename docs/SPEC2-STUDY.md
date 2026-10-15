# Gran Turismo 4 Spec II v1.10 — Second-Target Study

2026-10-14, slice 151. Read-only study of the owner's ISO (not distributed).
[C] = Confirmed, [H] = Hypothesis, [U] = Unknown.

## Identity [C]

- ISO SHA-256 `6a3484b9672c10a92154ecee6faa5bbcfd4ed8cd538d8283e429c3fe6e1704b2`,
  3,683,418,112 bytes. Serial SCUS-97436 (GT4 Online public beta lineage), boot
  file `SCUS_974.36`, no `CORE.GT4`; DNAS image and SYSTEM.CNF match the beta.
- `SCUS_974.36` is a plain ELF (no Salsa20/CORE step): text 0x100000 size
  0x55FBBC, data at 0x65FC00, plus an RWX plugin segment at 0x68A480 (0x2CCC
  bytes) that overlays a zero-filled part of the data image. Build strings:
  "ReleaseVersion 2.10.200605300000", "GT4O-1.0", "GT4_PREMIUM_US2560".
- About 35,678 `jr $ra` versus 30,330 in retail; only about 25% of sampled code
  windows appear verbatim in retail. Retail decomp addresses do not apply.

## Modifications

- [C] A PS2 plugin injector payload (section `.plugin_note`, "PS2PLUGININJECTOR")
  is entered by one call at 0x1001E8 during startup. Strings suggest camera,
  gauge, AI, lap and test toggles.
- [U] Other code patches: no unmodified beta ELF hash is available to diff.
- [C] GT4.VOL: RoFS 3.1 with the GT4 Online TOC position, 26,891 files
  (3.435 GB); 7,551 files (873 MB) not in the beta list (car sounds, GT mode and
  arcade projects, tuner screens). Scripts are mostly Adhoc 7.

## Microcode [C]

- The encrypted VU1 image (VA 0x69B110, file 0x59C110, 0x3FF0 bytes) is
  byte-identical to retail. Our VU1 AOT table (keyed by content) applies as is.
- The VU0 image (VA 0x731180, 0x920 bytes, plain) differs from retail in 1,050
  of 2,336 bytes; it needs its own check.

## What a second target needs

A new input path without CORE reconstruction, its own pins, a loader/translator
policy for the overlapping plugin segment (exclude it or treat the injected call
as external code), its own EE translation and symbol map, and a VU0 review.
Recommendation: finish the retail port first; the VU1, GS and pacing work carries
over directly.
