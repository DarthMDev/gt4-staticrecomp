# License demo cipher: consumers, coverage and remaining risk (TASK-017)

GT4 encrypts some data with an FPU-keyed stream cipher: decrypt `func_00579E00`, encrypt `func_00579F38`. The key
stream is the bit pattern of a chain of single-precision `mul.s`/`div.s` results. Our `ps2/fpu.hpp` is not bit-exact
with the R5900 for this chain (journal 2026-10-16, rounds 1-3), so data encrypted on a real PS2 decrypts wrong in our
build: on the disc, 0 of 592 license demos pass the game's own CRC. A decrypted body lands in one of two key-stream
phases. In the wrong phase the record count at body + 0x1AA4 reads 0x7F7F7F7F, and `Manager::load` sweeps 64 KiB
slots through RAM until a thread stack is wiped (crash).

Method: static call sites from the translated units (every direct `jal` leaves a `c.pc = <target>` fallback; the
units' `musttail` and entry tables were searched too), the gt4-decomp sources for names and behaviour, and a scan of
the reconstructed ELF for data words equal to the routine addresses. The scan found no vtable or function-pointer use
of any of the routines below, so the call graph is the static one (Confirmed for the translated code; code paths that
were never translated would halt with "no translated code" instead).

## Call graph (Confirmed)

| Routine | Called from (return address) | Role |
|---|---|---|
| `func_00579E00` decrypt | `func_0044D278` only (tail call) | in-place decrypt of `buf + hdr[0]`, `hdr[1]` bytes, key `hdr[3]`; returns the CRC check result |
| `func_0044D278` | `0x001100D0` (0x00110164), `0x00110230` (0x001102C4), `0x0044D1B8` (0x0044D1E0) | header unpack, then decrypt |
| `0x0044D1B8` | `func_001CFFF8` (0x001D019C), GT4MC FileGT4 read | decrypt; **calls the object's load virtual only if the CRC passed** |
| `func_00579F38` encrypt | `0x0044D2A8` (0x0044D2CC), `func_001CAF30` (0x001CB08C) | encrypt; key = CRC of the plaintext |
| `0x0044D2A8` | `0x0044D220` (0x0044D25C) <- `func_001CFE68` (0x001CFF58), GT4MC FileGT4 write | serialize an object, then encrypt it |
| `0x001100D0` license loader 1 | `func_0011CB38` (0x0011CCEC, 0x0011CE5C, 0x0011CEAC) | `"license/%s"` -> vtbl[154] (`RaceSolitaire::loadDemoInputs` 0x003EAD18) |
| `0x00110230` license loader 2 | `func_00114E38` (0x00114F54, 0x00115020, 0x00115064) | `"license/%s"` -> vtbl[57] (`RaceSolitaire::loadReplayInputs` 0x003EAD58 in the RaceSolitaire vtables; 0x003D5F00 in the owner's B5 crash) |

Every demo-input virtual reached from the two loaders calls `AutomobileControlRecord::Manager::load(this + X,
body + 0x1AA4, 1)` (0x003EAD18 and 0x003EAD58 with X = 0xE560, 0x003D5F00 with X = 0x124A0; Confirmed from the units).

## Consumers

| Consumer | Data | Reached from | CRC checked | Wrong data does | Coverage |
|---|---|---|---|---|---|
| License loader 1 `0x001100D0` | disc `replay/license/<region><group><n>` (PS2ZIP, GT4.VOL) | license test: background/attract replay of the test | no | count 0x7F7F7F7F: memory sweep, crash | **guard** (v1 slice205, v2 slice206) |
| License loader 2 `0x00110230` | the same files | license test: the second demo path (owner B5 crash, session-20261016-235625-870886) | no | the same: memory sweep, crash | **guard v2** (slice206) |
| Memory-card load `0x0044D1B8` (GT4MC FileGT4 read, `func_001CFFF8`) | GT4 card files (replays, ghosts and other serialized objects) | loading from the card | **yes**: no load on a CRC failure, the read returns error 2 | a clean load error, no crash | not needed |
| Memory-card save `0x0044D220` -> `0x0044D2A8` | the same files, written | saving to the card | n/a (encrypt) | nothing parses the result | not needed |
| `func_001CAF30` encrypt (from `func_0019B3A0`) | image-type data (YUV-sized buffer; Hypothesis: photo or EyeToy picture) | writing that data | n/a (encrypt) | nothing parses the result | not needed |

Attract or title demos, the showroom, photo mode and arcade demos do not call the decrypt routines (no other call site
exists). The only on-disc encrypted files that the game decrypts are the license demos, and the US game builds their
names from `"license/%s"` (format string at 0x00694C68).

## Oracle per on-disc file set (donor FPU model = our build; `scripts/license_demo_oracle.py`)

Both disc consumers read the same `replay/license/*` set.

| Prefix | Files | Game CRC passes | Sane layout (kept) | Guarded |
|---|---|---|---|---|
| us (the US game) | 85 | 0 | 33 | 52 |
| eu | 85 | 0 | 48 | 37 |
| uk | 85 | 0 | 48 | 37 |
| kr | 85 | 0 | 33 | 52 |
| jp | 252 | 0 | 80 | 172 (the JP files use another layout at +0x1AA4; the US game does not load them) |

A sane layout means the record payload is in the right phase and byte-exact. Only the first ~250 bytes (the header
region, before the cipher state saturates) are still wrong, so such a demo plays.

## Memory-card data

- Saves made by our build: encrypt and decrypt run the same FPU code and chain on the cipher bytes, so the key stream
  repeats exactly and the CRC passes. Saves are self-consistent (High confidence; the save-and-load round trip in
  `tests/python/test_license_demo_oracle.py` holds for every model).
- Saves made on a real PS2, for example a card imported from hardware: their decrypt in our build fails the CRC, as
  every disc demo does. `0x0044D1B8` then skips the load and the read reports an error. The guard is not involved and
  nothing crashes. Those replays and ghosts cannot be loaded until the FPU is bit-exact (TASK-063). Hypothesis:
  PCSX2-made cards behave the same way when PCSX2's rounding differs from the hardware.
- Saves made by our build would not load on a real PS2 either: the hardware decrypts them with its own key stream.

## Guard rule (native/license_guard.hpp)

At the indirect call from either license loader into its demo-input virtual (return address 0x001101E4 or 0x00110344,
any target), the decrypted body is checked with the game's own CRC and with the single-Recorder layout rule. If the CRC
fails **and** the layout is not sane, the record count is set to 0xFFFFFFFF. `Manager::load` then takes its own existing
`n < 0` path and copies no records. CRC-passing or sane data is never touched. One `[license-guard]` line per load
names the loader and the target.

## Remaining risk

- Root cause open: the FPU is not bit-exact (TASK-063). License demos in the wrong phase play no demo; the license test
  itself works. The same happens in PCSX2 with default settings; with EE clamping "Full", PCSX2 plays B1's demo but
  crashes in B3 (owner test 2026-10-16): another phase coin flip.
- Sane-layout demos have wrong bytes in their first ~250 bytes (options at +0xB00..+0xB04 come later and are right only
  when the phase is right). No crash is known from this.
- A real-PS2 card's replays and ghosts fail to load cleanly (above).
- A third, untranslated path would halt with "no translated code" rather than sweep memory. None is known.
