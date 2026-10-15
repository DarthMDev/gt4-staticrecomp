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

## Implementation status (slice 152)

Confirmed (slice 152 build, executable pin in
[slice152-gs-async.json](inputs/slice152-gs-async.json)): steps 1-3 of this design are
implemented in the adapter, behind the performance profile `--mode gs-async`.
Step 4 (render thread) is not implemented.

- Ring of three slots (`native/native_vulkan.cpp`, `native/gs_submit_ring.hpp`). Each
  slot owns a command buffer, descriptor set, `g_data`/`g_params`-equivalent upload
  buffers, an acquire semaphore, a render-finished semaphore and a present fence.
- One timeline semaphore orders submissions: each waits for its predecessor's value
  at `ALL_COMMANDS` and signals its own id. Vulkan does not order separate submissions
  by itself, so this chain is required for safe overlap.
- Reuse is gated by completion: `SubmitRing::reserve` waits only when the submission
  that last used the slot, three ids earlier, has not completed. CPU waits are at ring
  full, readbacks (`read`), resize, state transfer and shutdown.
- Presentation is recorded into the same slot submission as the display dispatch
  (copy and blit). At most two presentations are pending; the older present fence is
  waited before a third is issued.
- Cache eviction guard: an entry may be overwritten only when its last submission has
  completed (`SubmitRing::may_overwrite`, `completed_submission()`).
- `GT4_GS_SYNC=1` restores a fully synchronous path: every submission and every present
  is waited before returning. Any other value is refused.
- Step 0 (overlay visibility): the swapchain requests `COLOR_ATTACHMENT` when the surface
  supports it, with `TRANSFER_DST` kept. The present queue family is family 0 with
  graphics and compute, which is what the surface-support query requires.

Deviations from the design above:

1. One display image instead of one per slot. The timeline chain already orders every
   submission after its predecessor, so the shared display image is never used by two
   submissions at once.
2. Readbacks are still synchronous (the CPU waits for the timeline). The GPU-copy
   staging of mid-frame downloads is not implemented, so the GS wait category is only
   partly reduced. Slice 183 (TASK-030) counts readbacks (`gs_readback` in F8) and waits
   only for the submissions that wrote the bytes read (`native/gs_write_tracker.hpp`).
3. Barriers after each dispatch stay full. Two swapchain image barriers were tightened
   to `TRANSFER` (source) and `ALL_COMMANDS` (final destination) to match the acquire and
   signal stages that synchronization validation checks.
4. `vulkan_fixture` keeps its public `bind_buffers`/`execute` contract. `execute()` uses
   the buffers the caller binds at indices 2 and 3; `submit()` uses slot-owned buffers.

Verification (Confirmed, this build): `gs_submit_ring_fixture` (slot order, ring-full
blocking, eviction guard, shutdown drain, random completion lag); `vulkan_fixture` in
both modes; `native_vulkan_async_stress_compare` (async and `GT4_GS_SYNC=1` VRAM are
byte-identical, 4 MiB). Full CTest 17/17; Python 203 tests, OK, one pre-existing skip.

Stress determinism finding (Confirmed): draws that overlap inside one batch are not
deterministic on the existing raster path, in the old slice 151 adapter and in the new
build alike, so the stress workload flushes after each primitive. Uploads alone are
deterministic and identical across modes.

Headless smoke (Confirmed, 4200 frames, fixed press schedule, no window): every dumped
frame (42 at 100-frame interval, including the 700-frame multiples) is identical between
synchronous and asynchronous runs and between two synchronous runs. Wall times include
boot and are not a frame-time measurement: sync 34.7 s and 33.7 s, async 30.5 s, one run each.

Synchronization validation (Confirmed, overlay layers disabled): zero errors in both
modes. With implicit overlay layers enabled (OBS, Epic, Steam, RTSS on this machine),
the layers draw into swapchain images and report `PRESENT_AFTER_WRITE` on every
presentation, in the old adapter too. The old adapter also reported render-pass VUIDs
(`VkFramebufferCreateInfo pAttachments-00877`), consistent with overlays drawing into an
image without color-attachment usage; those VUIDs are absent after Step 0.
With implicit layers enabled, the create-info usage read back after
`vkCreateSwapchainKHR` is `0x1b` (requested `0x12`); with them disabled it stays `0x12`. Whether RTSS now draws on the owner's machine is Unknown
until the owner checks it.

Open: owner gameplay measurement (`--mode gs-async`), owner check of the overlay, the
download staging step and the render thread.
