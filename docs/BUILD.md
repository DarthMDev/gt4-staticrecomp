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
Slice130's optional runtime fixture is enabled with `GT4_SHAPE_RUNTIME_SOURCE`
pointing to a fresh capsule made by `scripts/prepare_shape_observation.py`.
Default is empty. CMake accepts only the reviewed three-source hashes, prevents
shadow headers and rechecks changed private sources before rebuilding. The game
target stays unoverlaid; synthetic observed/reference fixtures exercise runtime
lifecycle and selected guest outputs. Fresh130 CTest5/5/Python147/147, no skips.
This is not a playable probed-library profile. See [Observation](SCENE-OBSERVATION.md).

Slice131 builds the complete two-probe library with the same `tools/ee_producer`
project and translator disabled, in a fresh producer cache. Use
`scripts/ee_observation_library.py prepare --baseline <qualified-baseline>
--profile <qualified129-profile> --output private/<fresh-library-capsule>` first,
then configure/build `ps2game` with the copied generated directory and run
`record-library --output <that-capsule>`. Both commands validate the pinned
baseline and exact probe-only sources; recording also checks all archive members,
the unchanged compiler recipe and D5's independent129 normalized object hash.
Do not reuse frozen131 output or feed this profile to the ordinary null-option
producer recorder. CTest1/1 runs Python155/155 with frozen130 native fixtures;
runtime linkage and owner gameplay remain separate qualification boundaries.

Slice132 adds the opt-in `-DGT4_EE_PROFILE=observation` native profile, together
with `-DGT4_SHAPE_RUNTIME_SOURCE=<fresh-reviewed-overlay-capsule>`. It selects
only the pinned131 library and qualified118 port/shaders. Configure into a fresh
private build directory and build/test serially as usual. The game links the three
overlays and observer directly, preserving the original runtime archive and paired
unoverlaid synthetic control. CMake revalidates library/source changes. A separate
`observation-game.map` records actual application linkage; Python CTest verifies
selected bindings and source/library graph. Fresh132 CTest5/5/Python155/155 passes.
Capture is disabled; there is no activation/export interface or gameplay validation
yet. The normal build command and production launcher still select production.

Slice133 adds `shape_capture_export_fixture` and a serial native CTest entry.
The fixture uses a uniquely created temporary directory with synthetic metadata,
removing its files on success. Python parses the actual three JSON reports.
Reconfigure a fresh native build to include this sixth test; frozen132 caches stay
unchanged. Fresh133 CTest6/6/Python156/156, zero skips. The export component is not
linked into the game and requires no new dependency. Activation/output path
selection and a separate owner-facing capture launcher remain future work.

Slice134 links capture control/export only into the explicit observation profile.
Fresh134 build adds a seventh CTest entry and passes Python164/164 with zero skips.
Baseline graph checks exclude those game objects/macros; production was not rebuilt.
`scripts/play_shape_capture.py --assets-root <owner-assets-checkout>` selects the
qualified134 manifest and reuses the existing manual card/backup/lock flow.
Use the tooling Python and run from the launcher checkout containing the frozen
private134 runtime and118 shaders. Output belongs to the owner-assets checkout's
fresh `private/manual-play/session-.../shape-capture.json`, never a producer capsule.
The native window consumes the explicit `GT4_OBSERVATION_OUTPUT` provided by that
launcher; generic launches clear any inherited value. Invalid/existing native output
fails closed before emulation. Default production launch remains unchanged.
See [manual instructions](SCENE-OBSERVATION.md#owner-controlled-capture-slice134).

The current manual launcher selects the separately qualified136 candidate via
`docs/inputs/slice136-keyboard-input.json`, replacing its active134 selection without
overwriting that frozen build. It includes the minimal keyboard merge correction:
arrows emit digital D-pad only; keyboard never forces analog axes. Fresh136 native
CTest7/7/Python164/164 passes; the unchanged fixture rejects the old merge code.
All seeds/shaders/library pins and save/backup/lock behavior are preserved.
Owner must close the current window normally before launching the corrected build.
