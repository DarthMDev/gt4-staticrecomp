# 0053 — Direct native Vulkan for the isolated execution pilot

Status: accepted, 2026-10-12. Owner selects Vulkan after decisions0051/0052.

Reuse the frozen MIT donor's GS batching, texture addressing and compute
algorithms. Convert its nine WGSL compute entrypoints with pinned Naga30.0.0
and implement Vulkan resources, dispatch, readback and Win32 presentation.
Do not use a CPU-rendered frame upload as renderer acceptance. Retain the
separate Clang runtime/game library and production MSVC contract gates.

Scale1, one graphics/compute queue, coherent mapped buffers and synchronous
submission fences are the qualified first implementation. Compute writes
have explicit visibility barriers between dispatches and before readback or
image copy. Present waits use `VK_KHR_swapchain_maintenance1` fences before
reusing a semaphore or destroying a swapchain; queue idle alone is insufficient.
Unsupported features, dimensions, shader shape or buffer bounds fail with
context. Primary GPU mode is forced by the experimental host.

Confirmed: synthetic full-VRAM controls, nine fragmented upload formats,
state restore/readback serials, repeated Win32 presentation and resize restore.
Pinned game coldboots reach navigable menus and FordKa3D in a native window;
the3000frame window run ends normally. Fifteen headless/window Vulkan frame
captures agree exactly. This is renderer/execution progress, not stable racing
or PS2 fidelity. CPU/GPU pixel differences after frame300 remain open.

Trade-offs: synchronous operation, only scale1, nearest4:3 letterbox blit,
no temporal interlace blending, no GPU busy measurement, no audio/XInput.
Only this RTX3060Laptop/Windows driver has been exercised; the current pilot
requires maintenance1. No installed Khronos validation layer was available.
These limits are explicit rather than changes to production semantics.

Next: qualify the first pixel divergence and real race/loading behavior,
add validation-layer evidence, then optimize confirmed bottlenecks. Native
audio, XInput, wider save coverage and production adoption remain subsequent
work. See [evidence](../reverse-engineering/slice111-native-vulkan.md) and
[pinned metadata](../inputs/slice111-native-vulkan.json).
