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
library. Keep candidate and production qualification distinct.

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

Slice156 (TASK-009 quick fixes) is a candidate on the slice155 profile. Owner command, from the
repository root:

    private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode quickfix

Changes: window title `GT4Recomp`; the presentation is fixed 16:9 (F6, the 4:3 option and
`GT4_ASPECT` / `--aspect` are removed); Alt+Enter and F11 toggle borderless fullscreen on the
window's monitor and restore the windowed frame (`native/window_mode.*`, fixture
`native/quickfix_fixture.cpp`, CTest `native_quickfix`); the F8 report prints `interval` counter
differences since the last F7 or report next to `session` totals (`Counters::since`, pacing
fixture); `GT4_FORCE_480P=1` (`--force-480p`, default off) rewrites the guest default NTSC request
(SetGsCrt interlace 1, mode 0x2) to 480p progressive (interlace 0, mode 0x50); other modes are
unchanged (`native/display_override.*`; hook in the donor `hw.cpp` `gs_set_crt`).

Pins: [slice156-quickfix-runtime.json](inputs/slice156-quickfix-runtime.json) (capsule receipt;
system, vu_micro and vif sources byte-identical to slice155, `hw.cpp` is the only new source) and
[slice156-quickfix.json](inputs/slice156-quickfix.json) (launcher). Build directory
`private/slice156-quickfix-build/build`; capsule `private/slice156-quickfix-runtime`. The VU1
table `private/slice155-vu1-full-gen`, the adapter and shaders `private/slice153-pacing-port` and
`private/slice153-pacing-shaders` are reused unchanged.

Steps:

1. `scripts\prepare_perf_runtime.py private\slice156-quickfix-runtime --vu1-aot --gs-crt-override`
2. Configure with `GT4_PERF_RUNTIME_SOURCE=private/slice156-quickfix-runtime`,
   `GT4_VU1_GENERATED_DIR=private/slice155-vu1-full-gen`, `GT4_PORT_DIR` and `GT4_SHADER_DIR` set to
   the slice153 directories (the adapter check requires them), then build with `--parallel 1`.

Controls (candidate, not owner-measured): CTest 19/19 on the slice156 build, including
`native_quickfix` and `python_native_contracts` (GT4_NATIVE_BUILD on this build). Headless smoke,
4,200 frames, the slice153-156 schedule (12 presses, `--press 600:start:30` then cross/start/down
every 300 frames to 3900, card seed `private/slice113-video/widescreen-seed/card.ps2`):
GT4_FORCE_480P unset gives 42 frame dumps identical to `private/slice155-smoke/headless-155/frames.sha256`.
With `GT4_FORCE_480P=1` the frames are also identical (0 of 42 differ), and `probe.log` has one
line `[display] GT4_FORCE_480P: SetGsCrt interlace 1 mode 0x2 rewritten to interlace 0 mode 0x50`
followed by `[gs] SetGsCrt interlace 0 mode 0x50 field 1`. The headless path does not present to a
window, so these frames say nothing about the displayed image; no correctness claim is made.

Limits:

1. The pinned slice153 adapter still exports `set_widescreen(false)` (4:3 presentation). The
   host never calls it; removing it needs a new adapter pin.
2. `GT4_FORCE_480P` is validated at window start (`validate_environment`) and per SetGsCrt request;
   the headless path does not print the startup line.
3. The 480p rewrite changes only the CRT request. Whether the GS output and the menus follow it is
   the owner's check.
4. Fullscreen uses a WS_POPUP window over the monitor, not exclusive mode; the swapchain follows
   through the existing WM_SIZE path. Owner visual check pending.
5. Alt+Enter is consumed by the host and Enter is not read as Start while Alt is held.
6. Owner gameplay and frame timing are not measured.


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


Slice158 (TASK-009 quick fixes, second candidate) is built on the slice157 all-entries VU1 capsule with the
slice156 quick fixes. Owner command, from the repository root (the slice156 `quickfix` mode and its pins are
kept as history):

    private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode quickfix2

`--force-480p` (GT4_FORCE_480P=1, default off) is accepted by `quickfix2`. The name `quickfix2` is new so that
`quickfix` still reproduces the slice156 binary the owner tested; the two candidates keep separate pins.

Changes:

- 480p through the guest CRT mode table, not SetGsCrt. The table D_00618648 (ELF .data at 0x618648) holds 8
  rows of {id, interlace, mode, field}. `func_001060A0(interlace, mode, field)` returns the id of the first
  matching row (0 when none), and `func_00106058(id)` returns the matching row, or row 8 (the fallback) when
  none matches. When GT4_FORCE_480P=1 the boot hook (donor `system.cpp` `boot()`, after `load_elf`, before the
  main thread starts) checks the pinned bytes and rewrites row id 1 and the fallback row to {0, 0x50, 1}.
  Any other bytes refuse the rewrite with exit status 2. Code: `native/display_table.*`, fixture
  `native/display_table_fixture.cpp` (CTest `native_display_table`), prepare option `--display-table-override`.
- The slice156 `hw.cpp` SetGsCrt rewrite is not part of this capsule (`--gs-crt-override` still derives slice156).
- `late_frames` counts a release only when it is more than 1 ms (frequency / 1000 ticks) after its deadline
  (`native/pacing.*`, fixture `pacing_fixture`). Lateness within the tolerance is not counted.

Pins: [slice158-quickfix2-runtime.json](inputs/slice158-quickfix2-runtime.json) (capsule; `vu_micro.cpp` and
`vif.cpp` byte-identical to slice157, `system.cpp` adds the boot hook) and
[slice158-quickfix2.json](inputs/slice158-quickfix2.json) (launcher; runtime
`private/slice158-quickfix2-build/build/gt4vulkan.exe`, 95,128,576 bytes, sha256 0f498bfc...b6e3b9).
Directories: capsule `private/slice158-quickfix2-runtime`, build `private/slice158-quickfix2-build/build`, table
`private/slice157-vu1-all-gen` (reused), smoke `private/slice158-smoke`.

Steps:

1. `private\tooling-venv\Scripts\python.exe scripts\prepare_perf_runtime.py private\slice158-quickfix2-runtime --vu1-aot --display-table-override`
2. Configure as slice157 with `GT4_PERF_RUNTIME_SOURCE=private/slice158-quickfix2-runtime` and
   `GT4_VU1_GENERATED_DIR=private/slice157-vu1-all-gen`, `GT4_PORT_DIR` and `GT4_SHADER_DIR` set to the slice153
   directories, then build.

Results (candidate, not owner-measured):

- CTest 20/20 on the slice158 build (includes `native_display_table` and `native_pacing`).
- Python: 251 OK, 1 skipped (pre-existing), with `GT4_NATIVE_BUILD` on the slice158 build. New tests in
  `tests/python/test_display_table.py`: the pinned ELF rows (skipped when the ELF is absent), the lookup model,
  the boot hook derivation and reversal, and the slice158 pins.
- Headless smoke, 4,200 frames, the slice157 press schedule, no window. `GT4_FORCE_480P` unset: 42 frame dumps
  identical to the slice157 build run with the same script. `GT4_FORCE_480P=1`: the log has
  `[display] GT4_FORCE_480P=1: CRT table rows id 1 and fallback set to interlace 0 mode 0x50 field 1`, the
  frames are identical to the unset run (0 of 42 differ), and the only SetGsCrt line is still
  `[gs] SetGsCrt interlace 1 mode 0x2 field 1`.

Finding, which blocks the 480p claim: the rewritten table does not change the request that reaches the GS.
The boot issues one SetGsCrt (480i) through syscall 0x02 (`kernel.cpp`, stub 0x5ad8a0), called by the GS display
wrappers 0x57fd08 and 0x57fe00, which 0x4a03c0 reaches through a function table (no direct `jal` found). The
table lookups (`func_001060A0` at 0x107640, `func_00106058` at 0x10754C) are a separate path that the headless
boot did not show to change that request. The slice156 SetGsCrt rewrite changed only the syscall argument, and
the owner did not see 480p.

Limits:

1. GT4_FORCE_480P=1 is not a verified 480p switch. Do not test it as 480p until the request path is traced.
2. The fallback row is also rewritten, so unlisted CRT requests resolve to 480p while the switch is on.
3. `--mode quickfix` (slice156) still runs the SetGsCrt rewrite and keeps its owner result.
4. A `--load-state` save restores RAM after the boot hook, so a state saved without the hook keeps its table.
5. Owner gameplay, frame timing and the window (half-height) path are not checked here.

Slice164 (TASK-009, race display index) replaces the slice158 CRT table rewrite, which did not change the
request that reaches the GS. The owner decision: races always 480p; menus keep the original interlaced
behaviour. Owner command, from the repository root (`quickfix`, `quickfix2` and their pins are kept as history):

    private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode quickfix3 --force-480p

Confirmed (from the pinned `private/reconstructed/SCUS_973.28.elf`, text offset = vaddr - 0xFF000; words
decoded by a scratch MIPS reader, checked in `tests/python/test_race_display.py`):

| Guest word | Meaning | Evidence |
|---|---|---|
| 0x622F4C | owner pointer | `func_001103E0`: `lui v0,0x62; lw a0,0x2F4C(v0)`; `func_0043A490` stores it |
| owner + 0x38CB0 | race option object | `func_001103E0`: `lui at,0x3; ori at,0x8CB0; addu a0,at,a0`, passed to `func_00436740` |
| object + 0x16B0 | race display index (0 normal, 1 480p, 2 1080i) | `func_00436640` 0x43665C `lw v1,0x16B0(s1)`; `func_004363A0` 0x436598 `sw zero,0x16B0(s0)` |
| 0x622DF0 | names table: "normal", "480p", "1080i" | `func_00437380` and `func_004373F0` read it by index |

`func_00436640` (race start) selects the setting by the index: index 1 calls `func_001074D0(&D_006184D0,2,6,640,448)`
(row id 6, the 480p row), index 0 the default row. The index is zero after `setDefault` (0x436598).

Design:

- `native/race_display.*`: pure helpers `race_display_index_address` (owner pointer to index address, 64-bit sum,
  refused outside guest RAM; null owner refused), `should_force_480p_index` (only 0 changes), and
  `force_race_480p_at_vblank` (one guest VBlank: writes 1 at the index when it is 0, counts writes, logs the first).
- Host hook: `native/window.cpp` `on_vblank`, after `pump_messages`, when `GT4_FORCE_480P=1`. No donor source is
  changed: the capsule `private/slice164-quickfix3-runtime` is the slice157 derivation (system.cpp, vu_micro.cpp and
  vif.cpp byte-identical to slice157). The F8 report prints `race display index writes N` when forced.
- Removed: `native/display_override.*` (SetGsCrt rewrite), `native/display_table.*` and
  `display_table_fixture.cpp` (CRT table), the `--gs-crt-override` and `--display-table-override` options of
  `prepare_perf_runtime.py` with their tests, and the CTest `native_display_table`. A build of the slice156 or
  slice158 capsule no longer compiles from this tree; their pins and executables remain as history.
- Fixture: `native/race_display_fixture.cpp` (CTest `native_race_display`): environment parsing, address chain
  (including refused edge owners), write decision, one write then none at 480p, 1080i kept, invalid owner ignored.
- Launcher: `--mode quickfix3` (`docs/inputs/slice164-quickfix3.json`, runtime
  `private/slice164-quickfix3-build/build/gt4vulkan.exe`); `--force-480p` is accepted by `quickfix3`.
- Pins: [slice164-quickfix3-runtime.json](inputs/slice164-quickfix3-runtime.json) (capsule receipt and sources).

Steps:

1. `private\tooling-venv\Scripts\python.exe scripts\prepare_perf_runtime.py private\slice164-quickfix3-runtime --vu1-aot`
   (done in this slice; the derivation is the slice157 one).
2. Configure as slice157 with `GT4_PERF_RUNTIME_SOURCE=private/slice164-quickfix3-runtime`,
   `GT4_VU1_GENERATED_DIR=private/slice157-vu1-all-gen`, `GT4_PORT_DIR` and `GT4_SHADER_DIR` set to the slice153
   directories, then build. The launcher pin is filled after the link (sha256 and size).

Status (candidate, not owner-measured). Built at `private/slice164-quickfix3-build/build` (gt4vulkan.exe 95,126,016 bytes,
sha256 78b92c63...af014e). CTest 20/20 including `native_race_display`. Python discover with `GT4_NATIVE_BUILD` on this build:
248 run, OK, 1 skipped (pre-existing). Launcher pin `slice164-quickfix3.json` written.

Headless smoke, 4,200 frames, the slice158 12-press schedule, card `private/slice113-video/widescreen-seed/card.ps2`:
- Unset: 42 frame dumps identical to `private/slice158-smoke/headless-158` (0 differ).
- `--force-480p`: 42 dumps identical to the unset run (0 differ) and to the slice158 forced run (0 differ).
- The forced log has no `[display]` line and no `[window]` or `[pacing]` line. The only SetGsCrt is
  `interlace 1 mode 0x2 field 1`, so no race-start request with interlace 0 / mode 0x50 was seen.

**The headless path never calls `open_window`, so the VBlank hook never runs.** The headless smoke therefore
does not test the race display write. Only a windowed run (`probe --window`, which presents) exercises it.
That run has not been done.

Limits:

1. GT4_FORCE_480P does not change the GS, the table or any SetGsCrt request. It writes the game's race option index.
   Whether the race output is 480p on the owner's display is the owner's check; the headless path does not present.
2. The write also happens while the index is 0 outside races (menus, options). A user who chooses "normal" in the
   options menu is moved back to 480p at the next VBlank; the menu may therefore show 480p while the race uses it.
3. The index is forced only when it reads 0; a user choosing 1080i keeps it. The check runs at every VBlank, so a
   reset to 0 by `setDefault` is undone within one frame.
4. The race path (func_00436640) is confirmed from the ELF only by the words above, not by a traced race start.
   The headless schedule may not reach a race; the smoke says so when it does not.
5. No performance claim is made. Owner gameplay is not measured here.
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

## Slice 160 (TASK-013): VU0 microcode AOT

The 14 `VCALLMS` entries of the pinned VU0 image (docs/VU0-MICROCODE-MAP.md) run compiled. The
VU0 emitter profile is `gt4_vu_recomp --unit 0`; the VU1 profile is unchanged (its table
`vu1_generated.cpp` and manifest hashes equal slice157: 3eed39d6... and 07ae2077...). Pins:
[slice160-vu0-image.json](inputs/slice160-vu0-image.json) (static image),
[slice160-vu0-runtime.json](inputs/slice160-vu0-runtime.json) (runtime capsule, VU1 and VU0 overlays)
and [slice160-vu0-aot.json](inputs/slice160-vu0-aot.json) (launcher).

