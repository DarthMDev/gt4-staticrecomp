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
