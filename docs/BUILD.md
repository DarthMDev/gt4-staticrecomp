# Current Path Build Guide

Slice142 adds host-only `perf_accounting_fixture` to CTest. It uses authored
timestamps and requires no game/input/performance run. It does not replace the
player's probe or runtime. See [attribution qualification](PERFORMANCE-ATTRIBUTION.md).

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

Slice143 is a separate performance-only production-EE profile. Prepare fresh
three-source runtime overlays with `scripts/prepare_perf_runtime.py` (see its
`--help`); source hashes must match the clean pinned donor. Configure a fresh
cache with `GT4_PERF_RUNTIME_SOURCE` pointing to that immediate private capsule,
`GT4_EE_PROFILE=production`, empty `GT4_SHAPE_RUNTIME_SOURCE`, and
`GT4_PORT_DIR`/`GT4_SHADER_DIR` pointing to the qualified143 port/shaders. The
strict recipe uses `docs/inputs/slice143-performance-runtime.json`; fresh outputs
must be independently pinned before changing that metadata. Do not overwrite143.
Default profile still has no linked probe/hooks. Macro-off observation controls
can separately include shape runtime fixtures; performance and shape profiles
cannot be combined. Candidate native controls7/7 and separate full controls9/9
with Python171/171 pass. This still depends on the private production library,
not an end-to-end clean checkout build. Launch with `scripts/play_performance.py`
and the owner-assets root, using the existing protected manual card flow.
See [measurement instructions](PERFORMANCE-ATTRIBUTION.md#playable-diagnostic-candidate-slice143).

Slice145 reuses the frozen143 sources/port/shaders strictly as shared pinned inputs
and builds into separate fresh145 detailed/control caches. `GT4_PERF_FRAME_ONLY`
defaults OFF; ON requires the performance runtime profile, propagates consistently
to all linked TUs and compiles subsystem scopes/host-operation counters out. Common
frame-boundary telemetry remains, so this is not an entirely uninstrumented build.
The matched native profiles pass7/7 each; separate macro-off observation controls
pass9/9 with Python173/173 zero skips. `scripts/play_performance.py --mode detailed`
(default) or `--mode frame-only`, plus `--assets-root`, selects the corresponding
strictly pinned145 runtime. Existing143 profile validation remains available;
frozen143 caches/binaries/cards are not overwritten. Close sessions normally.

Slice149 adds `prepare_perf_runtime.py <fresh-capsule> --assets-root <assets>
--split-vu` for the dedicated immediate source capsule. Default derivation remains
byte-identical to143. CMake selects149 pins and GT4_PERF_VU_SPLIT only at
private/slice149-vu-split-runtime; frame-only rejected. Fresh149 build reuses frozen
143port/shaders as immutable inputs with production EE. Native7/7 and separate
full9/9 with Python175/175 pass. Explicit `play_performance.py --mode vu-split`
selects149; default detailed/frame-only still select145. See attribution guide.

Slice151 adds an owner-launched VU1 recording mode on the slice149 vu-split binary.
Owner command, from the repository root:

    private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode vu1-record

The mode pins [slice151-vu1-record.json](inputs/slice151-vu1-record.json) to
`private/slice149-vu-split-build/build/gt4vulkan.exe`. After the performance
scrub of inherited `PS2_` options, it sets `PS2_VU_RECORD` to a fresh
`vu1-corpus` directory in the session directory and `PS2_VU_COUNT=1`. Stderr
stays in `probe.log`. At exit, `vu1-corpus-summary.json` lists file count, bytes,
per-file sha256, key and entry parsed from names, and rejects any file that is
not exactly 16384 bytes. Card backup, session lock and default modes are
unchanged; default modes never set the recording variables. This is recording
only: no AOT, no timing claim, and the owner controls gameplay.

Slice152 adds asynchronous GS submission (O2) on the slice151 VU1 AOT runtime
capsule. Fresh pins only: adapter `private/slice152-gs-async-port`, shaders
`private/slice152-gs-async-shaders` (the nine unchanged slice143 SPIR-V files),
build `private/slice152-gs-async-build/build`. Splice command, from the repository
root:

    private\tooling-venv\Scripts\python.exe scripts\prepare_external_vulkan.py private\slice152-gs-async-port

The build is configured like slice151 with the 152 adapter and shader paths
(`GT4_PORT_DIR`, `GT4_SHADER_DIR`), the slice151 runtime capsule
(`GT4_PERF_RUNTIME_SOURCE`) and the generated VU1 table (`GT4_VU1_GENERATED_DIR`).
Runtime pins are [slice152-gs-async-runtime.json](inputs/slice152-gs-async-runtime.json)
(port and shader hashes) and [slice152-gs-async.json](inputs/slice152-gs-async.json)
(launcher). CMake refuses any other port or shader directory on the performance
profile. Owner command: `python scripts/play_performance.py --assets-root . --mode gs-async`.
`GT4_GS_SYNC=1` in the environment restores the synchronous GS path for A/B.

Slice153 adds frame pacing (O4) on the slice152 async build. Fresh pins only: adapter
`private/slice153-pacing-port` and shaders `private/slice153-pacing-shaders` (copies of the
slice152 files, byte-identical), build `private/slice153-pacing-build/build`, runtime capsule
unchanged (`private/slice151-vu1-aot-runtime`). Configure with the slice152 cache values but
`GT4_PORT_DIR` and `GT4_SHADER_DIR` pointing at the slice153 directories; CMake accepts them only
with `GT4_PERF_RUNTIME_SOURCE` on the 151 capsule and reads
[slice153-pacing-runtime.json](inputs/slice153-pacing-runtime.json). Launcher pins are
[slice153-pacing.json](inputs/slice153-pacing.json). Owner command, from the repository root:

    private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode pacing

The host paces guest VBlanks on one wall-clock grid at 60 Hz (the donor's `frame_cycles`
is `kEeClock / 60`, NTSC 60.000 Hz, not 59.94). Audio no longer blocks the emulation
thread: 16 x 256-frame WinMM buffers, a 16384-frame ring and a 60 ms (2880-frame) target,
with a linear resampler whose ratio stays within +-0.5 percent under a PI controller. Set
`GT4_PACING=audio` to restore the blocking audio clock of slice152 for A/B. Unknown values
of `GT4_PACING` or `GT4_PRESENT_MODE` are refused by the launcher and by the host.
`GT4_PRESENT_MODE` accepts `fifo` (default), `mailbox` or `immediate` and fails at swapchain
creation when the surface does not support it. Both variables are forwarded unchanged and
recorded as `pacing_environment` in the run result. Headless runs (`--frames`) never enter the
window path, so they are unpaced and their frames are unchanged.

Checks on this build: `native_pacing` (pure fixture), `native_audio_input` (includes a 3 s paced
run on the real device), CTest 18/18, Python 217 OK with `GT4_NATIVE_BUILD` set to the slice153
build, and 42 identical headless frame dumps against the slice152 build. Owner gameplay
measurement is pending. Diagnostics print in F8 and the periodic report when `GT4_PERF=1`.

Slice154 adds per-program VU1 flag elision on the slice153 pacing build (O1 follow-up). Fresh
pins only: capsule `private/slice154-vu1-flags-runtime` (derived with
`scripts/prepare_perf_runtime.py --vu1-aot`, overlay sha256 in its `runtime.json`), table
`private/slice154-vu1-flags-gen` (generated from `private/slice151-vu1-corpus-union`, 100
programs, 0 refused), build `private/slice154-vu1-flags-build/build`. The adapter and the nine
shaders are the slice153 directories, unchanged. Pins: [slice154-vu1-flags-runtime.json](inputs/slice154-vu1-flags-runtime.json),
[slice154-vu1-flags.json](inputs/slice154-vu1-flags.json). Configure with the slice153 cache values,
`GT4_PERF_RUNTIME_SOURCE` and `GT4_VU1_GENERATED_DIR` pointing at the slice154 directories, and
`GT4_VU_CORPUS_DIR` at the corpus. Regenerate the table with
`gt4_vu_recomp <corpus> <fresh_dir>`; `--no-flag-elision` (and `--namespace NAME`) emit the donor
behaviour for comparison. Owner command, from the repository root:

    private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode vu1-flags

`--vu1-verify N` and `--vu1-interpret-only` apply as in the vu1-aot and pacing modes. The
pacing environment variables (`GT4_PACING`, `GT4_PRESENT_MODE`) are unchanged.

What changes. The emitter computes MAC only where a later reader or the program exit can see
it. A MAC reader sees a write only at distance 4 or more with no later write before it, and
clip readers at distance 3 or more (FlagPipe windows). The exit state stays exact: `r.mac` and
`r.clip` are written back at every exit. VU1 status is not computed when the image has no
status instruction (0x14-0x17 and FSSET 0x15). CFC2 reads VU0 registers only, so the EE cannot
observe VU1 status; the corpus image has no such instruction. The clip word is a chain through
every CLIP instruction (`clip_value` reads the previous clip), so clip values are always
computed and only the FlagPipe pushes are elided. Programs with an end bit in a branch delay
slot are refused: the donor runs the target once more and then ends.

Evidence on this build (Confirmed unless marked):

- Sites, 100 programs of the corpus: 6,024 flag-setting writes. Before: 6,024 MAC computations
  with status and 6,024 FlagPipe pushes. After: 642 MAC computations (170 pushed for readers,
  472 for the exit state only) and no status. 607 CLIP writes compute their chain value; 403
  are pushed (was 607). Manifests: `private/slice154-vu1-flags-gen/manifest.json` (mac_mode,
  mac_writers, mac_sites, mac_reader_sites, clip_*).
- Micro-benchmark `gt4_vu1_flag_bench` (CTest does not run it): 200 seeded states (100
  programs, 2 seeds, 4,000-instruction budget). Both tables leave the same status, resume PC,
  registers and VU1 memory (200 compared, 0 failures). Timed: the 68 states that exhaust the
  budget inside generated code (the other 132 leave through an interpreter hand-off and are
  not timed). Two runs: 9,972 and 10,044 ns per call before, 4,681 and 4,674 after; ratio 0.47.
  Random states, not recorded gameplay: an estimate of the direction, not of the showroom time.
- CTest: see the journal entry. Corpus differential 208 checks, 0 failures (100 programs). Fixture
  differential 907 checks, 0 failures, with 40 budget exits resumed in the interpreter.
- Headless smoke: 4,200 frames, the slice153 schedule (12 presses), no window: 42 frame dumps
  identical to [slice153-smoke](../private/slice153-smoke/headless-153.frames.sha256) (run
  `private/slice154-smoke/final-headless`).
- Headless verify: `GT4_VU1_VERIFY=1` on the same schedule, every VU1 call compared with the
  interpreter: see the journal entry for the counters and mismatch count. Frames identical to
  the smoke run.

Limits. Owner gameplay is not measured; the O1 target (showroom VU1 time) is not claimed. The
known limit of the AOT path stays: an interpreter hand-off or a budget resume does not carry
the FlagPipe history, so a MAC or clip read within four instructions after the hand-off can
differ from an uninterrupted run. Random seeds reach the end of a VU1 program rarely, so the
benchmark uses the budget as the workload. Build outputs and card seeds are not committed.

Slice155 (TASK-008) gives the VU1 AOT table full static entry coverage on the slice154
flag-elision profile. No gameplay recording is needed for the table. Owner command, from the
repository root:

    private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode vu1-full

`--vu1-verify N` and `--vu1-interpret-only` apply as in vu1-flags. Pins:
[slice155-vu1-image.json](inputs/slice155-vu1-image.json) (static image),
[slice155-vu1-full-runtime.json](inputs/slice155-vu1-full-runtime.json) (runtime capsule, receipt
identical to slice154) and [slice155-vu1-full.json](inputs/slice155-vu1-full.json) (launcher).
Build directory `private/slice155-vu1-full-build/build`; table `private/slice155-vu1-full-gen`;
capsule `private/slice155-vu1-full-runtime`. The adapter and the nine shaders are the slice153
directories, unchanged.

Steps. All outputs are generated offline in fresh private directories and are not committed.

1. `scripts\vu1_image.py build --elf private\reconstructed\SCUS_973.28.elf --corpus private\slice151-vu1-corpus-union --out private\slice155-vu1-image`
   decrypts the blob (file offset 0x525DD0, length 0x3FF0; keystream in
   [VU1-MICROCODE-SOURCE](VU1-MICROCODE-SOURCE.md)), appends the 16 tail bytes from the snapshots
   (all 86 agree: zeros) and refuses any snapshot that differs. `check --image` verifies the pin.
2. `scripts\vu1_image.py entries --image private\slice155-vu1-image\vu1_image.bin --policy targeted --table-manifest private\slice154-vu1-flags-gen\manifest.json --out private\slice155-vu1-image\entries-targeted.txt`
   writes the entry list (426 addresses).
3. `gt4_vu_recomp --image private\slice155-vu1-image\vu1_image.bin --entries private\slice155-vu1-image\entries-targeted.txt private\slice155-vu1-full-gen`
   emits the table. Explicit mode: `--image` and `--entries` replace the corpus directory. All
   safety gates are unchanged (identity digest, budget and resume, flag elision with the
   image-wide status and MAC rules, collision refusal). The manifest records `input`, the image and
   entry-list hashes, `entry_count`, `function_count` and `generated_bytes`.
4. `scripts\vu1_image.py corpus --image ... --entries ... --out private\slice155-vu1-full-corpus`
   writes one KEY_ENTRY.bin per entry for the corpus differential. The emitter re-derives every key
   in corpus mode and refuses a mismatch. The corpus-mode table equals the explicit table (426
   identical key, entry and digest rows).
5. Runtime capsule: `scripts\prepare_perf_runtime.py private\slice155-vu1-full-runtime --vu1-aot`.
   The overlay hash is now computed over LF-normalized text, because a CRLF checkout
   (core.autocrlf) stores the same overlay with other bytes. The pinned receipt is unchanged
   (84eed2a3...). The overlay itself is not changed.

Entry policy decision. Numbers are Confirmed: each table was compiled standalone with the native
flags (clang-cl /O2 /Ob2 -std:c++20).

| Policy | Entries | Generated source | Size vs slice154 table (2,361,045 bytes) | Compile | Object |
|---|---|---|---|---|---|
| slice154 table (recorded and derived) | 100 | 2,361,045 bytes | 1x | 14.1 s | 1.8 MB |
| (b) targeted, chosen | 426 | 10,226,016 bytes | 4.3x | 65.0 s | 8.5 MB |
| (a) every 8-byte address | 2,048 | 51,532,190 bytes | 21.8x | 400 s (6 min 40 s) | 44.6 MB |

Policy (a) exceeds the 3x source-size limit of the decision rule, so the targeted policy is used.
Its compile time was measured only to report it; it is under 10 minutes. The targeted entries are:
address 0; the address after each E-bit delay slot (pc+16); every branch target; the continuation
of JAL and JALR (pc+16); every IADDIU constant with base vi0 (the donor emitter's rule for JR and
JALR targets); the address after each branch or jump delay slot; and the 100 slice154 entries.
Python `targeted_entries` implements the rules, and tests cover each rule on an authored image. The
set contains all 100 slice154 entries and all 86 recorded entries.

Results (Confirmed unless marked; candidate, not owner-measured):

- Image: `vu1_image.bin` sha256 9e5bc860...796cdff; blob sha256 ae6b6cb3...ac4c1; 86 of 86 snapshots equal the blob over 0x0000-0x3FEF.
- Table: 426 programs, 426 emitted, 0 refused, 0 interpreter exit sites, `image_status_used` false, `mac_mode` observed in every program. Generated source sha256 d65474f8bec2da541fb45d63ed7149440531debdc6acfeca6b11c2d211f831ec (recorded in the launcher pin); manifest sha256 6c69f522730acaec8bf13cab7361f8329c94740d55b4e71db5be505646e42863.
- Coverage: 100 of 100 slice154 entries and 86 of 86 recorded entries are in the entry list. The 100 slice154 entries keep their key and identity digest (Python control).
- CTest 18/18 on the candidate build (46.5 s). Core emitter: 110 checks, 0 failures. Registry (`vu1_aot_registry`): 426 of 426 entries, 2,130 checks, 0 failures. `vu_recomp_corpus` over the 426-file corpus: 426 programs matched, 1,150 checks, 0 failures (181 compared to the end, 1,523 budget-capped and resumed).
- Python: 231 OK, 1 skipped (pre-existing), with `GT4_NATIVE_BUILD` on the slice155 build. New `tests/python/test_vu1_image.py` has 14 tests: synthetic blobs, plus private controls that skip when the files are absent.
- Candidate build: 103 steps, 3 min 6 s, `gt4vulkan.exe` sha256 3cf5f998...c641d00 (62,204,416 bytes). The build reports about 65,000 `unused label` warnings from the generated code (one label per instruction, as in slice154). They are warnings, not errors.
- Headless smoke, 4,200 frames, the slice153/154 schedule (12 presses), no window: 42 frame dumps identical to `private/slice154-smoke/final-headless/frames.sha256`. Counters at every compiled call: compiled_calls=20002, interpreted_calls=0, budget_resumes=0, identity_rejects=0.
- Headless verify, `GT4_VU1_VERIFY=1`, same schedule: 42 frame dumps identical; verified=4300, verify_mismatches=0, compiled_calls=20002, interpreted_calls=0. The slice154 verify run had the same verified count (4300) at 20,000 calls.

Limits:

1. Coverage is heuristic. The targeted set comes from image structure, not from the EE's MSCAL
   call sites or VIF streams. An entry outside it still runs the interpreter and is counted as
   `interpreted_calls`. The owner scenes (showroom, night race, rally) decide whether this is
   enough; they are not verified here.
2. The schedule only calls slice154 entries (the slice154 verify run logged interpreted_calls=0 on
   the same schedule), so the 326 new entries are not executed in the headless runs. Their
   execution is covered by the corpus differential (426 programs, 0 failures), not by a game run.
3. `verified` counts top-level calls only. Calls made while a verification runs (jump-register
   dispatch and chained hand-offs) run compiled inside both legs and are not compared with the
   interpreter one by one. This predates slice155 and is not changed here.
4. Owner frame measurement is not taken; no speed claim is made. The FlagPipe history limit of
   slice154 is unchanged.

Measurement outputs kept in private directories: `private/slice155-vu1-full-gen-all` (policy a,
51.5 MB, not used), `private/slice155-vu1-full-gen-targeted` (same content as the candidate table),
`private/slice155-smoke/*`.

Slice157 (TASK-008, all entries) replaces the owner's slice155 table policy with every 8-byte
address of the pinned image (2,048 programs). The owner tested slice155 with 354,837 interpreted
VU1 calls in the rally and 0 in the night race; the all-entries table is the response. Owner
command, from the repository root:

    private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode vu1-all

`--vu1-verify N` and `--vu1-interpret-only` apply as in vu1-full. Pins:
[slice157-vu1-all-runtime.json](inputs/slice157-vu1-all-runtime.json) (capsule, same overlay and
donor hashes as slice155) and [slice157-vu1-all.json](inputs/slice157-vu1-all.json) (launcher,
runtime `private/slice157-vu1-all-build/build/gt4vulkan.exe`, 95,125,504 bytes, sha256 40388a41...2369e).
Directories: table `private/slice157-vu1-all-gen` (vu1_generated.cpp plus 128 parts
`vu1_generated_000.cpp` to `vu1_generated_127.cpp`, 16 programs each), entries
`private/slice157-vu1-all-entries`, corpus `private/slice157-vu1-all-corpus` (2,048 files), capsule
`private/slice157-vu1-all-runtime`, build `private/slice157-vu1-all-build/build`. Image and shaders are
the slice155 and slice153 files, unchanged.

Steps. All outputs are generated offline in fresh private directories and are not committed.

1. Entry lists (policy label recorded in a sidecar, `<list>.json`):
   `scripts\vu1_image.py entries --image private\slice155-vu1-image\vu1_image.bin --policy all --out private\slice157-vu1-all-entries\entries-all.txt`
   (2,048 addresses, sha256 284330f8...). `--policy targeted --table-manifest private\slice154-vu1-flags-gen\manifest.json`
   reproduces the slice155 list (sha256 ec0b9116..., 426 addresses). The targeted policy is kept.
2. Generator (new options, default output unchanged):
   `gt4_vu_recomp --programs-per-file 16 --entry-policy all --image ... --entries private\slice157-vu1-all-entries\entries-all.txt private\slice157-vu1-all-gen`
   writes the table file, the 128 parts and a manifest with `entry_policy`, `programs_per_file` and
   `generated_parts` (file, sha256, bytes, programs). The table file declares every function and holds
   kPrograms; each part defines its 16 functions. CMake compiles every `vu1_generated_*.cpp` in the
   table directory (sorted), so Ninja builds them in parallel.
3. Reproduction control: the same emitter without the new options, on the targeted list, produces
   `vu1_generated.cpp` with sha256 d65474f8..., identical to the slice155 table.
4. Corpus: `scripts\vu1_image.py corpus --image ... --entries private\slice157-vu1-all-entries\entries-all.txt --out private\slice157-vu1-all-corpus`
   writes 2,048 KEY_ENTRY.bin files for the registry control.
5. Runtime capsule: `scripts\prepare_perf_runtime.py private\slice157-vu1-all-runtime --vu1-aot`
   (source hashes and overlay 171b07c3... identical to slice155).
6. Configure as slice155 with `GT4_PERF_RUNTIME_SOURCE=private/slice157-vu1-all-runtime` and
   `GT4_VU1_GENERATED_DIR=private/slice157-vu1-all-gen`. `GT4_VU_CORPUS_DIR` stays the 426-file
   slice155 corpus (the corpus differential builds its own table from it; see the limits).

Entry policy comparison (Confirmed, from the manifests):
`vu1_image.py compare --targeted <slice155 targeted manifest> --full <slice157 manifest>` reports
426 of 426 targeted rows equal to the full table in key, digest, flag-elision flags, MAC and clip
counts, reachable size, status and function name, 0 differences. The full table has 2,048 emitted,
0 refused, `image_flags_used` true in 2,048 programs, `image_status_used` false in 2,048 programs,
0 interpreter exit sites, `mac_mode` observed in all. Flag elision is per program (the emitter's
`image_flags_used` per record), and the counts show it applied to every program.

Sizes and time (Confirmed, local):

| Item | Value |
|---|---|
| Table source `vu1_generated.cpp` | 449,433 bytes |
| Part sources `vu1_generated_NNN.cpp` (128) | 51,268,101 bytes (51.7 MB together with the table) |
| Objects, game table (128 parts + table file) | 87.8 MB + 0.8 MB |
| Executable `gt4vulkan.exe` | 95,125,504 bytes (slice155: 62,204,416, +32.9 MB) |
| Emitter `gt4_vu_recomp` (new options) | rebuilt in `private/slice157-vu1-all-tools` |
| Table generation | 2.4 s (emitter) |
| Game table compile | about 3 min wall clock on 16 cores (parts from 06:54 to 06:57) |
| Full configure and build, 231 steps | about 6.5 min, 0 errors, about 2,400 unused-label warnings in the first part of the log |

Results (Confirmed unless marked; candidate, not owner-measured):

- CTest 18/18 on `private/slice157-vu1-all-build/build` (about 2 min). `vu1_aot_registry` against the
  2,048-file corpus: 2,048 of 2,048 table entries found, 10,240 checks, 0 failures. `vu_recomp_core`:
  128 checks (new split-emission control included), 0 failures. `vu_recomp_corpus` (426 programs,
  the slice155 corpus): 0 failures.
- Python: 236 OK, 1 skipped (pre-existing), with `GT4_NATIVE_BUILD` on the slice157 build. New tests:
  policy comparison (synthetic), a private control for the 2,048 table with 128 parts, and the
  VU1 image tests (19 in total in `test_vu1_image.py`).
- Headless smoke, 4,200 frames, the slice153/154 schedule with 12 presses (`600:start:30`, then
  cross, start, down every 300 frames to `3900:start:30`), no window: 42 frame dumps identical to
  `private/slice154-smoke/final-headless/frames.sha256` (per-file sha256 compared).
- Headless verify, `GT4_VU1_VERIFY=1`, same schedule: completed 4,200 frames, 42 frame dumps identical
  to the smoke run. The probe log has no VU1 counter lines in verify mode; see the journal for the
  counter pass.

Limits:

1. Coverage is now complete over the 2,048 aligned addresses, but the interpreter still runs for any
   entry that is not an 8-byte aligned address in the image, and for any
   program refused by the emitter (0 refused). Owner scenes (rally, night race, showroom) are not run
   here; the owner decides whether `interpreted_calls` is 0 in F8.
2. The corpus differential (`gt4_vu_corpus_differential`) runs over the 426-file slice155 corpus, not
   the 2,048 files. The 2,048 coverage uses the registry control (keys, digests and identity) and the
   static emitter; a standalone 2,048 corpus differential needs one more single-table build (not run).
3. The emitter output for the 2,048 table is about 51.7 MB of C++ in 129 translation units. The
   executable is 32.9 MB larger than slice155 (owner-accepted size).
4. Owner frame measurement is not taken. No speed claim is made. The FlagPipe history limit and the
   `verified` counter limits of slice155 are unchanged.

## Build pipeline and tuning (slice159, TASK-010)

Status: local evidence only. No pin, launcher mode or owner session was added (no playable candidate).
Timings were taken under heavy concurrent load from other agents on this machine: compare CPU-seconds
from the ninja log, not wall time, and treat every timing here as not comparable across runs.

Changes (one variable per build directory):

1. Serial recipes removed. The --parallel 1 option is gone from native/build_candidate.cmd,
   tools/ee_producer/build.cmd, tools/ee_producer/build_analysis.cmd and the two EE recipes in
   docs/EE-CANDIDATES.md and docs/EE-CALLBACK-COVERAGE.md. Ninja now uses all cores by default.
2. Corpus table compiled once. tools/vu_recomp/CMakeLists.txt builds the 426-file corpus table as the
   OBJECT library gt4_vu_corpus_table. gt4_vu1_mismatch_replay and gt4_vu_corpus_differential link its
   objects. Before, each compiled its own copy.
3. Flag benchmark on demand. gt4_vu1_flag_bench and its baseline table are EXCLUDE_FROM_ALL. Build them
   explicitly with: cmake --build <build-dir> --target gt4_vu1_flag_bench. No CTest target depends on it.
   Every CTest executable is still built by default (18 tests).
4. Unused-label suppression scoped. -Wno-unused-label is applied only to the generated VU1 sources
   (native/CMakeLists.txt, set_source_files_properties). Measured on one part (vu1_generated_000.cpp,
   slice157 capsule, /O2 /Ob2): 2,484 unused-label warnings without the suppression, 0 with it. Other
   parts were not measured; the earlier "about 65,000" figure was not verified.
5. Tuning options (CMakeLists.txt, default OFF, applied to every target of the tree):
   - -DGT4_FP_CONTRACT_OFF=ON adds /clang:-ffp-contract=off (control, T0).
   - -DGT4_X86_64_V3=ON adds /clang:-march=x86-64-v3 and implies the fp-contract flag (T1).
   The pinned EE game library ps2game.lib is not rebuilt, so its code keeps the default flags.
6. Compiler cache (S4, not installed). sccache or ccache needs owner approval. The hook would be
   -DCMAKE_CXX_COMPILER_LAUNCHER=sccache on the configure line. Not done.

Build commands (fresh directories; the worktree private directory is a junction to the main checkout so
the perf capsule and port paths pass the checks in native/CMakeLists.txt):

    cmake -S <worktree> -B <main>/private/slice159-build-<name>/build -G Ninja -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DGT4_ASSETS_ROOT=<main>
      -DGT4_EE_PROFILE=production -DGT4_PERF_RUNTIME_SOURCE=<worktree>/private/slice157-vu1-all-runtime
      -DGT4_PORT_DIR=<worktree>/private/slice153-pacing-port -DGT4_SHADER_DIR=<worktree>/private/slice153-pacing-shaders
      -DGT4_VU1_GENERATED_DIR=<main>/private/slice157-vu1-all-gen -DGT4_VU_CORPUS_DIR=<main>/private/slice155-vu1-full-corpus
      -DGT4_PYTHON=<main>/private/tooling-venv/Scripts/python.exe [-DGT4_FP_CONTRACT_OFF=ON | -DGT4_X86_64_V3=ON]
    cmake --build <main>/private/slice159-build-<name>/build

Measurements (CPU-seconds are the sum of job durations in .ninja_log; wall time is not comparable):

| Build | Jobs | Wall (load-affected) | CPU-s |
|---|---|---|---|
| slice157 (baseline, slice158 plan) | 240 | 401 s | 3,942 |
| slice159 S1-S3 (slice159-build-s123) | 230 | 189 s | 1,700 |
| slice159 T0 -ffp-contract=off | 230 | 231 s | 2,045 |
| slice159 T1 x86-64-v3 + T0 | 230 | 229 s | 2,216 |

The S1-S3 build removes the 204 s serial flag-bench tail and two of three corpus-table compiles (about
176 CPU-s). The T0 and T1 CPU-s are higher under the same load, which the flags alone do not explain: the
runs overlapped with other agents' compiles. Treat the CPU-s column as an upper bound until repeated on an
idle machine.

Correctness (Confirmed):

- T0 vs slice157: the runtime library differs as a file, but the disassembly of 159 of 160 runtime objects
  is identical. The exception is vu_micro.cpp (slice157 runtime capsule): instruction order and register
  choice differ, with no FMA emitted for the default target.
- Headless 4,200 frames, the slice153/154 schedule (12 presses), frame dumps every 100 frames: T0 42 of 42
  identical to private/slice157-smoke/headless-157/frames; T1 (v3) 42 of 42 identical.
- CTest 18/18 on S1-S3 (slice159-build-s123), T0 and T1 (slice159-build-v3).
- Python unittest discover: 236 OK, 1 skipped (pre-existing), with GT4_NATIVE_BUILD on the S1-S3 and T0 builds.

VU1 micro-benchmark, baseline vs v3 (gt4_vu1_flag_bench 50 2 private/slice155-vu1-full-corpus):

| Build | Elided ns/call (timed calls, best of 2 passes) | Compared / failures |
|---|---|---|
| default (slice159-build-s123) | 10,715 | 852 / 0 |
| x86-64-v3 (slice159-build-v3) | 11,630 | 852 / 0 |

Verdict: Unknown. The run shared the machine with other compiles. The v3 build is not faster in this run.
Rerun on an idle machine, 3 or more times each, and compare the elided figure:

    <main>/private/slice159-build-s123/build/gt4_vu1_flag_bench.exe 200 5 <main>/private/slice155-vu1-full-corpus
    <main>/private/slice159-build-v3/build/gt4_vu1_flag_bench.exe 200 5 <main>/private/slice155-vu1-full-corpus

Limits:

1. The T0 and T1 flags apply to the runtime, the generated VU1 code and the tests. The pinned EE library
   and the qualified donor objects are not rebuilt by these options.
2. Frame identity is shown for one 4,200-frame headless schedule, not for owner gameplay. No speed claim.
3. PGO, ThinLTO, the dispatch divide change and the sccache option are not implemented or measured.
4. No candidate pin or launcher mode was added: T0 and T1 are build variants only, not playable candidates.
