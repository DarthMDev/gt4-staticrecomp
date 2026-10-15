# Optimization Plan: Recompile Everything Feasible, No JIT

2026-10-14, slice 151. Owner-requested replanning: make GT4 light and fast on
Windows, compile ahead of time every guest processor that can be compiled, keep
no JIT, and stay portable to Android/iOS later. A dedicated renderer is out of
scope; a PCSX2-class GS renderer (upscaling, dithering control, texture
replacement) is a later goal. This document supersedes the ordering of
[HYBRID-PORT-PLAN](HYBRID-PORT-PLAN.md) H1/H2 where they conflict; its safety
gates and no-JIT definition still apply. Planning only: no build, run or code
change in this slice.

## 1. Where the time goes today

Owner interval, slice 150 (Prius, external camera, Nordschleife, 149 candidate):

| Category | ms per VBlank sample | Share |
|---|---:|---:|
| Whole interval, median | 42.7 | ~23 fps |
| VU1 microprograms, interpreted | 27.0 | 62.5% |
| GS submission fence wait | 9.4 | 21.7% |
| EE (AOT C++) | 2.3 | 5% |

## 2. Findings from source (read-only, donor 1b48087 + `native/`)

**Confirmed — VU1 AOT was never activated for GT4.** The donor's intended port
flow records VU1 programs and compiles them offline (`scripts/port.sh:121`
runs with `PS2_VU_RECORD`, `:149-150` runs `vurecomp`). `vu_micro.cpp:794`
tries a compiled program first and only interprets on a miss (`:812`). Our
build links an empty registry, so every VU1 call takes the interpreter. The
27 ms is the miss path, not an inherent VU1 cost.

**Confirmed — the interpreter is slow by construction.** `run_fast` is scalar
per lane; every operand/result goes through `fin()`/`fout()` clamping
(`vu_micro.cpp:291-298`), lower ops are re-decoded through a two-level switch
each step (`:436`), and a flag pipe ring is updated per op. The emitter
`recomp/vurecomp.cpp` instead emits straight-line code per program with clang
vector-extension SIMD (`vu_ops.hpp:124-189`), VI registers in locals and flag
liveness analysis. **Hypothesis:** 5–15x on transform loops; not measured.

**Confirmed — GS work is fully synchronous and serial.** `finish_commands`
(`native/native_vulkan.cpp:507-530`) submits and immediately waits on a single
fence; one command buffer exists, and every dispatch is followed by a full
compute barrier (`:585-588`). `download_async` is a synchronous download
(`native/gpu_tail.inc:18-24`). Presentation uses FIFO (`native_vulkan.cpp:712`)
and then waits on a present fence (`:887-892`). All of it runs on the EE/VU
thread: the donor's render thread and VU thread (`system.cpp:422-423`) are
never started by `native/`.

**Confirmed — remaining interpreters.**

| Guest unit | Current execution | Plan |
|---|---|---|
| EE | AOT C++ (326 units) | keep; tune build (LTO/PGO) |
| VU0 macro (COP2 in EE) | AOT, inline in EE C++ | keep |
| VU1 micro | interpreted (empty registry) | **AOT first** |
| VU0 micro (VCALLMS) | interpreted, slower uncached path `vu_micro.cpp:921-967` | AOT after VU1 (emitter is VU1-only) |
| IOP (R3000) | fetch/decode/switch interpreter, mostly HLE modules | measure, then AOT the IRX set |

**High confidence:** IOP costs a few ms at most (EE/8 clock, idle skipping,
HLE modules); not yet separated in the probe.

## 3. Principle: "no interpreter" for a fixed disc

GT4 USA v2.00 is one fixed binary. Its EE code, IOP modules and VU microcode
are finite sets, so each can be compiled offline for every target (x86-64,
ARM64). The interpreters stay linked only as an **explicit, counted fallback**
for unseen variants, never as the normal path. The end state is a fallback
counter of zero across the qualified scenes. Coverage starts from recording,
because that is what the donor already supports. Static extraction of VIF MPG
packets from the disc follows later to close the gaps without gameplay.

No JIT means no runtime generation of host machine code from guest code, on
any platform. GPU shader/pipeline compilation by the driver is allowed.

## 4. Phases

Each phase is one variable, one candidate, measured in the same owner-chosen
scene against the previous candidate. Owner gameplay stays manual.

### O0 — Local baseline on this machine (agent only)

Rebuild the 149 VU-split candidate here (private outputs 116–150 are not on
this machine). Verify pins, CTest/Python. Reuse the slice 150 numbers as
reference; one owner interval confirms this machine matches.

### O1 — VU1 AOT (largest lever)

