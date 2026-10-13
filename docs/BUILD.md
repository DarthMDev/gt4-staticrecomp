# Current Path Build Guide

## Qualified Environment

Windows x86-64, VS2022 Build Tools / SDK 10.0.26100.0, clang-cl 23.1.2,
CMake 3.24+, Ninja, Rust/Cargo with Cargo.lock, and Naga 30.0.0.
Local Python in `private/tooling-venv` with `scripts/requirements.txt`
(pycdlib 1.20.0). The qualified GPU is an RTX 3060 Laptop; the backend requires Vulkan 1.2
and swapchain maintenance1. There is no evidence of full Khronos conformance validation.

`native/build.cmd` initializes the VS x64 environment and sets UTF-8 encoding prior to invoking CMake.
This Clang build replaces the legacy MSVC build from the original engine.

## Private Prerequisites

| Location | Role |
|---|---|
| `private/references/ps2-web-recomp` | MIT donor at commit 1b4808793a12c45125262fd2d5f50a9ec9f7c020 |
| `private/reference-acceleration-study/runtime-build/ps2game.lib` | Ahead-of-time (AOT) game library, SHA256 `c837d13dae027f377aecdb0c39834ede2e9463eabf7f767fda6a3c5a0762de1f` |
| `private/reference-acceleration-study/generated` | 326 C++ EE compilation units and registry that produced this library |
| `private/tooling/pcsx2-slice101-source/3rdparty/vulkan/include` | Khronos headers; no PCSX2 implementation code is linked |
| `private/tooling-venv/Scripts/python.exe` | Python virtual environment |
| Root ISO, `private/fingerprint-check/CORE.GT4`, `private/reconstructed/SCUS_973.28.elf` | Hash-pinned inputs in `docs/inputs` |
| Private slice 110/113 seeds | Memory cards derived from the original interface; never distributed |

The build validates the donor adapter sources and rejects an AOT library with a mismatched hash.
The launcher verifies ISO / CORE / ELF / runtime / shaders / card seed before execution.
Pins must not be updated to mask an underlying divergence.

**Reproduction limit:** The [P0 EE producer](EE-PRODUCER.md) now reconstructs the
pinned ELF from verified CORE/ISO input, rebuilds the donor translator and creates
a separate AOT library with fresh CMake caches. It does not yet replace the
production library or automate a clean-checkout playable game build. The ordinary
native build still recompiles runtime/host/adapters and links the existing qualified
library. Keep candidate and production qualification distinct; the archived
original engine is not used to run the game.

Slice 118 adds [isolated baseline/recovered EE builds](EE-CANDIDATES.md) with
separate library/executable manifests and a candidate launcher. The default build
still uses the production pin. Both paths require prepared private assets;
candidate native checks are distinct from owner gameplay qualification.

## Compiling and Verification

From repository root:

```powershell
cmd /c native\build.cmd
ctest --test-dir private/slice114-native-base/build --output-on-failure
```

CTest runs the GPU/Vulkan checks, Windows audio/input tests, and active Python test suite.
The 54 C++ tests from the original engine were archived alongside the removed code,
without modifying their assertions. This changes the test population; it does not make
their historical passing results proof of donor runtime correctness.

Running Python tests standalone, when needed:

```powershell
private/tooling-venv/Scripts/python.exe -m unittest discover -s tests/python
```

Execute builds and tests serially. Linkers may lock binary outputs, and an active manual play
session holds an exclusive lock on the memory card. Do not terminate the owner's active game session unnecessarily.

## Producers and Launcher

This slice outputs adapter / shaders / build artifacts to `private/slice114-native-base/`,
without overwriting producers from slices 111–113. The launcher references
`docs/inputs/slice114-native-base.json`. Publishing a new slice requires successful builds, passing tests,
and verified hashes; compiling modified sources does not authorize repinning the executable.
Future slices must select fresh directories before building and preserve previously accepted
producers. Binaries, SPIR-V files, and generated code remain untracked by Git.

`Jogar-GT4.cmd` manages `private/manual-play/card.ps2`, per-session backups, and exclusive
locking. It never reseeds an existing memory card. When initializing a new card,
it uses the 16:9 or 4:3 seed corresponding to the `--aspect` argument.

## Offline selected-asset research

[Volume Reader](VOLUME-READER.md) documents the bounded, hash-verified
`scripts/gt4_volume.py` command. It needs the tooling Python/pycdlib and owner
ISO, not a native rebuild. It writes only explicit selections to a fresh
ignored capsule and preserves all inputs/pins. Its public synthetic tests are
included automatically in existing Python discovery/CTest. SpecDB parsing in
slice 120 remains a private experiment, not a general supported decoder.

[Model Asset Probe](MODEL-ASSET-PROBE.md) adds a pure `probe_asset(bytes)` header
inventory in slice 121. It consumes separately verified, explicitly extracted
CAR4/MDLS input; no native rebuild or runtime integration. Its public controls
also run in Python discovery/CTest. Model and texture bodies remain unqualified.
Slice122 adds `gt4_model_records.inventory_asset(bytes)` for fixed-record and
packet-span metadata. Its controls use the same discovery; no VIF/pixel decoder
or native build is involved. See [Model Records](MODEL-RECORDS.md).
Slice123's `gt4_vif_framing.frame_packet(bytes, initial_cycle=(CL, WL))` is a
conditional offline scanner, included in existing controls. No live incoming
cycle or VU/channel semantics are qualified. See [VIF Framing](VIF-FRAMING.md).
Slice125's `gt4_raw_lanes.decode_integer_lanes` consumes explicitly framed
ordinary input integers with exact data/alignment spans. Public controls use
the same discovery; it adds no runtime decoder. See [Raw Lanes](RAW-CHANNEL-LANES.md).

Slice128 adds `shape_observer_fixture`, a host-only C++ contract control included
in native CTest and Python discovery. Reconfigure a fresh candidate build to
include the new fourth CTest entry; frozen118 caches do not contain this target.
The fresh128 build uses the existing qualified baseline library and private118
port/shaders. No observer is linked into gt4vulkan. CTest4/4 and Python132/132
pass with no skips. See [Shape Observation](SCENE-OBSERVATION.md) for scope.
Slice129 separately regenerates the exact two-probe source profile and compiles
only changed D5 in a fresh unit-control cache. Producer CTest1/1 runs Python141/141
using128 native fixtures; no full probed-library/runtime build is claimed.
