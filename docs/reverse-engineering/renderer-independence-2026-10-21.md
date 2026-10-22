# Renderer independence: branch audit and launcher contribution

Date: 2026-10-21. TASK-089. Baseline `6422988e35383a7d9242443db356a03bacaf6ec5`.

## Findings and scope

**Confirmed:** the built-in MIT CoreGS 1.1 is already selectable and launches with
`GT4_RENDERER=donor`. However, setup always invoked the PCSX2 GS installer, and
startup automatically repaired its missing/outdated package and requested its
release list even when CoreGS was explicitly selected. This contribution makes
those automatic operations conditional on the selected renderer. Launcher
updates remain enabled; explicit manual update actions retain both streams.

**Confirmed:** `prepare_play` now skips the unused external executable lookup for
CoreGS. The PCSX2 default, package hash verification when used, and rendering
implementations are unchanged. The change removes unwanted package/network work;
it does not improve CoreGS image quality or qualify it as the preferred renderer.

## Branch snapshot

These counts are main-only / branch-only commits, measured against the pinned
baseline above. They describe the fetched snapshot, not future branch state.

| Branch | Commit | Counts | Assessment |
|---|---|---|---|
| `task/026-hw-renderer-spec` | `7d66fc73e26a5b81d3615ef737f80e3979b11ee6` | 226 / 0 | Specification already merged. |
| `task/027-coregs2` | `af0c932ecdd3b9bc41a71b99bc4b8dd494d5ecef` | 29 / 66 | Active clean-room MIT implementation; evaluate here rather than combining historical components again. |
| `task/027-hw-renderer` | `80c87a22efa982c7196720139d607c70f22e2647` | 225 / 45 | Ancestor of `task/027-coregs2`. |
| `task/027-cpuref` | `03bdd0d297417498d87a19144eeb1962026ae7ea` | 225 / 46 | Ancestor of `task/027-coregs2`. |
| `task/027-m4-gpuref` | `e31fcc4586fd69304e1fbf17c61ad1a5a7ce9b30` | 225 / 46 | Not an ancestor; branch progress says the GPU path was ported. |
| `task/028-donor-upscale` | `8f480865b3a7f0681bae38cdde8c88842f683487` | 225 / 7 | Older enhancement candidate; aliasing limitations recorded in TASKS. |
| `task/030-night-readback` | `a7b844f987a96a72f5832afbf6dc5fd34162c0dd` | 225 / 3 | Older readback experiment; no demonstrated production wait reduction. |
| `native-renderer/main` | `495e3ff88d4d2f9f258d6c51e261f6b3d7425df6` | 116 / 1 | Separate-project plan, not a replacement implementation. |
| `linux/main` | `d38c635afd75c22315471725b1d9446a4c9a6953` | 110 / 1 | Linux plan, not a renderer replacement. |
| `task/088-custom-textures-120` | `39cd21bde26be3b59f5650b1ddbf77a8ff238509` | 0 / 2 | Parallel release/custom-texture work; no cherry-pick needed here. |
| `task/086-progressive-480p` | `a3873a129bbf130c40b8b08ed16021cacc9e6e0b` | 6 / 1 | Experimental progressive-menu work, separate scope. |
| `task/085-launcher-self-update` | `bb31bc3df2fca8908022e1585375c153bd557fc7` | 9 / 0 | Already merged. |
| `task/080-aspect-ratios` | `1c941f66df874ffcbf798291db7253880874ce37` | 14 / 0 | Already merged. |

The examined `task/027` m0, m0b, m1, m2, m3, m4-shaders, m5, m7, m9, m13
and m16 component branches are ancestors of `task/027-coregs2`.

**Confirmed as documentation, not reproduced gameplay:** the CoreGS2 branch's
`docs/STATUS.md` records resumption on 2026-10-21 despite main's older paused
entry. Its `docs/hw-renderer-spec/PROGRESS.md`, section IN-344..346, reports
approximately 14 ms/frame for 2D menus, 100 ms for the car menu and 113 ms for
the race; CPU texture decode costs about 25 ms and write-backs about 33 ms per
race frame. It reports 42/42 identical menu comparisons and a worst race
comparison of 0.96% bad pixels / 36.0 dB PSNR within that branch's gate.

**High confidence:** the next substantial renderer effort belongs on CoreGS2,
targeting measured texture decode/write-back costs and keeping its fidelity
gates. Wholesale adoption here would combine a large implementation and known
performance limitations with unrelated launcher work. **Unknown:** its current
behavior on this contributor's GPU; no native game build or gameplay claim is
made by this launcher-only contribution. No PCSX2 implementation sources were
opened and no GPL renderer code was copied or linked.

## Reproduce the audit

```text
git rev-parse origin/main origin/task/027-coregs2
git rev-list --left-right --count origin/main...origin/task/027-coregs2
git log -1 --format="%H %ad %s" --date=iso-strict origin/task/027-coregs2
git merge-base --is-ancestor origin/task/027-hw-renderer origin/task/027-coregs2
git show origin/task/027-coregs2:docs/hw-renderer-spec/PROGRESS.md
```

Repeat the count/ancestry commands for each branch above. Branch comparisons
and the performance report were reviewed independently before selecting scope.

## Verification

The launcher tests use a synthetic ISO, not game payload. The new full setup
control injects an installer that panics if called, checks that setup succeeds
without external download events/package folders, persists and reloads the
CoreGS choice, then verifies the launch plan has no external GS process.
Injected release-fetch tests check launcher-only automatic requests, continued
launcher update offers, default PCSX2 repair, and both manual update streams.

**Confirmed:** `cargo test --locked --offline --workspace` passed for the full
launcher Rust workspace on Windows x64, with Rust/Cargo 1.99.0 and VS2022 x64.
Tauri: 53 passed, one real-disc flow ignored. Other existing explicit ignores
include real-disc and Vulkan-probe tests. Five existing Tauri unused-import/dead
code warnings remain; no new warning was introduced by the changed functions.

Run from `launcher/` inside the VS x64 environment, setting `GT4_SDL3_ROOT` to
the absolute `private/tooling/sdl3-3.4.18` folder. Both official SDL3 packages
were hash-checked against `docs/inputs/slice210-sdl3.json`. The first build's
default SDL path pointed outside the new clone; explicitly setting the documented
environment variable fixed it. A sandboxed run refused two existing atomic
version-file writes; the unrestricted run of the same suite passed.

No CTest/native game build was run: this clone lacks the pinned private game
library and generated sources; no native source changed. Maintainer launcher
acceptance remains separate. The shared ai-memory server was unavailable;
canonical documents and Git history were used.
