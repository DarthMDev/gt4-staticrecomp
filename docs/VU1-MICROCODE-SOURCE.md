# Where GT4 Keeps Its VU1 Microcode

2026-10-14, slice 151.

## Confirmed

The whole VU1 microcode of GT4 USA v2.00 is one static image stored
XOR-encrypted inside the executable.

- Location: `SCUS_973.28` reconstructed ELF (pinned in `docs/inputs`), file
  offset `0x525DD0`, length `0x3FF0` bytes (VU1 code address 0 upward).
- Keystream (from Nenkai's `gt-modding-hub` notes,
  `docs/ps2/reverse_engineering_notes.md`, documented there for GT3 EU and GT4
  Online): `state = 0x32277070`; for each byte `i` from the blob start,
  `state = 0x5D588B65 * state + 1 (mod 2^32)`, `plain[i] = enc[i] ^ (state & 0xFF)`.
- Control: all 86 VU1 snapshots recorded in two owner sessions (slice 151) are a
  single identical 16 KiB image. That image equals the decrypted blob at every
  instruction except `0x3FF0` and `0x3FF8`, which lie beyond the stored blob
  length. All 86 recorded entry windows match.

Earlier byte searches of the ISO, ELF and all 18,352 decoded GT4.VOL files
found nothing because the stored bytes are encrypted.

## Consequences

- The VU1 program set is closed and known statically: every program is an
  entry point into this one image. AOT coverage no longer depends on gameplay
  recording; recording only helps confirm which entry points are used.
- Remaining work for full coverage: enumerate entry points statically (MSCAL
  targets in EE code and VIF streams, program boundaries after E-bit, jump
  targets) and confirm the content of the last two instructions.
- Hypothesis: the VU0 microcode is a separate, unencrypted blob (Nenkai lists one
  for GT4 Online, size `0x920`); not yet located in USA v2.00.