1. Recording mode: an opt-in runtime switch writes each missed VU1 program
   (16 KiB image, entry, key) into a fresh private corpus directory, with
   count/quota limits. Uses the existing `lookup` miss hook.
2. Owner plays menus, showroom, one race with recording on. This is one session.
3. Harden `vurecomp` before trusting it (gates in [VU-AOT-FEASIBILITY](VU-AOT-FEASIBILITY.md)):
   exact-size reads, fresh output directory only, a step budget with fallback,
   full-content identity next to the 64-bit key.
4. Differential verification mode: on sampled calls, run interpreter and AOT
   from the same state and compare VU registers/flags/memory/XGKICK output.
   The donor already has the hook shape (`check_jit`, `vu_micro.cpp:797-798`).
   Fixtures in CTest for the emitter on authored programs.
5. Generate the provider library, link it, report compiled vs interpreted
   calls at F8. Owner runs the same scene: AOT on vs `PS2_VU_INTERPRET`.

Exit: interpreted VU1 calls near zero in recorded scenes, differential mode
clean, measurable drop of the VU1 category, audio/input/saves unchanged.

### O2 — Asynchronous GS submission

1. Ring of N command buffers and fences; submit without waiting; wait only when
   the CPU needs GPU results (readbacks) or the ring is full.
2. Replace the barrier after every dispatch with barriers only where a
   read-after-write hazard exists in the batch (keep a full-barrier switch).
3. Presentation: frames in flight instead of the present-fence wait; MAILBOX or
   IMMEDIATE as options, FIFO kept as the vsync option.
4. Then evaluate the donor render thread so GS work leaves the EE/VU thread.

Exit: GS wait category mostly gone, pixel-identical headless frames against the
synchronous path, no new tearing/corruption reports from the owner.

### O3 — Close the remaining interpreters

- VU0 micro AOT: extend the emitter for unit 0 (4 KiB code, VU0 memory map,
  shared registers with COP2). Only after VU1 is done; its measured cost is ~0.
- IOP: add a separate probe category first. If material, translate the fixed
  IRX set offline with the same EE-style approach; HLE modules stay native.
- EE: clang ThinLTO, then PGO from an owner-played profile (offline), each as
  its own candidate. No fast-math or guest rounding changes.

### O4 — Frame budget and pacing

With VU1 and GS addressed, re-attribute. Target the H1 screening gate from the
hybrid plan (correct speed, p99 ≤ 20 ms), then the stricter stable-60 gate.
Evaluate the donor VU thread (MTVU-style) only if VU1 AOT is still the critical
path. Investigate the intermittent 43/60 fps menu pacing here.

### O5 — Graphics (later, not part of the speed work)

First the donor's scale 1..3 compute path (`gs.wgsl` `SCALE` override,
`gpu.cpp:1174-1180`), which `gpu_init.inc:3-5` currently rejects. A PCSX2 GS
hardware renderer is the path to upscaling, dithering control and texture
replacement. **License note:** PCSX2 is GPL-3.0; importing its GS makes the
distributed binary GPL-3.0 (the MIT donor is compatible). That decision belongs
to the owner before any import.

### O6 — ARM64, Android, iOS (later)

All guest code is C++ produced offline, so the same generators target ARM64
(Android NDK, Xcode). The `vu_ops.hpp` clang vector extensions compile to NEON.
Win32/WinMM/XInput host layers need platform implementations; Vulkan on
Android, MoltenVK on iOS (no JIT is also an iOS App Store requirement).

### After O6 — Phase 2: source-like game code

Owner direction (2026-10-14). Once the recompiled port runs fast and as
natively as possible, apply [gt4-decomp](DECOMP-IDENTITY.md) (byte-identical
CORE) function by function: translated functions are replaced by readable,
natively compiled decomp C++ behind a guest-memory/ABI bridge, each swap
checked against the translated version. Goal: code that reads like original
source, is moddable and portable to any hardware, and enables an own renderer
(removing VU/GS models) and physics decoupled from frame rate. This is not a
speed task. A cheap early step, importing decomp names into generated code,
may happen at any time.

## 5. Expected outcome (Hypothesis)

If VU1 AOT reaches the low end of its estimate (~5x → ~5 ms) and asynchronous
submission hides most of the 9.4 ms wait, the race interval moves from ~43 ms
toward the 16.7 ms budget. This is an estimate to choose the order of work,
not a claim. Only owner-run intervals in the same scene decide it.

## 6. Process

Keep evidence (hashes, pins, Confirmed/Hypothesis labels), but batch per
phase: one report per candidate, one owner session per comparison. Agents do
builds, fixtures and headless controls; the owner only plays and presses F7/F8.
