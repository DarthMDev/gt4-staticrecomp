# Active native runtime: third-party boundaries

## Planned PCSX2 GS reuse on the hybrid track

Slice141 records owner authorization to reuse selected PCSX2 implementations
for future GS optimization/features. Research checkout is pinned at
`aa7ab4306e269075784c7ac3eb4b45e6e6c53445`; inspected GS files specifyGPL3-or-later.
No implementation is imported/linked and no license changes in this planning
slice. Any derived integration needs per-file/dependency attribution, compatible
licenses and corresponding-source/build delivery when required. Personal builds
and distribution have distinct obligations; game assets/translated game code
remain private. See [Hybrid Port Plan](../docs/HYBRID-PORT-PLAN.md).

## Current linked base

The frozen ps2-web-recomp checkout is MIT licensed, commit
`1b4808793a12c45125262fd2d5f50a9ec9f7c020`. Its existing CPU runtime,
batching and WGSL GS algorithms are reused in a separate private build.
`scripts/prepare_external_vulkan.py` verifies donor source hashes, derives
the adapter under `private/`, and retains the original checkout unchanged.
The original MIT copyright/license at the checkout root applies to these
derived files and must accompany any distribution of the adapter.

Naga 30.0.0 converts the donor WGSL to nine SPIR-V compute entrypoints;
it is MIT OR Apache-2.0 licensed. `tools/wgsl_spirv/Cargo.lock` pins the
dependency graph and registry checksums. Cargo output stays under `private/`.

Khronos Vulkan headers have their own Apache-2.0 OR MIT license. The private
headers available in the pinned PCSX2 source tree are used only as headers;
no PCSX2 implementation or PS2Recomp GPL implementation is linked here.
Vulkan is resolved from the Windows System32 loader and installed driver.

The donor IOP interpreter and PS2 device models are still used; native Windows
does not mean zero emulation. VU1 programs are compiled ahead of time since slice 151; VU0 micro is interpreted.
This Clang/native path is main's only implementation. Game, BIOS, translated game code, captures, cards, SPIR-V and
native binaries remain private and ignored. The pinned game's generated static
library is linked locally; it is not redistributed.

Slice 119 adds six ignored, unmodified reference clones for offline format
research only. [Nenkai References](../docs/NENKAI-REFERENCES.md) records exact
revisions, root license observations and dependency limits. No reference tool,
package or implementation was built, copied into this runtime or linked. GT4FS
and the Modding Hub have no root license in the inspected checkouts; their
format facts are research references, not adopted dependencies. MIT tool/template
licenses and the separate SharpTriStrip BSD terms remain with their local sources.

Slice 120's offline RoFS/PS2ZIP reader is an original implementation using
referenced format facts, with independent synthetic fixtures. No GT4FS source
was copied. The private SpecDB probe references MIT PDTools and remains ignored;
it is not imported into the runtime or distributed as a supported decoder.
[Volume Reader](../docs/VOLUME-READER.md) and the pinned Nenkai source map record
the supported subset, attribution and qualification limits.

Slice 121 adds an original offline CAR4/MDLS/Tex1 header probe grounded in the
same pinned MIT templates and PDTools format facts. No reference implementation
or dependency is copied or linked. [Model Asset Probe](../docs/MODEL-ASSET-PROBE.md)
records actual-byte discrepancies, independent review and the header-only boundary.
Slice122's original fixed-record inventory uses the same pinned MIT format facts.
No implementation copied or linked; [Model Records](../docs/MODEL-RECORDS.md)
documents sources and the boundary before VIF, material or pixel semantics.
Slice123 uses pinned MIT donor and Nenkai format facts for an original conditional
VIF framing scanner. No source implementation copied or newly linked. Its source
corrections and unsupported forms are documented in [VIF Framing](../docs/VIF-FRAMING.md).
Slice125's original raw-lane extraction uses ordinary endian/width facts from
the same pinned MIT sources, with literal independent controls. No dependency
or implementation is copied or newly linked. [Raw Lanes](../docs/RAW-CHANNEL-LANES.md)
keeps numerical inputs separate from game geometry or VU semantics.

