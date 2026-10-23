# IOP Module Map

2026-10-14, slice 151. Read-only study; nothing changed.

## Confirmed

- Boot reboots the IOP with `cdrom0:\IOPRP300.IMG` (18 Sony modules: SYSMEM,
  LOADCORE, SIFCMD, SIFMAN, THREADMAN, IOMAN, MODLOAD, FILEIO, CDVDMAN, CDVDFSV,
  LOADFILE, TIMEMANI, ROMDRV, EESYNC, SYSCLIB, STDIO). The donor serves these
  libraries as native HLE functions (`runtime/src/iop/libs.cpp`, `io.cpp`,
  `cdvd.cpp`, `sif.cpp`).
- GT4 then loads 23 IRX files from `cdrom0:\IRX\`, identical in all 15 owner
  sessions and at identical load addresses (669,395 bytes total):
  - network: INET, NETCNF, RTINETB, INETCTL, DEV9, SMAP;
  - sound: LIBSD, PDISPU2, PDISTR, LIBPDI; disc: PDICDVD;
  - memory card: MCMAN, MCSERV, SIO2MAN, SIO2D;
  - USB: USBD, PDIUSB, USTORAGE; other: LGDEV, DBCMAN, DS2U_D, MSIFRPC, POWOFF.
  PADMAN, MTAPMAN, EYETOY and PDINETCNF are on disc but never loaded; pads go
  through the native SIO2 model.
- `register_hle_module` (`kernel.cpp:484`) has no caller, so every game IRX is
  interpreted. Imports bind to HLE traps or guest library jumps.
- The donor IOP interpreter writes load results immediately: the R3000A
  one-instruction load delay is not modeled (`iop/cpu.cpp` load cases). Fidelity
  gap independent of AOT; impact on GT4 Unknown.
- Measured cost: `iop_run` about 0.4 ms per race frame. No per-module counter.

## Plan

1. Add per-module instruction counters and per-library HLE call counts.
2. Only if a module group dominates: native HLE of that group (likely the sound
   path LIBSD/PDISPU2/PDISTR), keeping the SPU2, SIO2 and CDVD contracts. This is
   simpler than a full R3000A translator.
3. Full IOP AOT (load delay, COP0, thread yield points, per-base relocation —
   bases are fixed for this ISO) is possible but weeks of work; low priority.

## Spec II (TASK-100 S6)

Source: the owner's Spec II ISO. Pin: [inputs/spec2-iop.json](inputs/spec2-iop.json) (35 files, 3,747,861 bytes,
every size and SHA-256). Tool contract: `tests/python/test_spec2.py` (IopTests).

**The disc** [Confirmed]. Spec II carries 30 files in `\IRX\` and 5 in `\NET\`. Against v2.00: 6 are new
(`LGAUD.IRX`, `PPP.IRX`, `PPPOE.IRX`, `DNAS_MC.ERX`, `DNAS_NET.ERX`, `NTG_ISCK.ERX`), 5 are byte-identical
(`LIBPDI`, `PDICDVD`, `PDINETCNF`, `PDIUSB` and the two `/NET` icon files), and the rest are different builds. The
beta shares Spec II's `\IRX\` set exactly, so the extra modules come with the Public Beta lineage, not from the
Spec II patch.

**The load list** [Confirmed strings, order Unknown]. `SCUS_974.36` contains these lower-case names:
`pad2/ds2u_d.irx`, `sio2man`, `mcman`, `mcserv`, `sio2d`, `dbcman`, `libsd`, `usbd`, `inet`, `netcnf`, `inetctl`,
`smap`, `ppp`, `pppoe`, `msifrpc`, `libpdi`, `pdicdvd`, `pdispu2`, `pdistr`, `pdiusb`, `ustorage`, `lgdev`, `dev9`,
`powoff`, `rtinetb`, `eyetoy`, `pdinetcnf`, `lgaud`. That is 28 names against v2.00's 23 loaded modules: the new
ones are `ppp`, `pppoe` and `lgaud` (plus `dev9`/`smap`/`rtinetb`, which v2.00 also loads). Whether every name is
loaded on the offline path, and in what order, is Unknown until S7b.

**Policy per module** (fallbacks in order, no JIT anywhere):

| Group | Policy | Why |
|---|---|---|
| Pads, memory card, disc, USB, sound, `POWOFF`, `LGAUD` | interpreted as today | already covered by the HLE system libraries and the SIO2/SPU2/CDVD contracts |
| `INET`, `NETCNF`, `INETCTL`, `SMAP`, `RTINETB`, `DEV9`, `PPP`, `PPPOE` | interpreted, with no device model | no DEV9/SMAP/PPP hardware exists in the runtime; the module loads and its device open must fail cleanly. A hang is an S7b finding: first a per-module instruction counter, then an HLE stub that reports "no network device", then refusal to load |
| `DNAS_MC.ERX`, `DNAS_NET.ERX`, `NTG_ISCK.ERX` (under `\NET`) | not loaded offline | DNAS authentication needs a network and a server; offline play must not reach them |
| `PADMAN`, `MTAPMAN`, `EYETOY`, `PDINETCNF` | on disc, not requested | v2.00 never loads them; pads go through the native SIO2 model |

Nothing here changes v2.00: the module files come from the disc the user supplies and the interpreter is the existing
one. The S6 acceptance ("offline play without hangs") stays open until S7b runs the boot.
