# VU0 Micro Mode Map

2026-10-14, slice 151. Read-only study; nothing implemented.

## Confirmed

- Startup upload routine at EE 0x4A03C0 (wrapper `func_004A0398`, called once
  from `func_00107730`) uploads both microcode images: VU0 from vaddr 0x6D3280,
  size 0xA80, and VU1 from vaddr 0x624DD0, size 0x3FF0. The VU1 pointer equals
  the [encrypted VU1 image](VU1-MICROCODE-SOURCE.md) at file offset 0x525DD0,
  which validates the reading.
- The VU0 image (file offset 0x5D4280, 2,688 bytes) is stored in plain form.
- EE code contains 26 `VCALLMS` sites, no `VCALLMSR` and no CMSAR0 writes. They
  use 14 distinct entries: 0x058, 0x078, 0x0B8, 0x0F8, 0x2E8, 0x308, 0x460,
  0x478, 0x548, 0x678, 0x700, 0x858, 0x900, 0x930. All sites are in decomp
  unit `unit_00494578` (graphics/PGL side); the containing functions are not
  decompiled yet.
- No static VIF0 MSCAL packets were found; MPG packets are built at runtime.
- The donor runs VU0 micro synchronously (`vcallms` → `run(0)`, busy bit always
  false) and shares VF/VI/ACC/flags with COP2 macro mode.

## Hypothesis

Each entry runs 3-45 instruction pairs to an E-bit; programs are short, which
fits the ~0 ms measured in the race interval. Purpose (shape building, texture
or matrix work) is Unknown until the callers are decompiled.

## AOT plan

Because all 14 entries are known statically from `VCALLMS` immediates, VU0 AOT
needs no gameplay recording. `tools/vu_recomp` needs a unit parameter: 4 KiB
code/data masks, the 0xA80 image padded to 4 KiB, no XGKICK for unit 0, and a
lookup in `run(0, ...)`. Controls: blob SHA-256 pin, registry covering all 26
sites, per-entry differential against the interpreter, unchanged VU1 tests.
Estimated 4-7 days including controls. Low speed value; it completes the
"no interpreter on the normal path" goal.