Steps, from the repository root. Outputs go to fresh private directories:

1. `vu0_image.py build --elf private\reconstructed\SCUS_973.28.elf --out private\slice160-vu0-image`
   reads the 2,688-byte upload at file offset 0x5D4280 (vaddr 0x6D3280) and pads it with zeros to
   4096 bytes. Upload sha256 6ee4bdde...; image sha256 2d7b2653.... `vu0_image.py entries` writes the
   14 addresses (sha256 2f5ac510...). The list includes 0x308 and 0x478, which the slice-158 plan
   counted as fall-through suffixes; both are kept so every listed VCALLMS target runs compiled.
2. Emitter: `gt4_vu_recomp --unit 0 --namespace gt4::vugen::vu0 --entry-policy vcallms --image
   private\slice160-vu0-image\vu0_image.bin --entries private\slice160-vu0-image\entries-vcallms.txt
   private\slice160-vu0-gen-r3`. Output `vu0_generated.cpp` (14 programs, 0 refused) and manifest.json
   (`"unit": 0`, `identity_tag` GT4-VU0-PROGRAM-IDENTITY-1).
3. Runtime capsule: `prepare_perf_runtime.py private\slice160-vu0-runtime --vu0-aot` (implies
   `--vu1-aot`). The VU1 overlay hash stays 171b07c3...; VU0 overlay 9e5fee2b....
4. Build: configure with `GT4_PERF_RUNTIME_SOURCE` = the capsule, `GT4_VU1_GENERATED_DIR`
   = `private\slice157-vu1-all-gen` and `GT4_VU0_GENERATED_DIR` = the VU0 directory. Build directory
   `private\slice160-vu0-build\build`. The executable has `GT4_VU0_AOT` defined; the VU0 counters
   are in the F8 report.
5. Controls (CTest 21/21): `vu0_emitter` (20 checks), `vu0_aot_registry` (14 of 14 entries; 87 checks),
   `vu0_aot_differential` (below), plus the existing VU1 suites.

Owner command (from the repository root): `python scripts\play_performance.py --assets-root . --mode vu0-aot`.
Verify: add `--vu0-verify 50` (samples every 50th VU0 call against the exact interpreter path).
`--vu1-verify` and `--vu1-interpret-only` also apply in this mode.

Numbers (local controls only; no owner session):

- Differential, `gt4_vu0_differential_tests`: 14 entries, 1000 finite and 1000 raw-bit seeds each
  (28,000 comparisons over every Vu0Regs field and the 4 KiB data memory), 896 budget-resume runs with
  budgets 1 to 24, 0 failures. Negative control: `interpret(0, pc)` (run_fast) differs from `run(0, pc)`
  in MAC or status in 768 states, which is why hand-offs use the exact path.
- Registry, `gt4_vu0_aot_registry_test`: 14 of 14 entries found, changed reachable word not registered,
  padding change keeps the hit, `code_changed(0)` invalidates, VF0 not normalized takes the interpreter.
- Headless smoke, 4200 frames, the slice157 schedule (12 presses), no window: 42 frame dumps identical
  to `private/slice157-smoke/headless-157/frames`. `[perf] vu0_aot compiled_calls=1256000
  interpreted_calls=0 budget_resumes=0 handoffs=0 identity_rejects=0`.
- Headless verify, `GT4_VU0_VERIFY=1`, same schedule: 42 frame dumps identical; `verified=1255999
  verify_mismatches=0`.
- Python: unittest discover with `GT4_NATIVE_BUILD` on this build: 246 OK, 1 skipped (pre-existing).
- Executable `gt4vulkan.exe` 95,529,984 bytes, sha256 a0206c16... (after the merge of task/010-build-pipeline).

Limits:

1. The VCALLMS list is the 14 entries of the VU0 map. The EE call-site scan was not run (the donor has
   no VCALLMS decoder), so an EE call to another address is counted as `interpreted_calls`.
2. Only the headless schedule was run. Owner scenes are not verified.
3. Hand-offs inside generated code (a JR to a return site outside the table's static set, or a budget
   resume) run `run(0, pc)` and are counted as `handoffs`, not `interpreted_calls`.
4. VU0 uses the donor exact `fp::` arithmetic (the `run(0)` path). VU1 keeps its `run_fast` SIMD path.
   The VU1 SIMD `vitof`/`vftoi` helpers are not bit-identical to the donor per-lane conversion. VU1
   output is unchanged (byte-identical control), so this is an open finding for VU1, not investigated.
5. `private/` in the worktree is a directory junction to the main checkout (see the report).

## Slice 168 - audio test silenced (TASK-015 part B)

`native/host_io_fixture.cpp` now submits zeroed PCM in both the ramp and the paced sawtooth buffers. The assertions
are unchanged. Build only the fixture, then run its CTest:

```
cmake -S . -B private\slice168-audio-build\build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DGT4_EE_PROFILE=production ^
  -DGT4_ASSETS_ROOT=C:/Antigravity/gt4-staticrecomp
cmake --build private\slice168-audio-build\build --target host_io_fixture
ctest --test-dir private\slice168-audio-build\build -R native_audio_input --output-on-failure
```

## Slice 169 - MTVU measurement (TASK-011 step 0)

Counters for the VU1 worker plan ([MTVU-PLAN](MTVU-PLAN.md) section 9). No guest-visible change.
Built from the `task/011-mtvu` worktree with every private input in the main checkout (no junction):
`native/CMakeLists.txt` and `tools/vu_recomp/CMakeLists.txt` now qualify capsules, port, shaders, donor and
VU0 image against `GT4_ASSETS_ROOT` (equal to the source root in the main checkout, so main builds are unchanged).

```
python scripts\prepare_perf_runtime.py C:/Antigravity/gt4-staticrecomp/private/slice169-mtvu-measure-runtime ^
  --assets-root C:/Antigravity/gt4-staticrecomp --mtvu-measure
cmake -S <worktree> -B C:/Antigravity/gt4-staticrecomp/private/slice169-mtvu-measure-build/build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" ^
  -DGT4_EE_PROFILE=production -DGT4_ASSETS_ROOT=C:/Antigravity/gt4-staticrecomp ^
  -DGT4_PYTHON=C:/Antigravity/gt4-staticrecomp/private/tooling-venv/Scripts/python.exe ^
  -DGT4_PERF_RUNTIME_SOURCE=C:/Antigravity/gt4-staticrecomp/private/slice169-mtvu-measure-runtime ^
  -DGT4_PORT_DIR=C:/Antigravity/gt4-staticrecomp/private/slice153-pacing-port ^
  -DGT4_SHADER_DIR=C:/Antigravity/gt4-staticrecomp/private/slice153-pacing-shaders ^
  -DGT4_VU1_GENERATED_DIR=C:/Antigravity/gt4-staticrecomp/private/slice157-vu1-all-gen ^
  -DGT4_VU0_GENERATED_DIR=C:/Antigravity/gt4-staticrecomp/private/slice160-vu0-gen-r3 ^
  -DGT4_VU0_IMAGE_FILE=C:/Antigravity/gt4-staticrecomp/private/slice160-vu0-image/vu0_image.bin ^
  -DGT4_VU_CORPUS_DIR=C:/Antigravity/gt4-staticrecomp/private/slice155-vu1-full-corpus
cmake --build C:/Antigravity/gt4-staticrecomp/private/slice169-mtvu-measure-build/build
```

The capsule adds `gif.cpp` (donor `gs/gif.cpp`, sha256 eee761d7...) and `memory.cpp` (c33b9fed...). Without
`--mtvu-measure` the derivation reproduces the slice160 capsule byte for byte (Python control). Receipt
sha256 afeeea67...; executable `gt4vulkan.exe` 95,539,712 bytes, sha256
72fe2ad32ce76477d181e6bf8883644c84fc16c696563fce0a261eafebfc39e2. The compiler on PATH reported
clang-cl 22.1.3 at configure time (slice166 records 23.1.2; same executable path).

Launcher: `--mode mtvu-measure` (pin `docs/inputs/slice169-mtvu-measure.json`), same options as `main`.
`GT4_MTVU_MEASURE` (0 or 1, default on) and `GT4_MTVU_MEASURE_EVERY` are validated before any output,
forwarded and recorded in `result.json`. A worktree without `private/` takes the pinned binaries from
`--assets-root` (`binary_workspace_for`); `external_runtime_probe.py` gained `--assets-root` for headless runs.

Controls:

- CTest 24/24, including the new `native_mtvu_measure` (GIF scanner with split packets, REGLIST/IMAGE
  skipping and NREG 0; overlap model with authored timestamps; interval arithmetic; report lines; switch values).
- Python unittest discover (GT4_NATIVE_BUILD = this build, GT4_ASSETS_ROOT = main checkout): 264 OK, 12 skipped
  (tests that need `private/` in the source checkout).
- Headless 4,200 frames, slice157 schedule (`external_runtime_probe.py --assets-root <main> --runtime-manifest
  docs/inputs/slice169-mtvu-measure.json --card-seed <main>/private/slice113-video/widescreen-seed/card.ps2`
  plus the 12 presses): 42 dumps identical to a fresh slice166 run and to
  `private/slice157-smoke/headless-157/frames`, with the counters on and with `GT4_MTVU_MEASURE=0`.
  Outputs: `private/slice169-smoke/headless-{on,off,166}`.

Owner command (from the worktree or, after merge, the main checkout):
`python scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode mtvu-measure --force-480p`

Limits: downloads are counted as TRXDIR=1 writes, not `gs::read_image` calls; EE counts cover the slow
memory path, which is the only EE path to VU memory in the donor; host timings are only meaningful on an
idle machine.

## Slice170 - MTVU step 2 (TASK-011): VU1 on a worker thread

Branch `task/011-mtvu`, worktree `C:\Antigravity\gt4-wt-011` (no `private/` junction). Capsule
`private/slice170-mtvu-runtime`, derived from the main checkout:

```
python scripts\prepare_perf_runtime.py C:/Antigravity/gt4-staticrecomp/private/slice170-mtvu-runtime ^
  --assets-root C:/Antigravity/gt4-staticrecomp --mtvu
```

`--mtvu` implies `--vu0-aot`; its hooks are `prepare_perf_runtime.py` `MTVU_PATCHES`. Without `--mtvu` the
derivation reproduces slice160 (`system.cpp`, `vif.cpp`, `vu_micro.cpp`, `runtime.json`) and slice169 byte for byte.
Pin: `docs/inputs/slice170-mtvu-runtime.json` (receipt sha256 4e5f1e4dc2c1287dd73572e083794475dbfd2f6726badfbdffb8303ce4645e20).

Build (clang-cl 22.1.3, Ninja, Release; same pinned inputs as slice169, new runtime capsule):

```
cmake -S C:\Antigravity\gt4-wt-011 -B C:\Antigravity\gt4-staticrecomp\private\slice170-mtvu-build\build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" ^
  -DGT4_EE_PROFILE=production -DGT4_ASSETS_ROOT=C:/Antigravity/gt4-staticrecomp ^
  -DGT4_PYTHON=C:/Antigravity/gt4-staticrecomp/private/tooling-venv/Scripts/python.exe ^
  -DGT4_PERF_RUNTIME_SOURCE=C:/Antigravity/gt4-staticrecomp/private/slice170-mtvu-runtime ^
  -DGT4_PORT_DIR=C:/Antigravity/gt4-staticrecomp/private/slice153-pacing-port ^
  -DGT4_SHADER_DIR=C:/Antigravity/gt4-staticrecomp/private/slice153-pacing-shaders ^
  -DGT4_VU1_GENERATED_DIR=C:/Antigravity/gt4-staticrecomp/private/slice157-vu1-all-gen ^
  -DGT4_VU0_GENERATED_DIR=C:/Antigravity/gt4-staticrecomp/private/slice160-vu0-gen-r3 ^
  -DGT4_VU0_IMAGE_FILE=C:/Antigravity/gt4-staticrecomp/private/slice160-vu0-image/vu0_image.bin ^
  -DGT4_VU_CORPUS_DIR=C:/Antigravity/gt4-staticrecomp/private/slice155-vu1-full-corpus
cmake --build C:\Antigravity\gt4-staticrecomp\private\slice170-mtvu-build\build
```

Executable `gt4vulkan.exe` 95,561,728 bytes, sha256 a0b4968b2c59071bb06945b263e195a939ad2bb2c53513590a05244536cd333e.
Launcher pin: `docs/inputs/slice170-mtvu.json` (mode `mtvu`). Step 1 fixtures (`native_mtvu_ring`,
`native_mtvu_journal`) are in the same build.

Controls:

- CTest 26/26 on `private/slice170-mtvu-build/build` (includes `native_mtvu_ring`, `native_mtvu_journal`).
- Headless 4,200 frames, slice157 press schedule (12 presses), frame dumps every 100 frames, compared with
  `private/slice157-smoke/headless-157/frames`: 42 of 42 identical for `GT4_MTVU=1` (three runs: `on`, `on2`,
  `on3`), and 42 of 42 identical for `GT4_MTVU=0` (`off`). Outputs `private/slice170-smoke/{on,on2,on3,off}`.
- `GT4_MTVU_VERIFY=1` headless, on and off (`verify-on`, `verify-off`): 4,200 `[mtvu-verify]` lines each, 0 VIF1
  shadow/worker mismatches, and the per-frame GS render hash identical in all 4,200 frames (4,196 frames carry GS
  renders). Frame dumps of both runs identical to the reference (42 of 42).
- Python: 264 OK, 12 skipped (`GT4_NATIVE_BUILD` on this build, `GT4_ASSETS_ROOT` main checkout).

Owner A/B (showroom, night race, rally; one F7/F8 interval each; the `mtvu` mode is the same for both):

```
set GT4_MTVU=0
python scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode mtvu --force-480p
set GT4_MTVU=1
python scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode mtvu --force-480p
```

`GT4_MTVU` unset means 1. Add `GT4_MTVU_VERIFY=1` for a stream check (the per-frame `[mtvu-verify]` lines; MTVU
on and off must print the same `gs` hashes). Known limit: F8 is not per-thread yet (MTVU-PLAN section 11.5), so
the owner's F8 under MTVU is flagged invalid by the probe until step 3.

### Slice171 - MTVU step 3 (per-thread F8)

