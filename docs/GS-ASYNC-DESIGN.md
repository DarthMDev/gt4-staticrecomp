# O2 Design: Asynchronous GS Submission

2026-10-14, slice 151. Read-only source study for phase O2 of the
[Optimization Plan](OPTIMIZATION-PLAN.md). Not implemented. Confirmed = read in
code; Hypothesis = inferred, not measured.

## Root cause (Confirmed)

Every submit ends in a fence wait (`native/native_vulkan.cpp:524-527`). Per
frame there are several: VBlank flush (`gs.cpp:815` → `gpu_tail.inc:1-5`),
`gs::present` display dispatch (`gpu_helpers.inc:35`, `gpu_tail.inc:105`), the
blit submit (`native_vulkan.cpp:869`) and the present fence (`:889-891`). GPU
work never overlaps emulation. The native build never starts the donor render
thread, so `queue::active()` is false and `gs.cpp:192-216` paths are synchronous.

## CPU observation points

| Site | Needs CPU-visible result? | Plan |
|---|---|---|
| VBlank flush `gs.cpp:815` | no, ordering only | submit without wait |
| GS→host transfer `gs.cpp:604`, downloads `:204/209/215`, consumers `dmac.cpp:127`, `hw.cpp:262`→`gs.cpp:682-685,717` | yes | GPU copy of the range into per-slot staging in stream order; resolve on read (`gs.cpp:221`). Count frequency first |
| Present `gs.cpp:864` (`gpu_tail.inc:89-107`) | no, slot reuse only | merge into slot command buffer |
| CLUT slot overflow `port/gpu.cpp:1116-1118` | no | submit without wait |
| Display/copy_frame `gs.cpp:888/935`, dump/save state `gs.cpp:1032,1070-1099` | yes | stay synchronous |

The donor had real async download (donor `gpu.cpp:1518-1572`); the native
adapter replaced it with a synchronous shortcut (`gpu_tail.inc:18-24`).

## Ring of slots

Host-write hazards (Confirmed): `g_data`/`g_params` are single buffers rewritten
every submit (`gpu_helpers.inc:27-28`); one descriptor set (`native_vulkan.cpp:354`),
command buffer (`:309`) and fence (`:313`). `g_cache` is memcpy'd by the CPU
(`port/gpu.cpp:281`) while older batches may read it; its eviction guard
(`:259`) must also require GPU completion.

Design: N=3 slots. Per slot: command buffer, `g_data`, `g_params`, descriptor
set (pool `maxSets` 1→N, `:338-349`), readback staging, timeline value. Shared:
vram, clut, scratch, decoded, cache, tables. Order submits with a timeline
semaphore (enable `timelineSemaphore` in device creation `:266-292`). CPU waits
only for ring full, readback resolve, resize, state save/load and shutdown.

## Barriers

Nearly every adjacent dispatch pair conflicts on `vram` (upload, copy, CLUT,
raster all read or write it). Keep the full barrier (`:585-588`) as default;
a hazard-table mode is low value (at most ~1 ms GPU-side, Hypothesis).

## Presentation

Display dispatch, copy and blit in one slot command buffer; per-slot display
image (`:798-844`); per-image render-complete semaphores instead of single
`acquired_`/`rendered_` (`:621-626`); per-slot present fences checked before
reuse instead of the wait at `:889-891`. Keep FIFO; MAILBOX optional; no
IMMEDIATE/FIFO_RELAXED. At most 2 frames in flight.

## Render thread (optional, last)

The donor protocol exists (`gs.cpp:985-1004`, back-pressure of 2 frames at
`:969`). Requires one owner thread for the Vulkan context and touches about six
`queue::active()` branches (`gs.cpp:208,468,615,683,795,813`).

## Verification

- `vulkan_fixture` unchanged (full VRAM vs CPU reference) plus a random
  multi-submit stress comparing async and sync VRAM byte for byte.
- Headless `--frames N --frames-dir --frame-every`, per-frame SHA-256, sync vs
  async, after confirming two sync runs are identical.
- Vulkan synchronization validation layer on fixture and boot smoke.
- Pure C++ ring fixture with a fake timeline (reuse order, ring-full blocking,
  cache eviction guard, shutdown drain).
- GPU timestamps first: `PS2_GPU_PROFILE` currently throws in native init, so
  GPU busy time cannot yet be separated from submit overhead.

## Expected effect (Hypothesis)

VBlank async + ring 5-8 of 10.8 ms; merged display submit 0.5-1.5 ms; blit wait
0.3-1 ms; deferred mid-frame downloads 0-3 ms depending on frequency. Overlap
is bounded by actual GPU busy time.
