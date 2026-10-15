# Lessons from PCSX2 for a No-JIT GT4 Port

2026-10-14, slice 151. Read-only study of PCSX2 commit
`aa7ab4306e269075784c7ac3eb4b45e6e6c53445` (local private checkout). Ideas and
facts only; no PCSX2 code is copied (GPL-3.0). [F] = read in source,
[I] = inference.

## GT4 game database entry

[F] `bin/resources/GameIndex.yaml`: SCUS-97328 (line ~11870) and the other GT4
serials share anchors defined near lines 522-524:

- `vuClampMode: 2` (extra + preserve sign); comment cites GT-mode text and a
  menu fix. Our donor already clamps VU floats in `fin()`/`fout()`; preserve
  sign handling must stay exact in the AOT emitter.
- GS hardware fixes (inherited from the GT3 base, line ~204): `halfPixelOffset: 4`,
  `autoFlush: 1` (lens flare/intensity), `getSkipCount: GSC_PolyphonyDigitalGames`.
- [F] `GSC_PolyphonyDigitalGames` (`pcsx2/GS/Renderers/HW/GSHwHack.cpp:644+`)
  replaces GT's render-target channel shuffle through a CLUT ramp
  (brightness/fade) with one draw. [I] Our compute rasterizer executes the
  original sequence; a future hardware renderer must handle render-target
  feedback, CLUT render and half-pixel rules for GT4.

## Threading

- MTVU (`pcsx2/MTVU.cpp`): VU1 work goes through a 16 MB lock-free ring; the EE
  thread waits only for results it reads (signal/finish/label flags applied on
  the EE thread). Off by default in PCSX2.
- MTGS (`pcsx2/MTGS.cpp`): GS on its own thread with an 8 MiB ring; the EE stalls
  only on ring full, synchronous GS reads and more than 2 queued VSyncs.
- [I] For us: the GS thread (with [GS-ASYNC-DESIGN](GS-ASYNC-DESIGN.md)) is the
  cleaner first step. A VU1 worker thread is possible later with exact sync at
  every point where the EE observes VU1 results.

## VU flag elimination (applies to the AOT emitter)

[F] microVU computes flag liveness per block (`microVU_Flags.inl` ~359-460),
keeps only the last 4 status/MAC/clip writers, and the default-on
`vuFlagHack` drops status-flag work entirely when a microprogram never reads
status flags. [I] All of this is static analysis: compute it once per program
at AOT time and emit no flag code where nothing can read it.

## Speed hacks that do not change game time

- Wait-loop skip [F] (`Interpreter.cpp:234`, `iR5900.cpp` ~2153, ~2296-2320):
  detects spin/timeout loops and jumps to the next scheduled event, preserving
  counter values. Default on. [I] Can be detected offline in EE AOT output and
  emitted as a guarded skip.
- INTC_STAT shortcut [F] (`recVTLB.cpp:500`): spin-on-interrupt-status reads.
- Instant VU1 [F] (`VU1micro.cpp:61-63`): VU1 runs to completion synchronously,
  which our donor already does.
- EE cycle rate/skip changes game timing; do not use.

## Future GS renderer pointers

[F] `pcsx2/GS/Renderers/HW/GSRendererHW.*` (draws, upscale multiplier, dither
emulation), `GSTextureCache.*` (render-target/texture cache),
`GSTextureReplacements*.cpp`, `Vulkan/GSDeviceVK.cpp`. Importing any of it
makes the distributed binary GPL-3.0; an owner decision is required first.
