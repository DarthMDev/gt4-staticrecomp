# Active Architecture — Native Windows with PS2 Subsystems

2026-10-13, decision 0056 / slice 114. Evidence obtained from frozen code of donor
1b4808793a12c45125262fd2d5f50a9ec9f7c020 and local adapters.

## Confirmed

The program is a Windows x86-64 PE executable, compiled by clang-cl, linked
against a private static library of translated EE code. It does not load a
browser, a WebAssembly module, or a PCSX2 implementation. The EE is not
executed by a generic instruction interpreter: `dispatch.cpp:126–159`
dispatches to translated code and halts when code is missing. `generated/table.cpp`
from the private producer registers 326 EE units.

This does not eliminate subsystem emulation:

| Subsystem | Confirmed implementation in donor |
|---|---|
| IOP | `runtime/src/iop/cpu.cpp:61–74` fetches/decodes R3000 instructions; `iop/kernel.cpp:144–167` binds imports to HLE services |
| VU microprograms | `runtime/src/vu_micro.cpp:788–812` supports registered compiled VU1 microprograms, otherwise interprets; current programs are interpreted |
| VU0 macro operations | `recomp/codegen.cpp:389`, cases around 670–720 emit native C++ helpers with EE code; `Vcallms`/`Vcallmsr` enter separate micro execution |
| VU JIT | `runtime/src/vu_jit_web.cpp:18–30` returns mode 0 outside Emscripten; no Windows JIT is active |
| VU AOT Registration | No registration generated in current set; `vu_micro.cpp:978` provides an empty weak registry |
| Kernel/hardware | Memory models, timers, interrupts, DMA, VIF, disc, SIF/IOP, IPU, SPU2, and memory card |
| GS | PS2 algorithms/memory preserved in batching and compute shaders; our adapter uses Vulkan directly |

Therefore, the precise term is **native hybrid recompilation**. The fact that an
interpreter is compiled for x86-64 does not turn the code it interprets into
AOT. Calling the product native does not justify claiming an absence of
emulation or perfect fidelity of hardware models.

## Integration We Implemented

`scripts/prepare_external_vulkan.py` verifies donor sources before generating
the private GPU/GS/host adapter. `native/native_vulkan.cpp` executes Vulkan compute
and presents via Win32; `window.cpp` integrates keyboard/XInput and events.
`host_io.cpp` delivers 48kHz PCM to WinMM and translates controller input to in-game
buttons/analogs. The launcher verifies executable/shaders/inputs and protects the memory card.

The current graphics pipeline uses internal scale 1 and GS readout according to original rules.
The 16:9 viewport matches the original GT4 option for camera/HUD. This does not implement
4K internal resolution, 144 new frames per second, or decoupled physics.

## Separation of Bases

- MIT ps2-web-recomp: runtime and translator used in the current private producer.
- gt4-decomp: reference studied for metadata/decompilation; not our executable.
- PS2Recomp: reference studied; no GPL implementation imported or linked.
- PCSX2: historical comparison tool and source of Khronos headers;
  no PCSX2 emulation core is linked to the game.
- Original EE recompiler/runtime of this project: now on the archive branch,
  with no active target/build/test on main.

## Unknown

Full EE/IOP/VU/GS/SPU2 fidelity, long-term stability, root cause of sluggishness,
input double-triggering, audio continuity, and physical XInput validation.
Past observations from the original model do not automatically validate this
different runtime. Performance investigation is now delegated to OpenCode;
Codex prepared the handoff, and input fixes remain outside that investigation.
The owner separately authorized slice136's keyboard correction: arrows had been
sent as both D-pad and left-stick axes. Host merge now sends digital buttons/
pressures only, retaining XInput axes. Source/native regression controls qualify
this mapping delta, not its live GT4 menu/steering behavior.
This architecture analysis is neither a performance profile nor a fix.

## Proposed Native Scene Renderer

[Native Port Plan](NATIVE-PORT-PLAN.md) describes game-level geometry/material/camera
extraction before PS2 packet generation, a first live-car forward renderer, then
race coverage and retirement of graphics/service models. It is a proposal, not
the current implementation. Initial performance goal: entry-level dedicated
Windows GPU, 1080p/60; exact hardware and measured feasibility remain open.
Retaining a legacy GS HUD or interpreter fallback does not meet the corresponding
full native claim. Content/script VMs are distinct from PS2 instruction emulation.