Slice130 prepares private copies of MIT donor dispatch/kernel/system sources at
the same pinned revision, adding observer-only calls for an optional synthetic
fixture. Root donor license/attribution remains with the local source checkout;
no new dependency or GPL implementation is introduced. The default game target
uses the existing donor sources. [Shape Observation](../docs/SCENE-OBSERVATION.md)
records exact inverse checks and synthetic qualification limits.

## VU1 recompiler tooling (slice 151)

`tools/vu_recomp/src/vurecomp.cpp` is derived from `recomp/vurecomp.cpp` of the
MIT ps2-web-recomp donor, commit `1b4808793a12c45125262fd2d5f50a9ec9f7c020`
(donor file SHA-256 `07aefe0130e4866973c43467853181edfc31b67b97e71fc9b572ece389381323`,
Copyright (c) 2026 Samir Nuri). Its decode tables are kept from the donor; the
hardening changes are listed in the file header. The donor header
`runtime/include/ps2/vu_ops.hpp` is included unmodified (MIT). The MIT text is in
`tools/vu_recomp/MIT-LICENSE.txt`. `tools/vu_recomp/src/sha256.cpp` and the
fixtures in `tools/vu_recomp/tests/` are original code. The emitter output is
not linked into the game; the provider is a later, separate step.

## paraLLEl-GS and Granite (slice 162, TASK-012)

Off by default (`GT4_PARALLEL_GS=OFF`). When enabled, `native/parallel_gs.cmake` builds these
pinned sources from `private/references/parallel-gs` (fetched by `scripts/fetch_parallel_gs.py`,
verified against [parallel-gs-pin.json](../docs/inputs/parallel-gs-pin.json)). No vendor file is
modified; GT4Recomp's own code is in `parallel_gs_backend.*`, `gs_video_mode.*` and the fixtures.

| Component | Commit | License | Use |
|---|---|---|---|
| paraLLEl-GS (Arntzen Software AS, Hans-Kristian Arntzen, Runar Heyer) | `cc6184af7e0c03da603045ca371ffa5dae9b0655` | LGPL-3.0-or-later (`COPYING.LGPLv3` in the checkout) | GS renderer library, linked statically |
| Granite (Hans-Kristian Arntzen) | `16e7395f6a4858c1783dbf6f521f90b9d5f82ac5` | MIT | Device, allocator, timelines: `granite-vulkan`, `granite-util`, `granite-math` are built; renderer, shader compiler, SDL, tools and DXGI are off |
| volk (Arseny Kapoulkine) | `47cddf7ed97b94118a08aacb548a411188e016cc` | MIT | Vulkan function loading inside Granite |
| Vulkan-Headers (Khronos) | `11d6898377797e07dbd543aaaa367e4465074597` | Apache-2.0 OR MIT | Vulkan headers used by Granite; not the PCSX2-tree headers |

Notes:

- The paraLLEl-GS shader header `gs/shaders/slangmosh.hpp` is prebuilt SPIR-V (sha256 of the LF blob
  9f9cf941...). No GLSL compiler is built or used.
- Vendor targets are compiled with `-w` so their diagnostics do not reach the GT4Recomp logs; our
  targets keep `/W4` (`/W3` for the backend unit, which includes Granite headers).
- The vendor CMake forces `/MT` unless `CMAKE_MSVC_RUNTIME_LIBRARY` is set first; the build sets
  `/MD` (CMP0091 NEW) to match the rest of GT4Recomp.
- LGPL obligations: paraLLEl-GS is linked statically into the renderer. Any distributed binary
  with this option needs the object files or full corresponding source and build scripts so users
  can relink, as LGPL-3.0 section 4 requires. No binary is distributed by this slice. The plan
  ([PARALLEL-GS-PLAN.md](../docs/PARALLEL-GS-PLAN.md)) requires an owner decision on distribution
  before any public binary; none is recorded yet.
- Local patches to paraLLEl-GS, if any, will be kept as documented patch files here, not pushed upstream.
