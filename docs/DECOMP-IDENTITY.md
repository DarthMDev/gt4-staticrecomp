# gt4-decomp Targets the Same Game Code as Our Disc

2026-10-14, slice 151.

## Confirmed

[gt4-decomp](https://github.com/gustavocanepa/gt4-decomp) (MIT, studied at
commit `b5995291`) labels its target "SCUS-97328 v1.01". Its game code is the
same as ours:

- Our pinned `private/fingerprint-check/CORE.GT4` has SHA-1
  `30031137ce494323816bb707b90d3ec0d07937d2`, equal to the decomp's
  `orig/README.md` hash for `CORE.GT4` as on disc.
- SHA-1 of our reconstructed ELF code segment (file offset 0x1000, 0x517A14
  bytes) is `28422e7e20a6298224294255355487b9a22cef31`, equal to the decomp's
  recorded text SHA-1. Its data segment hash also matches (study report).
- Of 28,325 fully matched decomp functions, 26,261 end in `jr $ra` exactly at
  their recorded address in our ELF (zero shift).

The "v2.00" label in this project refers to the disc; the CORE payload is the
one the decomp matches. The bootstrap `SCUS_973.28` hash was not compared yet.

## Consequences

- No version switch is needed. Decomp names, class layouts and matched C++
  sources apply at the same addresses as our translated EE code.
- Useful decomp files: `config/symbol_addrs.txt`, `config/units.txt`,
  `config/classes.json`, `config/class_layout.json`, `config/adhoc_methods.txt`,
  `knowledge/*.md` (architecture, Adhoc VM, RTTI classes, hardware map).
- Not covered by the decomp: VU microcode, the native frame loop, vsync/DMA
  interrupts, GS/VU packet building and most physics math. See
  [VU1 microcode source](VU1-MICROCODE-SOURCE.md) for the VU1 image.