Same runtime capsule as slice170 (`private/slice170-mtvu-runtime`, unchanged). Build `private/slice171-mtvu-build`
(same CMake recipe as slice170, with `-DGT4_PERF_RUNTIME_SOURCE=.../slice170-mtvu-runtime`). `gt4vulkan.exe`
95,569,920 bytes, sha256 6e60fe72bc870c25d6252cb79b276fbbd870b857dc10343855854cb0ee37269d. Launcher pin
`docs/inputs/slice171-mtvu.json`; mode `mtvu` now points to it.

Owner A/B (PowerShell, from the worktree or the main checkout; `GT4_MTVU` unset means 1):

```
$env:GT4_MTVU="0"; C:\Antigravity\gt4-staticrecomp\private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode mtvu --force-480p
$env:GT4_MTVU="1"; C:\Antigravity\gt4-staticrecomp\private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode mtvu --force-480p
```

Compare, per scene (showroom, night race, rally, Nordschleife TT 480p), the F8 lines `frame ms` (median, p95,
p99, max), `VBlanks/s`, the `exclusive wall ms/frame` line (`vu1_aot`, `vu1_interpret`, `ee_dispatch`, `gs_execute`),
and with GT4_MTVU=1 the `[mtvu]` lines: `EE wait ms/frame` (by reason; `mtvu_wait_vblank` should be small),
`EE gs_drain`, `worker ms/frame` (vu1_program, output_path12, busy, idle), and `overlap achieved`.
Acceptance (MTVU-PLAN step 5): median and p99 equal or better in each scene; `vu1_aot` on the EE thread near 0 with
MTVU on (the worker's time appears in the `[mtvu]` worker line). F8 under MTVU is valid (no INVALID line).
Headless identity check: `GT4_MTVU_REPORT_EVERY=500 external_runtime_probe.py ... --runtime-manifest docs\inputs\slice171-mtvu.json`
(42 frames compared with slice157).

### Slice172 - MTVU step 4 (EE-side signals) and step 6 (`--cpu-cores`) (TASK-011)

Runtime capsule `private/slice172-mtvu-runtime`, derived from the main checkout (the same derivation as slice170 with the
step 4 `gif.cpp` hook; `runtime.json` profile `step4-runtime-v2`):

```
python scripts\prepare_perf_runtime.py C:/Antigravity/gt4-staticrecomp/private/slice172-mtvu-runtime --assets-root C:/Antigravity/gt4-staticrecomp --mtvu
```

Pin: `docs/inputs/slice172-mtvu-runtime.json` (receipt sha256 ca565bbcd75214074fc333aed3cafda6b580e1b078ea221ad4519b8028fc0d29).
Configure and build (fresh dir; the worktree is the source, the build dir is in the main checkout's `private/`):

```
cmake -S C:\Antigravity\gt4-wt-011 -B C:\Antigravity\gt4-staticrecomp\private\slice172-mtvu-build\build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DGT4_EE_PROFILE=production ^
  -DGT4_ASSETS_ROOT=C:/Antigravity/gt4-staticrecomp -DGT4_PYTHON=C:/Antigravity/gt4-staticrecomp/private/tooling-venv/Scripts/python.exe ^
  -DGT4_PERF_RUNTIME_SOURCE=C:/Antigravity/gt4-staticrecomp/private/slice172-mtvu-runtime ^
  -DGT4_PORT_DIR=C:/Antigravity/gt4-staticrecomp/private/slice153-pacing-port -DGT4_SHADER_DIR=C:/Antigravity/gt4-staticrecomp/private/slice153-pacing-shaders ^
  -DGT4_VU_CORPUS_DIR=C:/Antigravity/gt4-staticrecomp/private/slice155-vu1-full-corpus ^
  -DGT4_VU1_GENERATED_DIR=C:/Antigravity/gt4-staticrecomp/private/slice157-vu1-all-gen ^
  -DGT4_VU0_GENERATED_DIR=C:/Antigravity/gt4-staticrecomp/private/slice160-vu0-gen-r3 ^
  -DGT4_VU0_IMAGE_FILE=C:/Antigravity/gt4-staticrecomp/private/slice160-vu0-image/vu0_image.bin
cmake --build C:\Antigravity\gt4-staticrecomp\private\slice172-mtvu-build\build
```

Result: `gt4vulkan.exe` 95,570,944 bytes, sha256 47df308fbbb3f555ea54749dae94ee249023e93cc315a721491d7a6661c50b0c.
Launcher pin `docs/inputs/slice172-mtvu.json`; launcher mode `mtvu` now uses it (`mtvu-slice171` keeps slice171).
Tests: `ctest --test-dir C:\Antigravity\gt4-staticrecomp\private\slice172-mtvu-build\build --output-on-failure` (27 tests);
Python: `$env:GT4_NATIVE_BUILD="C:\Antigravity\gt4-staticrecomp\private\slice172-mtvu-build\build"` then
`private\tooling-venv\Scripts\python.exe -m unittest discover -s tests/python`.

Weak-CPU simulation: `play_performance.py --cpu-cores N` (1 to the logical CPU count) limits the game process with
`SetProcessAffinityMask` after start; distinct physical cores first. Refused outside performance runs. Recorded as
`cpu_affinity` in `result.json`. See MTVU-PLAN section 12.5 and 12.6 for the owner A/B.

### Slice178 - release 1.0.0 W1: MTVU in `--mode main` (TASK-011)

`--mode main` is the slice172 MTVU runtime with the slice166 main features, pinned as `docs/inputs/slice178-main.json`. The
runtime capsule is `private/slice178-main-runtime`, derived from the main checkout with the same flags as slice172 (receipt
ca565bbc... identical, pin `docs/inputs/slice178-main-runtime.json`):

```
python scripts\prepare_perf_runtime.py C:/Antigravity/gt4-staticrecomp/private/slice178-main-runtime ^
  --assets-root C:/Antigravity/gt4-staticrecomp --mtvu
```

Configure and build from the worktree source (fresh build directory in the main checkout's `private/`; no junctions; shader and
port paths are main-checkout absolute):

```
cmake -S C:\Antigravity\gt4-wt-011 -B C:\Antigravity\gt4-staticrecomp\private\slice178-main-build\build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DGT4_EE_PROFILE=production ^
  -DGT4_ASSETS_ROOT=C:/Antigravity/gt4-staticrecomp -DGT4_PYTHON=C:/Antigravity/gt4-staticrecomp/private/tooling-venv/Scripts/python.exe ^
  -DGT4_PERF_RUNTIME_SOURCE=C:/Antigravity/gt4-staticrecomp/private/slice178-main-runtime ^
  -DGT4_PORT_DIR=C:/Antigravity/gt4-staticrecomp/private/slice153-pacing-port -DGT4_SHADER_DIR=C:/Antigravity/gt4-staticrecomp/private/slice153-pacing-shaders ^
  -DGT4_VU_CORPUS_DIR=C:/Antigravity/gt4-staticrecomp/private/slice155-vu1-full-corpus ^
  -DGT4_VU1_GENERATED_DIR=C:/Antigravity/gt4-staticrecomp/private/slice157-vu1-all-gen ^
  -DGT4_VU0_GENERATED_DIR=C:/Antigravity/gt4-staticrecomp/private/slice160-vu0-gen-r3 ^
  -DGT4_VU0_IMAGE_FILE=C:/Antigravity/gt4-staticrecomp/private/slice160-vu0-image/vu0_image.bin
cmake --build C:\Antigravity\gt4-staticrecomp\private\slice178-main-build\build
```

Result: `gt4vulkan.exe` 95,570,944 bytes, sha256 6d71f531e4dc84338cb4163b1aba643b077933f68005cb101391e811e7db1094. CMake
accepts the capsule name in `native/CMakeLists.txt` (slice178 added to each allowlist).

Checks: `ctest --test-dir C:\Antigravity\gt4-staticrecomp\private\slice178-main-build\build` 27/27. Python: with
`GT4_NATIVE_BUILD` on that build and `GT4_ASSETS_ROOT` the main checkout, `unittest discover -s tests/python` 281 OK, 12 skipped.
Headless: `external_runtime_probe.py` with `--runtime-manifest docs/inputs/slice178-main.json`, 4,200 frames, the 12 slice157
presses; 42/42 frames identical to `private/slice157-smoke/headless-157/frames` for `GT4_MTVU=1` (three runs) and `GT4_MTVU=0`
(one run), and for the default environment (one run).

Owner commands (PowerShell):

```
python scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode main
$env:GT4_MTVU="0"; python scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode main
```

`--mode main` holds the race display at 480p (the launcher sets `force_480p`); the hidden opt-out is `GT4_FORCE_480P=0`, which the
probe records. `--mode mtvu` keeps the slice172 pin and binary. Owner A/B and weak-CPU runs are pending (MTVU-PLAN section 12.6).

## Slice191 - GS stream protocol and the `GT4_RENDERER=pcsx2` seam (TASK-035)

Branch `task/035-gs-stream`, worktree `C:\Antigravity\gt4-wt-035-stream` (no `private/` junction; derived capsules and
the build live in the main checkout's `private/`). Spec: [GS-STREAM-PROTOCOL](GS-STREAM-PROTOCOL.md).

Derivations (fresh directories; `<A>` = `C:/Antigravity/gt4-staticrecomp`):

```
python scripts\prepare_external_vulkan.py <A>/private/slice191-gs-stream-port --assets-root <A> --gs-stream-hooks
python scripts\prepare_perf_runtime.py <A>/private/slice191-gs-stream-runtime --assets-root <A> --gs-stream
```

Controls: without `--gs-stream-hooks` the port derivation reproduces `slice153-pacing-port` byte for byte; without
`--gs-stream` the runtime derivation reproduces `slice178-main-runtime` (checked in `tests/python/test_gs_stream.py`).
The slice191 runtime changes only `gif.cpp` and `hw.cpp`; the port changes only `gs.cpp`. Pin:
`docs/inputs/slice191-gs-stream-runtime.json` (receipt sha256 d41d2d689a384efb5b5df116eba9750d43bd67870fb7d162067695ec8eab2326,
profile `task035-seam-v1`, port `gs.cpp` 12a989e6...). CMake accepts the slice191 port only with the slice191 runtime and
links `gt4_gs_stream_client` into the runtime for that pair.

Configure and build (clang-cl, Ninja, Release; same EE library, VU1/VU0 tables and slice153 shaders as slice178):

```
cmake -S C:\Antigravity\gt4-wt-035-stream -B C:\Antigravity\gt4-staticrecomp\private\slice191-gs-stream-build\build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DGT4_EE_PROFILE=production ^
  -DGT4_ASSETS_ROOT=C:/Antigravity/gt4-staticrecomp -DGT4_PYTHON=C:/Antigravity/gt4-staticrecomp/private/tooling-venv/Scripts/python.exe ^
  -DGT4_PERF_RUNTIME_SOURCE=C:/Antigravity/gt4-staticrecomp/private/slice191-gs-stream-runtime ^
  -DGT4_PORT_DIR=C:/Antigravity/gt4-staticrecomp/private/slice191-gs-stream-port -DGT4_SHADER_DIR=C:/Antigravity/gt4-staticrecomp/private/slice153-pacing-shaders ^
  -DGT4_VU_CORPUS_DIR=C:/Antigravity/gt4-staticrecomp/private/slice155-vu1-full-corpus ^
  -DGT4_VU1_GENERATED_DIR=C:/Antigravity/gt4-staticrecomp/private/slice157-vu1-all-gen ^
  -DGT4_VU0_GENERATED_DIR=C:/Antigravity/gt4-staticrecomp/private/slice160-vu0-gen-r3 ^
  -DGT4_VU0_IMAGE_FILE=C:/Antigravity/gt4-staticrecomp/private/slice160-vu0-image/vu0_image.bin
cmake --build C:\Antigravity\gt4-staticrecomp\private\slice191-gs-stream-build\build
```

Result: `gt4vulkan.exe` 95,634,432 bytes, sha256 03173665d6cbb98c5b3a691158d43cbe212a09bdab9eea0bcda612563f4f6490;
`gs_stream_record.exe` sha256 b5cf7e2b83f93eac6320f45a79071f3cb145d74853de885b611c2480330a4f41. Launcher pin
`docs/inputs/slice191-pcsx2-stream.json` (mode `pcsx2-stream`; the manifest's `gs_stream` block pins the recorder as the
GS process). Not for owner play: in `pcsx2` mode the window stays black (the consumer is not a renderer).

Checks (2026-10-15, idle machine):

- CTest 29/29 (new: `native_gs_stream_ipc`, `native_gs_stream_client`; `native_mtvu_perf` now checks that the four
  `gs_ipc_*` categories follow the MTVU ones). Python (`GT4_NATIVE_BUILD` on this build, `GT4_ASSETS_ROOT` the main
  checkout): 321 tests OK, 12 skipped.
- Headless, 4,200 frames, slice157 schedule (12 presses), `external_runtime_probe.py --runtime-manifest
  docs/inputs/slice191-pcsx2-stream.json`, outputs `private/slice191-smoke/final-*`:
  - `GT4_RENDERER` unset: 42/42 frames identical to `private/slice157-smoke/headless-157/frames`.
  - `GT4_RENDERER=donor GT4_GS_CAPTURE=...`: 42/42 identical; capture 2,599,116,800 bytes, sha256 e651bba3....
  - `GT4_RENDERER=pcsx2` with the recorder (`GT4_GS_RECORD_OUTPUT`) and `GT4_GS_CAPTURE`: run completes (frame limit
    reached, no frame dumps in this mode); the in-process capture is byte-identical to the donor capture (e651bba3...),
    and the recorder's file is the same bytes plus the 48-byte CONTROL shutdown record (sha256 73bee2da...).
  - Stream: 1,637,885 records, 2.60 GB, 619 KB and 390 records per frame on average (1,612,695 GIF records, 20,986
    PRIV, 0 READ_FIFO, 3 RESET). Readbacks: 0 in this schedule (menus).
- Converter and PoC replay: `gscap_to_gsdump.py <recorded> stream-0-1100.gs --max-frames 1101` gives a dump
  byte-identical to the TASK-034 PoC dump `private/slice190-dump/menu-0-1100.gs`; the PoC `pcsx2-gsrunner.exe`
  (`private/slice190-run`, 1x, `-surfaceless -renderer vulkan -loop 1 -perf -dumpdir`) produced 1,100 PNGs, all
  byte-identical to `private/slice190-gsrunner-out/1x/f` (frames 300 and 700 included). Outputs: `private/slice191-gsrunner`.

Headless commands (PowerShell, from the worktree; add the stream variables per case):

```
$env:GT4_RENDERER="pcsx2"; $env:GT4_GS_RECORD_OUTPUT="C:\...\stream.gscap"; $env:GT4_MTVU_REPORT_EVERY="500"
python scripts\external_runtime_probe.py C:/Antigravity/gt4-staticrecomp/private/slice191-smoke/<run> --frames 4200 --timeout 2400 `
  --press 600:start:30 ... --card-seed C:/Antigravity/gt4-staticrecomp/private/slice113-video/widescreen-seed/card.ps2 `
  --runtime-manifest docs/inputs/slice191-pcsx2-stream.json --assets-root C:/Antigravity/gt4-staticrecomp
```


## Slice193 - live PCSX2 GS process `gt4-gs.exe` (TASK-036)

Branch `task/036-gs-live` (worktree `C:\Antigravity\gt4-wt-036-live`, from main `57d1fa5`), fork branch `gt4gs/live`
(`C:\Antigravity\gt4recomp-gs`). Design and results: [GS-LIVE](GS-LIVE.md).

Game build: the slice191 runtime capsule, port and shaders are reused unchanged (CMake accepts the pair as before);
only `native/gs_stream/gs_stream_client.*` (GS process pin, `--config`, `WS_CLIPCHILDREN`, crash message),
`tools/gs_stream_record` (accepts `--config`) and the new `tools/gs_live_check` changed.

```
cmake -S C:\Antigravity\gt4-wt-036-live -B C:\Antigravity\gt4-staticrecomp\private\slice193-gs-live-build\build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DGT4_EE_PROFILE=production ^
  -DGT4_ASSETS_ROOT=C:/Antigravity/gt4-staticrecomp -DGT4_PYTHON=C:/Antigravity/gt4-staticrecomp/private/tooling-venv/Scripts/python.exe ^
  -DGT4_PERF_RUNTIME_SOURCE=C:/Antigravity/gt4-staticrecomp/private/slice191-gs-stream-runtime ^
  -DGT4_PORT_DIR=C:/Antigravity/gt4-staticrecomp/private/slice191-gs-stream-port -DGT4_SHADER_DIR=C:/Antigravity/gt4-staticrecomp/private/slice153-pacing-shaders ^
  -DGT4_VU_CORPUS_DIR=C:/Antigravity/gt4-staticrecomp/private/slice155-vu1-full-corpus ^
  -DGT4_VU1_GENERATED_DIR=C:/Antigravity/gt4-staticrecomp/private/slice157-vu1-all-gen ^
  -DGT4_VU0_GENERATED_DIR=C:/Antigravity/gt4-staticrecomp/private/slice160-vu0-gen-r3 ^
  -DGT4_VU0_IMAGE_FILE=C:/Antigravity/gt4-staticrecomp/private/slice160-vu0-image/vu0_image.bin
cmake --build C:\Antigravity\gt4-staticrecomp\private\slice193-gs-live-build\build
```

`gt4-gs.exe` (fork commit `acf4f986e`, local build): dependencies with the fork's
`build-dependencies-gt4gs.bat` (`DEBUG=0 BUILD_FFMPEG=0`) into `private/slice193-gt4gs-deps/root/deps`, then (VS 2022
Build Tools, `vcvars64.bat`):

```
cmake -S C:\Antigravity\gt4recomp-gs -B C:\Antigravity\gt4-staticrecomp\private\slice193-gt4gs-build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DENABLE_QT_UI=OFF -DENABLE_GSRUNNER=OFF -DENABLE_GT4GS=ON -DENABLE_TESTS=OFF ^
  -DDISABLE_ADVANCE_SIMD=ON -DCMAKE_PREFIX_PATH=C:\Antigravity\gt4-staticrecomp\private\slice193-gt4gs-deps\root\deps
cmake --build C:\Antigravity\gt4-staticrecomp\private\slice193-gt4gs-build --target pcsx2-gt4gs
powershell -File .github\workflows\scripts\windows\package-gt4gs.ps1 -Version 0.2.0-live -Build <build> -Deps <deps> -OutDir private\slice193-gt4gs-dist
```

The zip (`gt4-gs-0.2.0-live-windows-x64.zip`, 13,390,625 bytes, sha256 b32d7f8d...) is extracted to
`private/slice193-gt4gs-pkg` (140 files). Scripts: `private/slice193-gt4gs-deps/{build-deps,build-gt4gs,build-game}.bat`.

Pins: `docs/inputs/slice193-pcsx2.json` (mode `pcsx2`): `gt4vulkan.exe` sha256 d15913f2..., `gt4-gs.exe` sha256
1508c6d9... (runtime dependency), package tree sha256 45da0a68... (140 files) in `gs_stream.gs_package`.

Checks (2026-10-16): CTest 29/29; Python 335 OK, 13 skipped; headless donor 42/42 identical to slice157; headless
pcsx2 1x (`GT4_GS_SCALE=1 GT4_GS_ASPECT=auto`): 41/41 live PNGs identical to the offline `--replay-gscap` PNGs, frames
300 and 700 identical to the TASK-034 PoC; pcsx2 3x completes; GS process killed mid-run halts the game; hidden-window
check passes (attach, resize, snapshot, shutdown; kill). Outputs: `private/slice193-smoke`, `private/slice193-gt4gs-run`,
`private/slice193-compare`.

Headless pcsx2 command (PowerShell, from the worktree):

```
$env:GT4_RENDERER="pcsx2"; $env:GT4_GS_SCALE="1"; $env:GT4_GS_ASPECT="auto"; $env:GT4_MTVU_REPORT_EVERY="500"
C:\Antigravity\gt4-staticrecomp\private\tooling-venv\Scripts\python.exe scripts\external_runtime_probe.py `
  C:/Antigravity/gt4-staticrecomp/private/slice193-smoke/<run> --frames 4200 --timeout 2400 --press 600:start:30 ... `
  --card-seed C:/Antigravity/gt4-staticrecomp/private/slice113-video/widescreen-seed/card.ps2 `
  --runtime-manifest docs/inputs/slice193-pcsx2.json --assets-root C:/Antigravity/gt4-staticrecomp
```

Offline replay and window check:

```
private\slice193-gt4gs-pkg\gt4-gs.exe -dumpdir <dir> [--config <ini>] --replay-gscap private\slice191-smoke\streams-final\pcsx2-recorded.gscap
private\slice193-gs-live-build\build\gs_live_check.exe private\slice193-gt4gs-pkg\gt4-gs.exe <capture.gscap> 1100 <gt4-gs.ini> [--kill-at 300]
```

## Slice194 - readbacks, cache, window and crash handling (TASK-037/038)

Branch `task/037-038-gs-window` (worktree `C:\Antigravity\gt4-wt-037-gs`, from main `5cb714e`), fork branch
`gt4gs/window` (`C:\Antigravity\gt4recomp-gs`, commit `10851476b`, tag `gt4gs-v0.3.0`). Design and results:
[GS-LIVE](GS-LIVE.md) section 6.

Game build (same capsules as slice191/193; changed: `native/gs_stream/gs_stream_client.cpp`, `native/window.cpp`,
`native/CMakeLists.txt`, `tools/gs_live_check`), script `private/slice194-gt4gs-scripts/build-game.bat`:

```
cmake -S C:\Antigravity\gt4-wt-037-gs -B C:\Antigravity\gt4-staticrecomp\private\slice194-gs-window-build\build -G Ninja ^
  (the slice193 options, unchanged)
cmake --build C:\Antigravity\gt4-staticrecomp\private\slice194-gs-window-build\build
```

`gt4-gs.exe` 0.3.0, local build (`private/slice194-gt4gs-scripts/build-gt4gs.bat`, dependencies of
`private/slice193-gt4gs-deps`, fresh build directory `private/slice194-gt4gs-build`), packaged with
`package-gt4gs.ps1 -Version 0.3.0` into `private/slice194-gt4gs-dist` (`gt4-gs-0.3.0-windows-x64.zip`, 13,394,973 bytes,
sha256 `3dbacd9b...`) and extracted to `private/slice194-gt4gs-pkg` (140 files). The CI prerelease `gt4gs-v0.3.0` is
built from the same commit (another compiler, LTO) and is not the pinned binary.

Pins: `docs/inputs/slice194-pcsx2.json` (mode `pcsx2`): `gt4vulkan.exe` 95,655,936 bytes sha256 `e81d7e57...`, package
tree sha256 `ec8c12dd...` (140 files). The worktree holds copies (files, not links) of every pinned `private/` file the
launcher needs, checked by hash.

Checks: CTest 29/29; Python 350 OK, 14 skipped; donor 42/42 identical to slice157; pcsx2 1x 41/41 PNGs identical to the
slice193 offline replay; GS kill halts after the grace VSYNCs. Headless script:
`private/slice194-gt4gs-scripts/headless.sh <run> [GT4_RENDERER=pcsx2 GT4_GS_SCALE=1 GT4_GS_ASPECT=auto]`; outputs in
`private/slice194-smoke`. Window and readback checks:

```
gs_live_check.exe <pkg>\gt4-gs.exe private\slice191-smoke\streams-final\pcsx2-recorded.gscap 1100 <ini> --pace --readbacks --readback-check accurate --aspect-check
gs_live_check.exe ... 900 <ini with [GT4GS] DebugStallWindowMs = 3000, DebugStallAfterMs = 4000> --pace --stall-check
gs_live_check.exe ... 3100 <ini> --soak 3000
```

## Slice195 - click fix (TASK-038)

slice194 plus `native/window.cpp` (keyboard focus on activation in pcsx2 mode) and gt4-gs 0.3.1 (fork commit
`0099d77e1`, tag `gt4gs-v0.3.1`). Fresh directories: `private/slice195-gs-window-build` (game,
`private/slice194-gt4gs-scripts/build-game-195.bat`), `private/slice195-gt4gs-build` (gt4-gs, `build-gt4gs-195.bat`),
`private/slice195-gt4gs-dist` (iteration package, not pinned). The pinned package is the PUBLISHED release
`gt4gs-v0.3.1`, built by `gt4gs/release-local.ps1` from the tag checkout (`private/slice195-gt4gs-release-build`),
downloaded back to `private/slice195-gt4gs-download` (sha256 `e24dd2b8...`, checked with SHA256SUMS) and extracted to
`private/slice195-gt4gs-release-pkg` (140 files, tree sha256 `243fa487...`). Pins `docs/inputs/slice195-pcsx2.json`
(`gt4vulkan.exe` sha256 `afaa1e44...`); `--mode pcsx2` points here, `--mode pcsx2-slice194` keeps slice194. Checks:
CTest 29/29, Python 350 OK, donor 42/42, pcsx2 1x 41/41, `gs_live_check --click-check` and `--stall-check` pass.

## Slice196 - 480i deinterlace (TASK-047)

slice195 plus `native/gs_stream/gs_stream_client.cpp` (SetGsCrt posts SYNCV for NTSC/PAL), protocol 1.1
(`native/gs_stream/gs_stream_protocol.h`) and gt4-gs 0.3.2 (fork branch `gt4gs/deinterlace`, commit `08d394a87`, tag
`gt4gs-v0.3.2`). Same capsules as slice191-195. Scripts in `private/slice196-scripts`:

- game: `build-game-196.bat` (source = this checkout, fresh build dir `private/slice196-gs-deinterlace-build/build`);
- gt4-gs iteration build: `build-gt4gs-196.bat` (`private/slice196-gt4gs-build`), packaged with
  `package-gt4gs.ps1 -Version 0.3.2-dev` into `private/slice196-gt4gs-dist` (not pinned);
- release: `gt4gs/release-local.ps1 -Tag gt4gs-v0.3.2 -Build private\slice196-gt4gs-release-build -Deps
  private\slice193-gt4gs-deps\root\deps -OutDir private\slice196-gt4gs-release` (log `release-0.3.2.log`), published as
  the prerelease `gt4gs-v0.3.2`, downloaded back to `private/slice196-gt4gs-download` (zip sha256 `239030d9...`,
  SHA256SUMS OK, byte-identical to the local zip) and extracted with Expand-Archive to
  `private/slice196-gt4gs-release-pkg` (140 files, tree sha256 `4a62262a...`, `gt4-gs.exe` sha256 `95d89607...`).

Pins `docs/inputs/slice196-pcsx2.json` (`gt4vulkan.exe` 95,655,936 bytes, sha256 `f09489df...`); `--mode pcsx2` points
here, `--mode pcsx2-slice195` keeps slice195. Headless: `private/slice196-scripts/headless-196.sh <run> [env]`, outputs
in `private/slice196-smoke`. Deinterlace evidence: `tools/gs_frame_metrics/gs_frame_metrics.py <dir> --pattern
NAME_frame{:05d}.png --first N --last M`; work files `private/slice196-deinterlace-work`, samples
`private/slice196-deinterlace-samples`; [GS-LIVE](GS-LIVE.md) section 8. Offline replays need a writable data root,
`[GT4GS] DataRoot = <dir>` in the `--config` file (without it `gt4-gs.exe` exits with code 2 when
`%LOCALAPPDATA%\GT4Recomp` cannot be created, for example in a sandbox).

Checks: CTest 29/29 (the stream client fixture checks the SYNCV values and the SMODE1, SYNCV, SMODE2 order); Python
410 OK, 6 skipped; donor 42/42 identical to slice195; pcsx2 1x live 41 frames: 40 differ from slice195 (the 480i
frames are now deinterlaced), and frames 100-1400 are pixel-identical to the offline 0.3.2 replay of the SYNCV
stream with the live settings (slice195 live = offline 0.3.1 replay of the old stream, same method).

## Slice198 - PCSX2 GS path performance (TASK-041)

Branch `task/041-gs-perf` (from main `a1a06a4`). Same capsules, shaders, VU tables and gt4-gs package (`gt4gs-v0.3.2`,
`private/slice196-gt4gs-release-pkg`) as slice196; native game-side changes only (`native/mtvu_*`,
`native/gs_stream/gs_stream_client.*`, `gs_stream_ipc.*`, `perf_probe.*`). Scripts in `private/slice198-scripts`:

- game: `build-game-198.bat` (source = worktree `C:\Antigravity\gt4-wt-041-perf`, build dir
  `private/slice198-gs-perf-build/build`, assets root the main checkout); `guard.ps1` waits until no ninja, cl,
  clang-cl or link process runs (another task builds in parallel);
- pin: `make_manifest.py <worktree> <assets root>` writes `docs/inputs/slice198-pcsx2.json` from slice196 with the
  new runtime pin;
- headless: `headless-198.sh <run> [env]` (slice157 menus schedule), `headless-race.sh` / `race-run.sh <run> [env]`
  (race schedule, 16,000 frames, keeps `probe-nosys.log`: the syscall trace makes the full log about 1 GB),
  `analyze.py <first report> <runs>` (averages of the F8-equivalent reports), timing sets `timing-set1.sh`
  (slice196 vs slice198, privileged sync on/off, wake batching off), `timing-set2.sh` (cold/warm cache, GS spin),
  `timing-set3.sh` (donor vs pcsx2 menus); outputs in `private/slice198-smoke`, race route exploration
  `explore.sh`.

Pins `docs/inputs/slice198-pcsx2.json` (`gt4vulkan.exe` 95,667,712 bytes, sha256 `8658cffa...`); `--mode pcsx2` points
here, `--mode pcsx2-slice196` keeps slice196. The worktree holds file copies (no links) of every pinned file of both
modes plus the gt4-gs package, cache and data folders (`private/slice198-gs-perf-build`, `slice196-gs-deinterlace-build`,
`slice153-pacing-shaders`, `slice196-gt4gs-release-pkg`, `slice193-gt4gs-cache`, `slice193-gt4gs-data`; hashes and the
package tree digest checked), so `--mode pcsx2` runs from the worktree.

Switches: `GT4_MTVU_PRIV_ASYNC` (default 1; 0 = slice196 privileged sync), `GT4_GS_WAKE_BYTES` (default 262144; 0 =
wake on every record), `GT4_GS_STREAM_HASH=1` (test aid: F8 and session stream hash).

Checks: CTest 29/29; Python 416 OK, 16 skipped; donor 42/42 identical to slice157 (two runs); pcsx2 1x 41/41 identical
to slice196 (two runs); menus `.gscap` donor/pcsx2 slice198 = pcsx2 slice196 (`4ff3993d...`); race stream hash with the
privileged sync on and off identical (`9b88c23e984ff940`, 588 queued writes), race frames 159/159 identical.
Details and timings: [GS-LIVE](GS-LIVE.md) section 9.

## Slice197 - JIT-free gt4-gs (TASK-042)

Branch `task/042-gs-nojit` (worktree `C:\Antigravity\gt4-wt-042-nojit`, from main `a1a06a4`), fork branch `gt4gs/nojit`
(worktree `C:\Antigravity\gt4recomp-gs-nojit`, commit `4c8b63a6e`, tag `gt4gs-v0.4.0`). No game change: the pin
`docs/inputs/slice197-pcsx2.json` keeps the slice196 runtime (`private/slice196-gs-deinterlace-build/build/gt4vulkan.exe`,
sha256 `f09489df...`) and pins the published gt4-gs 0.4.0 package.

- gt4-gs iteration build: fork recipe plus `-DGT4GS_ONLY=ON` into `private/slice197-gt4gs-build` (712 steps cold; removed
  after the release), packaged by hand into `private/slice197-gt4gs-devpkg` (0.3.2 DLLs and resources, new exe).
- Release: `gt4gs/release-local.ps1 -Tag gt4gs-v0.4.0 -Build private\slice197-gt4gs-release-build -Deps
  private\slice193-gt4gs-deps\root\deps -OutDir private\slice197-gt4gs-release -NoPublish` (log
  `private/slice197-release-0.4.0.log`), then `gh release create` with the same assets and notes; downloaded back to
  `private/slice197-gt4gs-download` (SHA256SUMS OK, identical to the local zip) and extracted with Expand-Archive to
  `private/slice197-gt4gs-release-pkg` (140 files, tree sha256 `e9402cf2...`, `gt4-gs.exe` sha256 `1c4c45da...`).
- Static check: `powershell -File tools\gs_nojit_check\check-nojit.ps1 -Path private\slice197-gt4gs-release-pkg
  [-Report <json>]` (needs Visual Studio's `dumpbin`, found with vswhere); exit 0 = pass.
- Streams for the offline checks (re-captured, the TASK-035/047 files were gone): `GT4_GS_CAPTURE` on a headless slice157
  schedule, slice195 runtime (no SYNCV, sha256 `e651bba3...` = the slice191 capture) and slice196 runtime (SYNCV, sha256
  `4ff3993d...`); deleted after the checks (2.6 GB each; recapture in about a minute).
- Offline replay: `gt4-gs.exe -perf --config <ini> --replay-gscap <stream>` with `[GT4GS] DataRoot`, `DumpDir`,
  `DumpEvery` and `[Folders] Cache = private/slice197-gt4gs-cache`; frame comparison by bytes, then pixels. Reports
  `private/slice197-runs/*.json`, console logs `private/slice197-runs/*/console.log`.
- The worktree holds copies (files, not links) of the pinned `private/` files: the slice196 `gt4vulkan.exe`, the nine
  `slice153-pacing-shaders` files, `slice197-gt4gs-release-pkg`, `slice196-gt4gs-release-pkg`, `slice193-gt4gs-cache`
  and `slice193-gt4gs-data`, checked by hash and tree digest.

Checks: CTest 29/29 (slice196 build); Python 414 OK, 15 skipped (`GT4_NATIVE_BUILD` = the slice196 build; new
`tests/python/test_gs_nojit.py`); static check pass (0.3.2 fails it with 22 markers); offline 1x/3x frames byte-identical to
0.3.2 on both menu streams; headless game from the worktree: donor 42/42 identical to slice157, pcsx2 1x 41/41 identical to
slice196 live, GS process exit 0, ACG on and 0 executable non-image regions in the log. Details: [GS-LIVE](GS-LIVE.md)
section 10.


## Slice199 - TASK-041 on the JIT-free gt4-gs 0.4.0

Branch `task/041-gs-perf` (worktree `C:\Antigravity\gt4-wt-041-perf`), merged with main `ecda378` (TASK-042 merged,
PR #33). No game change: the game build is slice198 (`private/slice198-gs-perf-build/build/gt4vulkan.exe`, 95,667,712
bytes, sha256 `8658cffa...`, the same file as slice198), so no new executable and no CMake run. The GS package is the
published `gt4gs-v0.4.0` of slice197 (`private/slice197-gt4gs-release-pkg`, 140 files, tree `e9402cf2...`, zip sha256
`5c92ad00...`, `gt4-gs.exe` 8,928,256 bytes, sha256 `1c4c45da...`).

Pins: `docs/inputs/slice199-pcsx2.json` (new; slice198 and slice197 pins are not edited). `--mode pcsx2` = slice199;
`--mode pcsx2-slice198` (gt4-gs 0.3.2) and `--mode pcsx2-slice197` (game build of slice196, gt4-gs 0.4.0) stay for A/B.
The worktree holds file copies (no links) of every pinned input of slice199, copied from the main checkout and checked by
size and sha256 (18 files plus the 140-file package tree digest).

Checks (2026-10-16): CTest 29/29 on `private/slice198-gs-perf-build/build`; Python `GT4_NATIVE_BUILD` = that build:
438 tests OK, 20 skipped (missing data only); no-JIT static check `tools/gs_nojit_check` on the slice199 package: 23
modules, no code generator markers, no writable+executable sections; headless (slice157 menus schedule, 4,200 frames):
donor 42/42 identical to slice157; pcsx2 1x 41/41 identical to slice198 (gt4-gs 0.3.2); gt4-gs 0.4.0 log: Arbitrary Code Guard on, 0 executable non-image regions.

Headless (PowerShell, from the worktree; outputs in the main checkout's `private/slice199-smoke`, the probe refuses
outputs outside `private/`):

```
python scripts\external_runtime_probe.py C:/Antigravity/gt4-staticrecomp/private/slice199-smoke/<run> --frames 4200 --timeout 2400 `
  --press 600:start:30 ... --card-seed C:/Antigravity/gt4-staticrecomp/private/slice113-video/widescreen-seed/card.ps2 `
  --runtime-manifest docs/inputs/slice199-pcsx2.json --assets-root C:/Antigravity/gt4-staticrecomp
```

Add `GT4_RENDERER=pcsx2 GT4_GS_SCALE=1 GT4_GS_ASPECT=auto` for the pcsx2 runs (`GT4_MTVU_REPORT_EVERY=500` for the F8 lines).
Details: [GS-LIVE](GS-LIVE.md) section 10.5.

## Slice200 - release 1.0.0 hardening (TASK-058 crash-safe card, TASK-059 installed folder)

Branch `task/058-059-release-hardening` (worktree `C:\Antigravity\gt4-wt-058-harden`, from main `3b5334d`). The game
build changes: the executable, a new port (`private/slice200-port`, `main.cpp` only) and a new memory card capsule
(`private/slice200-memcard-runtime`). The GS package stays gt4-gs 0.4.0 (`private/slice197-gt4gs-release-pkg`).

- TASK-058, memory card: the atomic writer of TASK-025 Part A (`native/memcard_store.*`, `memcard_store_fixture.cpp`,
  `scripts/prepare_memcard_flush.py`, `tests/python/test_memcard_flush.py`) was on `task/025-save-manager` (commit
  `c9e90bd`), not on main; it is imported here with its CMake and `window.cpp` hunks. The memcard capsule is derived by
  `scripts/prepare_memcard_flush.py --assets-root <main checkout>` into `private/slice200-memcard-runtime` (`sio2.cpp`,
  `runtime.json` with its SHA-256 `8e3bf667...`). CMake option `GT4_MEMCARD_RUNTIME_SOURCE` replaces the donor
  `iop/sio2.cpp`, links `gt4_memcard` and defines `GT4_MEMCARD_FLUSH_OVERLAY` for `gt4vulkan` (window.cpp calls
  `flush_cards_now()` after `emulate()` returns, which covers the halt after the GS-process grace VSYNCs as well).
  A failed write keeps the card dirty and retries after 50 VBlanks.
- TASK-059, installed folder: `native/install_paths.*` (shaders in `<exe folder>\shaders`, else the build-time folder;
  GS process `GT4_GS_PROCESS`, else `<exe folder>\gs\gt4-gs.exe`), used by `native_vulkan.cpp` and `gs_stream_client.cpp`;
  `--portable-check` and the headless exit flush (`flush_cards_now()` after `emulate()`) in the port `main.cpp`. The TASK-019 branch version of the shader change had a broken `printf`
  string (a literal line break) and was rewritten here.
- Pins: `docs/inputs/slice200-pcsx2.json` (new; `--mode pcsx2` = slice200; `--mode pcsx2-slice199` keeps the slice199
  runtime) and `docs/inputs/slice200-gs-stream-runtime.json` (new port manifest: the slice191 manifest with the new
  `main.cpp` hash). slice199 and slice191 pins are not edited.
- `--mode main` (slice178) is not rebuilt: its build is not shared with slice200, so it keeps the old card flush and no
  `--portable-check`. A slice200 main build would need its own pin.

Build (fresh directories in the main checkout; `cmd /c` with the VS x64 environment, same cache as slice198 except the
new capsules):

```
cmake -S C:\Antigravity\gt4-wt-058-harden -B C:\Antigravity\gt4-staticrecomp\private\slice200-build\build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DGT4_EE_PROFILE=production ^
  -DGT4_ASSETS_ROOT=C:/Antigravity/gt4-staticrecomp -DGT4_PYTHON=<main>/private/tooling-venv/Scripts/python.exe ^
  -DGT4_PERF_RUNTIME_SOURCE=<main>/private/slice191-gs-stream-runtime -DGT4_PORT_DIR=<main>/private/slice200-port ^
  -DGT4_SHADER_DIR=<main>/private/slice153-pacing-shaders -DGT4_VU1_GENERATED_DIR=<main>/private/slice157-vu1-all-gen ^
  -DGT4_VU0_GENERATED_DIR=<main>/private/slice160-vu0-gen-r3 -DGT4_VU0_IMAGE_FILE=<main>/private/slice160-vu0-image/vu0_image.bin ^
  -DGT4_VU_CORPUS_DIR=<main>/private/slice155-vu1-full-corpus -DGT4_MEMCARD_RUNTIME_SOURCE=<main>/private/slice200-memcard-runtime
cmake --build C:\Antigravity\gt4-staticrecomp\private\slice200-build\build
```

Result: `gt4vulkan.exe` 95,680,000 bytes, sha256 `256c883b...` (the slice198 build was 95,667,712 bytes).

Checks (2026-10-16, build `private/slice200-build/build`):

- CTest 29/29 (new: `native_memcard_store`, the atomic writer with the crash-injection cases).
- Crash-safe card, kill test (`tests/python/test_memcard_kill.py`): the fixture `memcard_store_fixture --writer <card> 0`
  rewrites a copy in two alternating complete images; the test kills it 25 times at random moments (TerminateProcess) and
  requires the card to be byte-identical to one of the two images. Passed.
- Installed paths (`tests/python/test_portable_install.py`): installed shaders and GS package found next to the
  executable; missing shader fails (exit 1); `GT4_GS_PROCESS` override reported; build-time fallback. Passed.
- Distribution (`tests/python/test_make_distribution.py`): folder content, no GS package, no game data, changed exe
  refused, output outside `private/` refused. Passed.
- Donor headless, `--mode`-equivalent command from the distribution folder (`private/slice200-dist`, see
  [DISTRIBUTION](DISTRIBUTION.md)): 42/42 frames identical to `private/slice157-smoke/headless-157/frames`, and to the
  slice199 donor run.
- pcsx2 headless from the distribution folder (`GT4_GS_PROCESS` = slice197 gt4-gs 0.4.0): 41/41 PNG dumps identical to
  the slice199 pcsx2 run; Arbitrary Code Guard on.
- Card in those runs: the menus schedule (4,200 frames, the slice157 presses) never saves. The `card.ps2` of the donor
  and pcsx2 runs is byte-identical to the seed (`b757feff...`), as is the slice199 donor run. So the headless byte
  comparison of a saved card is not available for this schedule; the save path is covered by the fixture, the kill test
  and the build. An in-game save check is the owner's test.

Path audit (`gt4vulkan.exe`): one build-machine string remains, `C:/Antigravity/gt4-staticrecomp/private/slice153-pacing-shaders`
(the build-time shader fallback, kept for development builds). No pdb path, no user name. Details: [DISTRIBUTION](DISTRIBUTION.md).

Distribution: `scripts/make_distribution.py` wrote `private/slice200-dist` (16 files: `gt4vulkan.exe`, `shaders/*.spv`,
`vcruntime140.dll`, `msvcp140.dll`, `msvcp140_atomic_wait.dll`, `licenses/`, `MANIFEST.json`). No gt4-gs package, no game
data.

Worktree copies: the pinned `private/` inputs of slice200 were copied (files, no links) into `C:\Antigravity\gt4-wt-058-harden\private`
and checked by size and SHA-256 (16 checks; the 140-file gt4-gs package tree `e9402cf2...`). The disc image
(`Gran Turismo 4 (USA) (v2.00).iso`, 5.3 GB) is not copied: the probe checks it at the checkout root.

## Slice201 - cursor, shader notice, performance overlay, stable gt4-gs 1.0.0 (TASK-060/061/062)

Branch `task/060-062-osd` (worktree `C:\Antigravity\gt4-wt-060-osd`, from main `3b5334d`), fork branch `gt4gs/osd`
(worktree `C:\Antigravity\gt4recomp-gs-osd`, commit `3b9aae399`, tag `gt4gs-v1.0.0`). Same capsules, shaders, EE library
and VU tables as slice198/199; native changes only. Scripts in `private/slice201-scripts`:

- game: `build-game-201.bat [targets]` (source = the worktree, build dir `private/slice201-osd-build/build`, assets root the
  main checkout); `guard.ps1` / `guarded.sh <log> <command>` wait until no ninja, cl, clang-cl or link process runs;
- gt4-gs iteration build: `build-gt4gs-201.bat` (`GT4GS_ONLY=ON`, `private/slice201-gt4gs-build`), packaged by copying the
  new `gt4-gs.exe` over the 0.4.0 package into `private/slice201-gt4gs-devpkg` (not pinned);
- release: `release-100.sh` = `gt4gs/release-local.ps1 -Tag gt4gs-v1.0.0 -Build private\slice201-gt4gs-release-build -Deps
  private\slice193-gt4gs-deps\root\deps -OutDir private\slice201-gt4gs-release -Release` (log
  `private/slice201-release-1.0.0.log`); downloaded back to `private/slice201-gt4gs-download` and extracted to
  `private/slice201-gt4gs-release-pkg`;
- pin: `make_manifest.py <worktree> <assets root> private/slice201-gt4gs-release-pkg slice201-pcsx2.json <zip> <url>`;
  `copy_pins.py <worktree> <main checkout> slice201-pcsx2.json slice199-pcsx2.json` copies (never links) every pinned
  file, both package trees and the GS cache and data folders into the worktree's `private/` and checks them;
- checks: `headless-201.sh <run> [env]` (slice157 schedule, outputs in `private/slice201-smoke`), `cmp201.py`,
  `overhead.sh <runs>` (hidden-window overlay A/B with `tools/gs_live_check`), `ctest-201.bat`; commit hygiene
  `normalize_eol.py` (each file keeps its blob's line endings) and `check_blobs.py` (no CR CR LF, no mixed blobs).

Pins `docs/inputs/slice201-pcsx2.json` (`gt4vulkan.exe` 95,672,320 bytes, sha256 `f4737af4...`; gt4-gs 1.0.0 package tree
`c95edf1c...`); `--mode pcsx2` points here, `--mode pcsx2-slice199` keeps slice199. Fixtures (`tools/gs_live_check`):
`--cursor-check` and `--overlay-shot <bmp>` are VISIBLE (about 1-2 s, small window at a screen corner, topmost while they
run; they stop as inconclusive when another window covers them or the mouse moves); `--osd` posts synthetic OSD_STATS.

Checks: CTest 30/30; Python 455 OK, 25 skipped (`GT4_NATIVE_BUILD` = the slice201 build); donor 42/42 identical to
slice157; pcsx2 1x 41/41 identical to slice199; no-JIT static check passes on the published package (23 modules).
Details: [GS-LIVE](GS-LIVE.md) section 11.

## Slice203 - gt4-gs 1.0.1 (TASK-044: no writes under Documents\PCSX2)

Game executable `private/slice203-build/build/gt4vulkan.exe` (copied from the slice202 build, same hash `d4cb6de6...`,
95,685,120 bytes). Package: the stable gt4-gs 1.0.1 release (see [GS-FORK](GS-FORK.md)), extracted to
`private/slice203-gt4gs-release-pkg` (tree `55939b2b...`). Pin `docs/inputs/slice203-pcsx2.json` (slice202 fields with the
1.0.1 `gs_stream` and `runtime_dependencies`, written as a new file); `--mode pcsx2` = slice203, `pcsx2-slice202` kept.
The test `tests/python/test_gs_slice203_pin.py` checks the pin, the release values and the copied files.

Checks: Python suite 472 run, 18 skipped (`GT4_NATIVE_BUILD` = the slice202 build). CTest 29/30: `python_native_contracts`
discovers tests in the stale path `C:/Antigravity/gt4-wt-060-osd`, which does not exist any more (the slice202 build config),
so it finds nothing; it passes from the worktree (472 tests). Donor headless 4200 frames: 42/42 identical to slice202.
pcsx2 headless 4200 frames with `GT4_GS_SCALE=1 GT4_GS_ASPECT=auto` (the slice202 settings): 41/41 gs-frames identical to
slice202. no-JIT check of the 1.0.1 package: passed (`check-nojit.json` from the release script).

Documents\PCSX2: a `Documents\PCSX2` snapshot (path, size, mtime) before and after a live headless run and after an
offline `--replay-gscap` run: no changes. The offline replay stops after VM initialization with exit code -16843010, the same
point as the 1.0.0 logs (a limit of the replay test, not investigated). Not done: a clean-profile run with a redirected
USERPROFILE; the PCSX2 folder location comes from the known-folder API, so a redirect is not a reliable test.

Runs: the headless runs used the main checkout as assets root, with the slice203 build and package copied into
`private/slice203-build` and `private/slice203-gt4gs-release-pkg` there (new directories; the worktree copies are the same
files, hashes verified). The probe also reads the main checkout's own `private/fingerprint-check/CORE.GT4` and
`private/reconstructed/SCUS_973.28.elf` (not copied).

## Slice202 - TASK-058/059 and TASK-060/061/062 together, gt4-gs 1.0.0

`task/060-062-osd` merged with main `b955235` (TASK-058/059, PR #35); conflicts resolved keeping both sides
(`private/slice201-scripts/resolve_merge.py`). One game build with the slice200 recipe (port `private/slice200-port`, memcard
capsule `private/slice200-memcard-runtime`, `-DGT4_MEMCARD_RUNTIME_SOURCE`) and this source: `private/slice202-scripts/
build-game-202.bat` into `private/slice202-build/build` (`gt4vulkan.exe` 95,685,120 bytes, sha256 `d4cb6de6...`). The GS
package is the stable gt4-gs 1.0.0 of slice201 (`private/slice201-gt4gs-release-pkg`, tree `c95edf1c...`).

Pins `docs/inputs/slice202-pcsx2.json` (slice200 fields plus the slice201 `gs_stream` and runtime dependencies, written by
`private/slice202-scripts/make_manifest.py`); `--mode pcsx2` = slice202, `--mode pcsx2-slice201`, `pcsx2-slice200` and
`pcsx2-slice199` kept. The worktree holds file copies of the slice202 and slice200 pins (`copy_pins.py`, 32 checks).
Distribution: `scripts/make_distribution.py C:/Antigravity/gt4-staticrecomp/private/slice202-dist ... --exe
.../slice202-build/build/gt4vulkan.exe --runtime-manifest docs/inputs/slice202-pcsx2.json` (16 files); `--portable-check`
ok (9 of 9 shaders installed); headless smoke from the folder (`private/slice202-scripts/smoke_dist.py`).

Checks (2026-10-16): CTest 30/30; Python 469 OK, 17 skipped (`GT4_NATIVE_BUILD` = the slice202 build, `GT4_ASSETS_ROOT` =
the main checkout); headless from the worktree: donor 42/42 identical to slice157, pcsx2 1x 41/41 identical to slice201
(overlay off); from `private/slice202-dist`: donor 42/42, pcsx2 41/41 (gt4-gs pin matched, ACG on, 0 executable non-image
regions); no-JIT check of the 1.0.0 package as slice201.

gt4-gs and `Documents\PCSX2` (TASK-044 note): gt4-gs 1.0.0 writes under `Documents\PCSX2`: `VMManager::Internal::LoadStartupSettings()` runs before `[GT4GS] DataRoot` is applied, so it rewrites `Documents\PCSX2\logs\emulog.txt` every session (Confirmed: before/after snapshot of the folder) and creates the PCSX2 user folders there (bios, memcards, logs, ...) on a PC that has none (High confidence, `EmuFolders::EnsureFoldersExist`). The log line "MemoryCards Directory: ...Documents\PCSX2\memcards" is that early pass; the session itself uses DataRoot. Fix on fork branch `gt4gs/datadir` (commit `b30870fe8`, version 1.0.1, NOT released): the call moves after the stream folders; checked: 0 changes under `Documents\PCSX2`, emulog in DataRoot\logs. Check script:
`private/slice202-scripts/docs_check.ps1` (offline replay) and two headless live runs with a folder snapshot.

## Slice 208 - TASK-014 stage B candidate (`--mode pcsx2-coverage`)

The slice206 game with an EE library that has 1,105 accepted explicit translator entries (library profile `coverage`).
Only the library changes. `--mode pcsx2` stays on slice206. Pins: `docs/inputs/slice208-pcsx2.json` (runtime and library
hashes, measurement mode `pcsx2-coverage`) and `docs/inputs/slice208-ee-libraries.json` (library profile);
seeds `docs/inputs/slice208-ee-entries.txt` and `.json` (sha256 and expected count).

Steps (serial; fresh directories under the main checkout's `private/`):

1. Seeds: the high candidates plus the medium candidates with a decomp name from the Phase 1 ledger (1,105; see
   `docs/EE-ENTRY-LEDGER.md`).
2. Producer: `python scripts/ee_entries.py prepare --assets-root C:\Antigravity\gt4-staticrecomp --output C:\Antigravity\gt4-staticrecomp\private\slice208-ee-entries-producer`,
   then `python scripts/ee_entries.py generate --output <same> --seed-set accepted`. The translator reports 0 rejected
   seeds, 138,813 analysis entries and 0 stop sites; 325 of 326 units change (`generation-entries.json`).
3. Library: `private/slice208-scripts/build-library.cmd` configures `tools/ee_producer` with the portable clang-cl
   23.1.2 (`private/tooling/llvm-23.1.2/bin/clang-cl.exe`) and builds `ps2game` with one worker. The 325 changed
   objects differ from production; unchanged objects match (check: 8 sampled units compiled by 23.1.2 are identical to
   the production members, timestamp-normalised). Record: `python scripts/ee_entries.py record-library --output <same> --seed-set accepted --profile coverage`.
4. Executable: `private/slice208-scripts/build-game-208.bat` (`GT4_EE_PROFILE=coverage`, the slice206 capsules, port,
   shaders and GS package). The executable uses the system clang-cl 22.1.3, the compiler that built slice206, so that the
   library is the only changed variable.

Hashes: executable `private/slice208-build/build/gt4vulkan.exe` 97,104,896 bytes, sha256 `b96c6d64...`; library
`private/slice208-ee-entries-producer/game-build/ps2game.lib` 70,694,004 bytes, sha256 `8f40b11e...`. All 16 pinned
files verified by sha256 in the main checkout.

Checks (2026-10-20): CTest 33 of 33 (`private/slice208-scripts/ctest-208-r2.log`); Python 583 OK, 23 skipped
(`GT4_NATIVE_BUILD` = slice208 build, `GT4_ASSETS_ROOT` = main checkout). Headless, 4,200 frames, the slice157/202
12-press schedule and widescreen seed card, 42 frame dumps: slice206 (`private/slice208-smoke/base-206`) and slice208
(`private/slice208-smoke/cand-208`) are identical, 42 of 42 (per-file sha256). Neither probe log contains
"no translated code". The headless path does not show whether the 1,105 new entries run; that is the owner play-through.

Owner command (after this branch is merged): `python scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode pcsx2-coverage`.

## Slice 209 - TASK-019 launcher test: `GT4_FULLSCREEN` start (`--mode pcsx2`)

The slice208 build (coverage EE library) with one native change: the game starts in borderless fullscreen when
`GT4_FULLSCREEN=1` (the launcher's Display setting, on by default). Unset, empty or `0` start windowed; any other
value stops the start with a message. The start uses the Alt+Enter path (`set_fullscreen`) after the window exists.
Pins: `docs/inputs/slice209-pcsx2.json` (measurement mode `pcsx2`). `--mode pcsx2` now selects slice209; `pcsx2-coverage`
(slice208) and `pcsx2-slice206` stay as history modes.

Source: `private/slice209-src`, a `git archive` of main 8bd113a (identical to the slice208 source, 801ee94) with four
files replaced: `native/window.cpp` (only the `open_window` start block), `native/window_mode.hpp`, `native/window_mode.cpp`
(`parse_start_fullscreen`) and `native/quickfix_fixture.cpp` (`test_start_mode_environment`). Nothing else differs, so the
window change is the only variable against slice208. The branch's `native/` also carries earlier TASK-019 native work
(`controls.cpp`, `host_io`, the icon and title in `window.cpp`, `gt4vulkan.rc`); that work is not in slice209.

Build (serial; `private/slice209-scripts/build-game-209.bat`, log `build-game-209.log`): the slice208 recipe with
`GT4_EE_PROFILE=coverage`, the slice200 port, slice153 shaders, slice191 runtime, slice206 license guard, clang-cl 22.1.3
(`C:/Program Files/LLVM/bin/clang-cl.exe`) inside the VS x64 environment. Executable `private/slice209-build/build/gt4vulkan.exe`,
97,105,408 bytes, sha256 `a75c58faec60d41c...` (slice208: 97,104,896 bytes, `b96c6d64...`). The coverage library
`private/slice208-ee-entries-producer/game-build/ps2game.lib` is the pinned file (sha256 `8f40b11e...`), unchanged.

Checks (2026-10-20):
- CTest 33 of 33 (`private/slice209-scripts/ctest-209.log`). The `native_quickfix` fixture covers the start values: unset,
  empty and `0` start windowed; `1` starts fullscreen; `true`, `on` and `2` are refused.
- Python `unittest discover` with `GT4_NATIVE_BUILD` = the slice209 build and `GT4_ASSETS_ROOT` = the main checkout:
  589 OK, 23 skipped (`private/slice209-scripts/python-209.log`).
- Headless, 4,200 frames, the slice208 arguments (12 presses, widescreen seed card, `--frames-dir`, `--frame-every 100`,
  `GT4_MTVU_REPORT_EVERY=500`): 42 of 42 frame dumps identical to slice208 (`private/slice209-smoke/cand-209`
  against `private/slice208-smoke/cand-208`, per-file sha256). No `no translated code` line.
- The headless path never calls `open_window`, so the live fullscreen start is not covered by these runs; it is the owner's
  launcher test.
- Pins: the 23 pinned files of `docs/inputs/slice209-pcsx2.json` were checked by size and sha256 in the main checkout
  (runtime, library, port, shaders, runtime dependencies, memcard and license guard capsules, card seeds, VU0 image,
  gt4-gs package file list not recomputed here: unchanged from slice203).

Owner launcher test folder (`private/owner-test-launcher`): `game` is slice209 (16 files: `gt4vulkan.exe`,
`shaders/`, `licenses/`, the three VC runtime DLLs, `MANIFEST.json` with runtime manifest `slice209-pcsx2.json`), built with
`scripts/make_distribution.py` in a fresh `game-209` and renamed. The slice206 folder is `game-206-old`. `GT4Recomp.exe`
and `version.json` were not touched.

Owner command: `python scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode pcsx2`.

## Slice 210 - TASK-075 (game side) and TASK-076: SDL3 gamepads and the keyboard table (`--mode pcsx2`)

The slice209 build with the SDL3 gamepad layer and the TASK-076 keyboard table. Nothing else changes: guest code, EE
library (coverage), port (slice200), shaders (slice153), runtime capsule (slice191), license guard (slice206) and the
memory card capsule are the same. Pins: `docs/inputs/slice210-pcsx2.json` (measurement mode `pcsx2`) and
`docs/inputs/slice210-sdl3.json`. `--mode pcsx2` selects slice210; `pcsx2-slice209` stays as history.

SDL3 (zlib): the official SDL 3.4.18 Windows packages from the GitHub release `release-3.4.18`, fetched with
`gh release download`. Their sha256 equals GitHub's asset digest:

- `SDL3-devel-3.4.18-VC.zip` sha256 `78d84602ae616cfe26b33a73b7d3b9b6b56e8a6650e53d92c9c6ca938f747acd`
- `SDL3-3.4.18-win32-x64.zip` sha256 `75c2c0fc74e7d1206aaedc22893a35887332f4f85f5c8b74fddc10d177985f2c`

Extracted, not committed, to `private/tooling/sdl3-3.4.18/devel` (headers, `lib/x64/SDL3.lib`, licence) and
`private/tooling/sdl3-3.4.18/runtime` (`SDL3.dll`). `native/CMakeLists.txt` (`gt4_sdl3`) checks the import library,
`SDL3.dll` and the licence against the pin at configure time. Each executable that loads SDL (`gt4vulkan`,
`host_io_fixture`) gets `SDL3.dll` copied beside it by a POST_BUILD step. `scripts/make_distribution.py` ships the DLL
and `licenses/LICENSE-SDL3.txt`.

Source: this branch (`task/075-sdl3-input`, from `task/019-launcher` f64f723). Build script
`private/slice210-scripts/build-game-210.bat`: the slice209 recipe with the source `C:\Antigravity\gt4-wt-075`, the
`GT4_SDL3_ROOT` pin, clang-cl 22.1.3 (`C:/Program Files/LLVM/bin/clang-cl.exe`) in the VS x64 environment, serial
build. Executable `private/slice210-build/build/gt4vulkan.exe`, 97,184,768 bytes, sha256 `561a5e15...`.

Changes against slice209: `native/pad_mapping.{hpp,cpp}` (new, pure mapping), `native/host_io.{hpp,cpp}` (SDL3 backend;
the XInput path stays under `GT4_INPUT=xinput`), `native/window.cpp` (`GT4_INPUT`), `native/controls.cpp` (keyboard
defaults), `native/CMakeLists.txt` (SDL3 pin, link, DLL copy, `pad_mapping_fixture`; the duplicated `gt4_memcard` block
on the launcher branch is removed, it stopped configure), `native/host_io_fixture.cpp` (quiet loader errors),
`scripts/external_runtime_probe.py` (quiet loader errors on game launches). Also on the branch since TASK-019 and not in
slice209: `controls.cpp`, the window icon and title.

Checks (2026-10-20/21):

- CTest: `private/slice210-scripts/ctest-210.log`.
- Python `unittest discover` with `GT4_NATIVE_BUILD` = `private/slice210-build/build`: 589 tests OK, 41 skipped
  (`private/slice210-scripts/python-210.log`). The `FAIL:` lines in that log are subtest messages of the existing GS
  option tests; the run reports OK.
- Headless, 4,200 frames, the slice208 arguments (12 presses, widescreen seed card, `--frames-dir`, `--frame-every 100`,
  `GT4_MTVU_REPORT_EVERY=500`, no `--window`): 42 of 42 frame dumps identical to slice209
  (`private/slice210-smoke/cand-210` against `private/slice209-smoke/cand-209`, per-file sha256). No `no translated code`
  line. The headless path never creates the controller, so SDL is not started there.
- Pins: all 16 files of `docs/inputs/slice210-pcsx2.json` and the SDL3 library, DLL, licence and 86 headers of
  `docs/inputs/slice210-sdl3.json` checked by size and sha256 in the main checkout.

Owner launcher test folder (`private/owner-test-launcher`): `game` is slice210, made by `scripts/make_distribution.py`
in `game-210` and renamed (18 files, including `SDL3.dll` and `licenses/LICENSE-SDL3.txt`); the slice209 folder is
`game-209-old`. `GT4Recomp.exe`, `version.json` and `%LOCALAPPDATA%\GT4Recomp` were not touched.

Owner command: `python scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode pcsx2`.

## Slice 211 - TASK-079 force 16:9 at boot (`--mode pcsx2`)

The slice210 build plus `GT4_FORCE_WIDESCREEN`: the game's own Wide setting is held at 16:9 at every guest VBlank.
Source: this branch (`task/079-force-widescreen`, from `task/075-sdl3-input` 85b63cd). Build script
`private/slice211-scripts/build-game-211.bat` (the slice210 recipe, source `C:\Antigravity\gt4-wt-079`, clang-cl 22.1.3
as slice206-slice210). Executable `private/slice211-build/build/gt4vulkan.exe`, 97,186,816 bytes, sha256 `2fecda70...`.

Changes against slice210:
- `native/race_widescreen.{hpp,cpp}` (new, pure logic plus two entry points). It holds three guest words, all range-checked:
  the wide flag `owner + 0x38CC4` (= option + 0x14, set to 1); the aspect record `option + 0x24` (11 words, copied from the
  wide preset `0x622EC0`, or `0x622F20` for NTSC, when the record is not wide); the published word `0x6184F0` (D_006184F0,
  set to 1 when the record is wide). A wide preset that is not wide is refused with no write. Option = `owner + 0x38CB0`.
- `native/mtvu_runtime.cpp` `vblank_sync`: calls the hold at the top, so it runs at every guest VBlank in windowed and
  headless runs (the donor's `system.cpp` calls `gt4::mtvu::vblank_sync()` before `on_vblank`). The window's `on_vblank`
  is not used for it: `run_window` is never reached headless, and a static initializer for `g_options.on_vblank` was
  overwritten by the dynamic initialization of `Options` (std::string members). `native/CMakeLists.txt`: `gt4_mtvu_runtime`
  links `gt4_quickfix`.
- `native/window.cpp`: validates `GT4_FORCE_WIDESCREEN` at window start (invalid value: refused, exit before guest work).
- Fixture `native_race_widescreen` (CTest), `tests/python/test_race_widescreen.py` (launcher env, mode pin, ELF records).
- `scripts/external_runtime_probe.py`: `GT4_FORCE_WIDESCREEN` validated (unset means 1; `0` and `1` only), forwarded and
  recorded as `force_widescreen_environment`. `PERF_PROFILES['pcsx2']` is slice211; `pcsx2-slice210` is history.

Pinned ELF facts (checked in `private/reconstructed/SCUS_973.28.elf`): the wide and 4:3 preset sources at `0x6A61B8` and
`0x6A6188` are 425 x 240 and 275 x 205 (func_00439B30 initialises the presets from them).

Checks (2026-10-21):
- CTest: the new fixture and all other tests were run; `python_native_contracts` failed on an autocrlf artifact
  (`docs/inputs/slice208-ee-entries.txt` in the working copy) and on a pin test that was updated (slice 211). The
  worktree copy was restored from the committed LF blob.
- Python `unittest discover` with `GT4_NATIVE_BUILD` = slice211: see `private/slice211-scripts/python-211-final.log`.
- Headless, 4,200 frames, the slice210 arguments (12 presses, frame dump every 100, `--trace-syscalls`,
  `GT4_MTVU_REPORT_EVERY=500`), 42 frame dumps each:
  - (a) widescreen seed card (`private/slice113-video/widescreen-seed/card.ps2`), `GT4_FORCE_WIDESCREEN=0`: 42 of 42
    identical to slice210 (`private/slice210-smoke/cand-210`, per-file sha256).
  - (a2) the same, unset (force on): 42 of 42 identical to (a). The 16:9 card is unchanged by the hold.
  - (c) 4:3 candidate card (`private/slice110-menu-pilot/name-control4/card.ps2`, a copy, `GT4_FORCE_WIDESCREEN=0`):
    36 of 42 frames differ from (a) (title screen).
  - (d) the same 4:3 copy, unset (force on): 3 of 42 frames differ from (c) (frames 700, 800, 900, the title transition);
    the log shows `wide flag set to 16:9 at guest 0x009A7024 (owner 0x0096E360, ntsc 0)`. 35 of 42 frames still differ
    from (a).
  - Limit found: the headless schedule stays on the title screen, so no race, HUD or photo frame is compared. The
    16:9 state of the native seed card includes more than the three words held here; the remaining state is not identified.
    The owner's in-game test is the decision point.
- Pins: `docs/inputs/slice211-pcsx2.json` (runtime `private/slice211-build/build/gt4vulkan.exe`, 97,186,816 bytes, sha256
  `2fecda70...`). Every other pin is unchanged.

Owner launcher test folder (`private/owner-test-launcher`): `game` is slice211 (`make_distribution.py`, 18 files including
`SDL3.dll` and `licenses/LICENSE-SDL3.txt`); the slice210 folder is `game-210-old`; `GT4Recomp.exe` is the TASK-079 launcher
build (`launcher/target/release/GT4Recomp.exe` on `task/019-launcher`). `version.json` and `%LOCALAPPDATA%\GT4Recomp` untouched.

Owner command: `python scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode pcsx2`.

## Slice 213 - TASK-081 release build for 1.0.0 (`--mode pcsx2`)

The slice211 recipe plus `-DGT4_RELEASE_DISTRIBUTION=ON`. Source: branch `task/081-release-1-0-0` (from main 26f83b8).
Build script `private/slice213-scripts/build-game-213.bat` (clang-cl 22.1.3 as slice206-slice211, `--parallel 4`).
Executable `private/slice213-build/build/gt4vulkan.exe`, 97,188,352 bytes, sha256
`c503ee9c0729a9e0acc606762a0cb79c130829e311a771ac284e25a63c27422b`.

Changes against slice211:
- `native/CMakeLists.txt`: option `GT4_RELEASE_DISTRIBUTION` (default OFF). ON sets `GT4_VULKAN_SHADER_DIR` to an empty
  string and copies the nine shaders of the pinned capsule into `<build>\shaders` after `gt4vulkan` links.
- `native/install_paths.{hpp,cpp}`: with an empty build-time folder the answer is always `<exe folder>\shaders`;
  `--portable-check` prints `build-time shader fallback: present (development build)` or `none (release build)`.
- `native/native_vulkan.cpp`: a missing shader folder stops Vulkan start with "The game's shader folder is missing
  (...). Reinstall GT4Recomp." (windowed runs print it as `[host] audio/input/window failure: ...`; a headless run
  ends without the text, as for every other Vulkan start error).
- CPU level: unchanged. `GT4_X86_64_V3` stays OFF. A disassembly of slice211 (llvm-objdump) finds 346 SSE4/AVX
  instructions, all in one 12 KB region of CPU-dispatched Visual C++ library code (`cpuid` x5, `xgetbv` x1), and no
  BMI1/BMI2 instruction. The executable runs on baseline x86-64; the launcher checks SSE4.1 (owner, slice214 round), so the
  game has no CPU check of its own (Confirmed for slice211 and the same recipe in slice213).

Checks (2026-10-21):
- String scan of `gt4vulkan.exe` (ASCII and UTF-16 runs of 5+ characters; drive paths, `Antigravity`, `Alano`,
  `gt4-wt`, `Users\`, `staticrecomp`, `private\slice`, `.cargo`, `AppData`): no match. slice211 had exactly one
  (`C:/Antigravity/gt4-staticrecomp/private/slice153-pacing-shaders`).
- CTest: `private/slice213-scripts/ctest-213.log`.
- Python `unittest discover` with `GT4_NATIVE_BUILD` = slice213: `private/slice213-scripts/python-213.log`.
- Headless, 4,200 frames, the slice211 arguments (12 presses, frame dump every 100, `--trace-syscalls`,
  `GT4_MTVU_REPORT_EVERY=500`), a copy of the widescreen seed card, `GT4_FORCE_WIDESCREEN` unset:
  42 of 42 frame dumps identical to slice211 runs (a), (a2) and (b) (`private/slice213-smoke/h213-seed-unset`,
  `private/slice213-scripts/sha-213.txt`). No `no translated code` line. The card copy is unchanged. The log shows
  `[vulkan] shader folder: ...\slice213-build\build\shaders` (the build-tree copy).
- Without `shaders\` (scratch copy of the distribution folder): `--portable-check` says `missing; a release build has
  no build-time fallback` and exits 1; the game start stops at Vulkan start.
- Distribution folder `private/slice213-dist` (`scripts/make_distribution.py`, 18 files, 97 MB): `--portable-check`
  `result: ok`, `build-time shader fallback: none (release build)`.
- Pins: `docs/inputs/slice213-pcsx2.json` (mode `pcsx2`; `pcsx2-slice211` kept as history). Every other pin is
  unchanged.

Owner command: `python scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode pcsx2`.
The installer (TASK-081) packs `private/slice213-dist` as `game\`.

## Slice 214 - TASK-081 round 2: Vulkan baseline path for older drivers (`--mode pcsx2`)

Release blocker: slice213 required `VK_KHR_get_surface_capabilities2` and `VK_KHR_surface_maintenance1` (instance) and
`VK_KHR_swapchain_maintenance1` (device), which only 2025+ drivers have. slice214 = the slice213 recipe
(`private/slice214-scripts/build-game-214.bat`) plus:
- `native/vulkan_extensions.{hpp,cpp}` (pure): required = `VK_KHR_surface`, `VK_KHR_win32_surface` (instance) and
  `VK_KHR_swapchain` (device). Swapchain maintenance 1 is enabled only when present: the KHR family (2025+), else the
  EXT family (2023+, same structures and values), each with `VK_KHR_get_surface_capabilities2` and its own
  `surface_maintenance1`, and only when the device reports the `swapchainMaintenance1` feature.
  `GT4_VULKAN_FORCE_BASELINE=1` forces the plain path; any value other than 0 or 1 refuses the start.
- `native/native_vulkan.cpp`: enumerates instance and device extensions; the feature query chains the maintenance
  structure only when offered; timeline semaphores (core 1.2) stay required. Baseline path: a slot waits on the GS
  timeline value of its last presenting submission instead of a present fence; each swapchain image has its own present
  semaphore (recreated with the swapchain); `vkDeviceWaitIdle` before a swapchain rebuild and at shutdown. The startup
  log says `[vulkan] presentation path: khr-maintenance1 | ext-maintenance1 | baseline (...)`.
- CTest: `native_vulkan_extensions` (selection table) and `native_vulkan_pixels_baseline` (the pixel fixture, including
  Win32 presentation to a hidden window with repeat, zero size and resize, on the forced baseline path).

Executable `private/slice214-build/build/gt4vulkan.exe`, 97,197,568 bytes, sha256
`77909fa6c9f6ea832c204136b118a8da559718ec72a231d3d1601beb60e36150`. String scan: no build path.

Checks (2026-10-21):
- CTest 38/38 (`private/slice214-scripts/ctest-214.log`); Python 596 OK, 24 skipped (`python-214.log`).
- `vulkan_fixture` under `VK_LAYER_KHRONOS_validation` (Vulkan SDK 1.4.341.1), both paths: no validation message.
- Headless, the slice213 arguments on a copy of the widescreen seed card: default path (`khr-maintenance1` on this AMD
  driver) 42/42 and forced baseline 42/42 frame dumps identical to slice213 (`private/slice214-smoke/h214-default`,
  `h214-baseline`). Headless runs never present, so the baseline difference is limited to presentation, which the
  hidden-window CTest and the owner check.
- Pins: `docs/inputs/slice214-pcsx2.json` (mode `pcsx2`; `pcsx2-slice213` kept). Distribution `private/slice214-dist`.
- Launcher (same round): the CPU minimum is SSE4.1 with SSE3/SSSE3 (owner decision), and the Vulkan check requires only
  `VK_KHR_surface` and `VK_KHR_win32_surface`.

## Slice 216 - TASK-027 CoreGS 2.0 development build (`task/027-coregs2`, no pins)

Check build of the merge that resumes TASK-027 (worktree `C:\Antigravity\gt4-wt-027-coregs2`): `main` 1.0.0 plus the
TASK-027 work, without paraLLEl-GS. Not offered to the owner; no pin, no launcher mode, `--mode pcsx2` unchanged.
Recipe = slice214 (`private/slice216-scripts/build-game-216.bat`) plus `-DGT4_HW_GS=ON` and
`-DCMAKE_C_COMPILER=clang-cl` (the HWR `replace/` library compiles C). Vulkan SDK 1.4.341.1 (`VULKAN_SDK`), pinned
glslangValidator 16.2.0.

- `GT4_HW_GS=ON` builds `native/hw_gs` (backend, options, smoke, shaders, cpuref with memory/clut/frontend/pixel/
  display, cache, decide, lod, replace) and links the in-process sink into `gt4_gs_stream_client`
  (`GT4_GS_STREAM_HW`): `GT4_RENDERER=hw` then feeds the GS stream hooks to the HWR backend (no GS process, no donor
  Vulkan renderer); `GT4_GS_CAPTURE` is refused in that mode. With the option OFF the client refuses `hw` as before.
- Executable `private/slice216-coregs2-build/build/gt4vulkan.exe`, 97,291,264 bytes, sha256 `c89ba26d7ae334c673165823df35276f40ca4e5cd7996b47935a8a95af17cd81`.

Checks (2026-10-21):
- CTest 179 tests: 176 passed, 1 skipped (`hw_replace_q07_pcsx2_dumps`, pending Q-07), 2 failed
  (`private/slice216-scripts/ctest-216.log`):
  - `hw_gpu_pixel_directed`: 8 of 1,048,576 TEXA sweep cases differ (GPU alpha 0, CPU 0x20/0x80). The CPU pixel
    reference took the IN-07 AEM rule (A bit first) in 2434e1d after the GPU shader was verified; the shader still has
    the old rule. Pre-existing on `task/027-hw-renderer` (High confidence, from the commit history; not rerun there).
  - `python_native_contracts`: one error, `test_make_distribution` refuses the executable because it differs from the
    slice214 release pin. Expected for an unpinned development build.
- Python with `GT4_NATIVE_BUILD` = slice216: 627 run, 44 skipped, the same single error (`python-216.log`).
- Headless, the slice214 arguments on a copy of the widescreen seed card (`run-headless-216.sh`):
  - default renderer (`GT4_RENDERER` unset): exit 0, 42/42 frame dumps identical to slice214 `h214-default`
    (`private/slice216-smoke/h216-default`, `sha-h216-default.txt`); no `no translated code`; card copy unchanged.
  - `GT4_RENDERER=hw`: exit 0 after 4,200 frames; `[hw] session: vsyncs=4200 frames=4200 gif_bytes=2546704448
    privileged=20987 unmirrored_privileged=0 faults=0`; 0 frame dumps (the donor display path is off in a non-donor
    mode; HWR frame dumps are M6 work) (`private/slice216-smoke/h216-hw`).

## Slice 217 - TASK-027 GPU TEXA/AEM warm-up (07.5, IN-07)

Fresh full build using the slice216 recipe and unchanged asset pins, with the shader following the resolved IN-07 A-bit priority rule. To honor the owner's instruction to write only inside the worktree, build output is `C:\Antigravity\gt4-wt-027-coregs2\build\slice217-coregs2\build`; recipe and build log are in its parent. No main-checkout output was created. No launcher mode or distribution pin.

- Confirmed: full build passed with clang-cl and pinned glslangValidator 16.2.0 / Vulkan SDK 1.4.341.1.
- Executable: 97,291,264 bytes, SHA-256 `3fa40ea106b09816b52f1b0da5a7c7f3350f97ce77e953f5fce91e25fcd5b458`.
- Confirmed: `ctest -R "^hw_gpu_" --output-on-failure`: 3/3 passed, including directed TEXA and random pixel fixtures (8.59 s). Full suite and default-renderer headless check are recorded with the next replay checkpoint.

## Slice 219 - TASK-027 native v2 CPU replay (02, 10.2, 14.3)

Standalone composed CPU libraries, fresh `build/slice219-cpuref` inside the worktree, Release/clang-cl in VS x64. Configure `cmake -S native/hw_gs/cpuref -B build/slice219-cpuref -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe"`; build with `--parallel 4`, then serial CTest. No main-checkout output, candidate pin or launcher-mode change.

- Confirmed: 69/69 tests pass (3.36 s), including `cpuref_capture_v2_parity`, `_resets`, `_fifo`, `_malformed` and all existing CPU/memory/front-end/pixel/display fixtures. Initial slice218 fixtures exposed the separate A+D kick gap (IN-326); the final fixtures use a real REGLIST triangle and check cleared reset output against the specified background behavior.
- `hw_cpuref_replay.exe` SHA-256 `d7529881d1195dad64ea00d3ba16d3415ac09ec9e69b95bace3f4ddf7dc47f5b`. Logs/recipes: worktree `build/slice217-coregs2/`.
- Slice217 full native CTest (Python excluded and run separately): 178 selected, 177 passed, Q-07 skipped, zero failed (184.53 s). Python with `GT4_NATIVE_BUILD` pointing at the slice217 full game build: 627 tests, 44 skips, one expected pre-existing `test_make_distribution` error (unpublished development exe does not match the slice214 pin); no check was relaxed. Replay/frontend changes are confined to pure HWR libraries; those are rebuilt and retested in slice219. The game backend still does not consume them.
- Fresh donor headless from the slice217 full game: 4,200 frames, 12 presses, 37.0 s, exit0, no Win32 presentations, copied seed card unchanged; 42/42 dump hashes equal slice214. Evidence: worktree `private/slice217-coregs2-evidence/`, `default-parity.json`, `headless-report.json`. Existing scripts/outputs/cards in the main checkout were not changed.
- Fresh `GT4_GS_CAPTURE_FRAMES=0:1100` capture `menu-v2.gscap`: 530,261,664 bytes; SHA-256 `7abb51cfeb610fe71fd4855937dea88b6e691bb281895ad3e3abd849387a74a7`. Replay: 488,557 records, 3 resets, 481,961 GIF packets / 514,627,776 GIF bytes, 5,492 privileged writes, 1,101 frames/images, zero rejected GIF/skipped textures/FIFO faults.
- Original legacy capture SHA-256 `c72a2f0c52dd6b529a0ee804f78608ae19be5b2e90a4430b81f27503a634a0ee`; same-record envelope conversion to v2 SHA-256 `ba5c301ddfdb5e47c962fb3748af314ffaf4d11b34cf42dcd4754c5412085345`. Both replay 488,553 records / 1,101 frames. **All 1,101 PPMs are byte-identical across legacy, converted v2 and fresh donor v2**, including every frame ID0..1100. Replays took59.85/59.81/60.81 s. Evidence: worktree `private/slice218-replay-evidence/`, `replay-results.json`, `fresh-legacy-parity.json` and replay logs.
- Menu comparison rerun: `fresh-menu-comparison-diagnostic.json` records 11 donor frames100..1100 against capture frames99..1099. It is explicitly diagnostic only: the CPU output is composed and the donor output is VRAM-decoded (IN-325). Ten samples have pct_bad <=0.038%; garage frame900 remains29.5487%, PSNR8.7427 dB. No chapter14 qualification or IN-315/IN-316 closure is claimed.
- S-01/S-02 are covered. S-03..S-11 are owner gameplay scenes (chapter14.5) and were not recorded or claimed here. No windowed game was launched.

## Local slice221 - CPU VRAM diagnostic output (12.9,14.2-14.4)

Fresh standalone build inside the CoreGS worktree: `build/slice221-cpuref`. Local build names are not main
checkout slice reservations or release pins. Main checkout remains read-only. Configure/build/test in VS x64:

```text
cmake -S native/hw_gs/cpuref -B build/slice221-cpuref -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe"
cmake --build build/slice221-cpuref --parallel 4
ctest --test-dir build/slice221-cpuref --output-on-failure
```

- Confirmed:75/75 pass (3.82s), including6 new `cpuref_vram_dump_*` fixtures. Initial local220 had one incorrect
  NTSC test geometry;221 corrects the fixture magnification, leaving the diagnostic implementation unchanged.
- CLI: `hw_cpuref_replay <capture> <out-dir> [--dump-source vram|scanout]`. Default vram, explicit scanout
  preserves prior output; EndFrame/ReplayCapture API defaults stay unchanged. `frames.jsonl` contains all IDs,
  no-image reasons and lossless raw register metadata. No automatic renumbering or image shifts.
- Binary SHA-256 `5a411d1887bb5439c8ce3c2a98fb73c77636a737fa68b45aa35a0953d6eb8f21`.
- Python command from worktree, with `GT4_NATIVE_BUILD`=full-game local217 and `GT4_HW_REPLAY_EXE`=local221
  replay executable: `python -m unittest discover -s tests/python`:629 run,44 skipped, only the expected
  pre-existing distribution-pin error. The2 new external CLI tests ran, including metadata and invalid options.
- Reused fresh donor capture from local217 (`menu-v2.gscap`, SHA7abb51cf...; full digest in slice219 section).
  Raw explicit/default replays take65.62/64.40s, each1,096 PPMs and1,101 metadata rows. All PPMs identical.
  Five omitted images are precisely frames0..4 with no enabled circuit. Explicit scanout takes63.62s and all
  1,101 PPMs equal prior local219 output. Exit0 for all; no unsupported PSM/dimension refusals in real menu data.
- Evidence: `private/slice221-vram-evidence/validation.json`, metadata/replay logs,
  `frame-mapping.json`, `menu-vram-comparison.json`, `vertical-pairing.json`. Harness/build/Python logs:
  `build/coregs2-next-validation/`. Aligned sample folders rename copied files only, preserving pixel bytes
  and recording their original capture IDs/hashes. Donor frameN+1 corresponds to capture frameN.
- Valid VRAM-to-VRAM2D report now has10/11 samples under the thresholds; donor900/capture899 still29.5487%
  bad, PSNR8.7427dB. Correctly paired800/1000/1100 need no vertical shift (see IN-315/325/327). No entire-scene
  parity, real scanout phase, GPU write-back or playable HWR is claimed. No game launch was needed.

## Local slice222 - A+D XYZ kicks (01.1, 02.1, 02.3, IN-326)

Standalone composed CPU libraries again, fresh `build/slice222-cpuref` inside the worktree, same recipe as
slice221. No main-checkout output, no candidate pin and no launcher-mode change.

```text
cmake -S native/hw_gs/cpuref -B build/slice222-cpuref -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe"
cmake --build build/slice222-cpuref --parallel 4
ctest --test-dir build/slice222-cpuref --output-on-failure
```

- Confirmed: 76/76 tests pass (4.31s), including the new `m3_hw_gif_ad_xyz`. PACKED descriptor 0xE now decodes
  XYZF2/XYZ2/XYZF3/XYZ3 as vertex kicks in the canonical register layout; bits 72..127 of the A+D qword, bit 111
  ADC included, are ignored (01.1/02.1). REGLIST and PACKED descriptor behaviour is unchanged.
- Confirmed cause of IN-316: with the same donor v2 menu capture (`menu-v2.gscap`, SHA-7abb51cf...) exactly
  frames 811..916 change. 990/1,096 VRAM images and 995/1,101 scanout PPMs remain byte-identical to slice221.
- Confirmed: aligned donor samples (capture N = donor N+1) now 11/11 inside the chapter14.4 2D thresholds; the
  garage sample donor900/capture899 went from pct_bad29.5487%/PSNR8.7427dB to0.2051%/45.992dB. Report only:
  full scene qualification is M8.
- Confirmed: the game executable is byte-identical to slice217 (97,291,264 bytes, SHA-3fa40ea106b0...). The HWR
  front end is linked only into the CPU reference, not into `gt4_hw_stream_sink`, so the default renderer path
  and the recorded 42/42 headless parity keep holding. Slice217 full native CTest after the change: 188 passed,
  Q-07 skipped, only the expected `python_native_contracts` distribution-pin failure. Python: 629 tests, 44
  skips, same single expected error.
- Evidence: `private/slice222-in326-evidence/` (`vram`, `scanout`, `aligned-candidate`,
  `menu-vram-comparison.json`, logs). No game launch was needed; no windowed run.
