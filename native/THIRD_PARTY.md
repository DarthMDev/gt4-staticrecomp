# Active native runtime: third-party boundaries

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
does not mean zero emulation. Current VU programs are interpreted. The original
MSVC EE engine is archived, while this Clang/native path is now main's active
implementation. Game, BIOS, translated game code, captures, cards, SPIR-V and
native binaries remain private and ignored. The pinned game's generated static
library is linked locally; it is not redistributed.

Slice 119 adds six ignored, unmodified reference clones for offline format
research only. [Nenkai References](../docs/NENKAI-REFERENCES.md) records exact
revisions, root license observations and dependency limits. No reference tool,
package or implementation was built, copied into this runtime or linked. GT4FS
and the Modding Hub have no root license in the inspected checkouts; their
format facts are research references, not adopted dependencies. MIT tool/template
licenses and the separate SharpTriStrip BSD terms remain with their local sources.
