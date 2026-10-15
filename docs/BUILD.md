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

    private	ooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root C:\Antigravity\gt4-staticrecomp --mode quickfix

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
cmake -S . -B private\slice168-audio-builduild -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DGT4_EE_PROFILE=production ^
  -DGT4_ASSETS_ROOT=C:/Antigravity/gt4-staticrecomp
cmake --build private\slice168-audio-builduild --target host_io_fixture
ctest --test-dir private\slice168-audio-builduild -R native_audio_input --output-on-failure
```

## Slice 176 - internal-resolution upscaling (TASK-028)

`PS2_SCALE` = 1 to 4 renders each GS pixel as SCALE x SCALE sub-samples in the donor Vulkan compute
renderer (CoreGS 1.x). Scale 1 is the default and is the qualified output: 42 of 42 headless frame dumps
are byte-identical to `private/slice157-smoke/headless-157/frames`. Scales 2 and 3 complete the same
4,200-frame schedule with exit 0.

Why it was off: the donor scales the GS VRAM, copy and field buffers and folds `SCALE` into the SPIR-V,
but the native adapter (`native/gpu_init.inc`) rejected any value except 1, and its readback, display and
checkpoint paths assumed 1. The changes are: scaled buffer sizes and tile grid; per-scale shader
directories (`tools/wgsl_spirv <gs.wgsl> <out> <scale>`; scale 1 is byte-identical to slice153);
`download` and `read_state`/`write_state` resolve or replicate sub-samples; display and present dispatches
cover `width x SCALE` by `height x SCALE`; the swapchain blit uses a linear filter when scaled
(`GT4_PRESENT_FILTER=nearest|linear` overrides).

Frame dumps: `display()` returns the 1x image, each pixel the rounded average of its SCALE x SCALE
samples. `GT4_DUMP_NATIVE_SIZE=1` returns the scaled image instead.

Steps (fresh private directories in the main checkout; run from the worktree root):

1. Shader tool: `cargo build --release --manifest-path tools\wgsl_spirv\Cargo.toml --target-dir C:\Antigravity\gt4-staticrecomp\private\slice176-wgsl-target`.
   Shaders: `gt4-wgsl-spirv.exe <donor>\gs.wgsl C:\...\private\slice176-donor-upscale-shaders 1`, then the same with
   `...-shaders-s2`, `-s3`, `-s4` and scale 2, 3, 4.
2. Port: `python scripts\prepare_external_vulkan.py C:\Antigravity\gt4-staticrecomp\private\slice176-donor-upscale-port --assets-root C:\Antigravity\gt4-staticrecomp`.
3. Configure `private\slice176-donor-upscale-builduild` with `GT4_ASSETS_ROOT`, `GT4_PERF_RUNTIME_SOURCE`
   = `slice160-vu0-runtime`, `GT4_VU1_GENERATED_DIR` = `slice157-vu1-all-gen`, `GT4_VU0_GENERATED_DIR` =
   `slice160-vu0-gen-r3`, `GT4_PORT_DIR` and `GT4_SHADER_DIR` = the slice176 directories (all absolute, main checkout).
   Pins: [slice176-donor-upscale-runtime.json](inputs/slice176-donor-upscale-runtime.json) (capsule inputs) and
   [slice176-donor-upscale.json](inputs/slice176-donor-upscale.json) (launcher run manifest, runtime and scale shaders).
4. Controls: `ctest --test-dir ... `: 23 tests of slice 166 plus `native_upscale_scale1..4` (VRAM
   write_state/download/read_state round trip, integer-aligned sprite counts in VRAM and in the 1x frame,
   and presentation at each scale). Python: `tests/python` includes `test_compare_frame_dumps.py` and the scale gate tests.
5. Headless (no window), the slice157 schedule, PS2_SCALE set in the environment; the arguments are those the
   probe builds (`--ntsc --disc <iso> --frames 4200 --memcard <card> --misses <file> --frames-dir <dir>
   --frame-every 100 --trace-syscalls` plus the 12 `--press` entries).
6. Compare: `python scripts\compare_frame_dumps.py <ref-frames> <candidate-frames> --scale N --json <out>`.

Results (Confirmed, local headless; not owner gameplay; no performance claim):

| Run | Dumps | Notes |
|---|---|---|
| scale 1 vs slice157 | 42 of 42 identical | psnr-1x-vs-157.json |
| scale 2 vs scale 1 | exit 0; mean 30.9 dB, min 26.1 dB | identical only on one frame |
| scale 3 vs scale 1 | exit 0; mean 29.8 dB, min 25.0 dB | native-size dumps give the same PSNR |

The PSNR is a distance from the 1x image, not a quality score: the scaled image has finer edges and text
(the 3x native frame `private/slice176-donor-upscale-smoke/png/3x-native-frame_02100.png` against
`1x-frame_02100.png`). Guest traces (system calls, VIF, GIF, counters) match scale 1 except the
`card`/path lines and the `[vulkan]` counter lines. The `decode` dispatch count drops from 192,307 to 32,396 at
scale 2 and 3 (donor `rendered` guard, active only when scale > 1); `resolve` is 0 in the headless schedule
because no guest readback runs there, so the readback path is covered only by the `upscale_fixture` control.

Owner commands (PowerShell, from the main checkout after task/028 is merged; the launcher reads its runtime and
pins from the checkout it runs in):

```powershell
C:\Antigravity\gt4-staticrecomp\private	ooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root . --mode upscale --scale 2 --force-480p
C:\Antigravity\gt4-staticrecomp\private	ooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root . --mode upscale --scale 3 --force-480p
```

Limits:

1. Save states across scales are converted by word replication; not exercised in the headless schedule.
2. Window presentation at 2x and 3x is accepted by the control (`check_presentation`), not viewed in the owner's window.
3. A scaled frame's `decode` skip (donor `rendered` guard) changes texture decoding; not separately validated against the 1x path beyond the image comparison.
4. No fullscreen, FSR, FXAA or deinterlace: see the CoreGS 1.1 workstream entry in the journal.

## Slice 182 - CoreGS 1.1 present pass (TASK-028, workstream W2)

CoreGS 1.1 is the donor Vulkan compute GS renderer (CoreGS 1.x lineage) with an internal-resolution
scale of 1 to 4 (slice 176), and a present-path post pass for the final image (this slice): deinterlace,
FXAA-class anti-aliasing and AMD FidelityFX FSR 1.0 scaling to the 16:9 window rectangle. Scale 1 and the
defaults keep the headless frames: 42 of 42 dumps identical to `private/slice157-smoke/headless-157/frames`.

Options (all default to the game's own choice; set only through the launcher or the environment):

| Environment | Values | Default | Meaning |
|---|---|---|---|
| `PS2_SCALE` | 1, 2, 3, 4 | 1 | internal resolution: GS pixel = SCALE x SCALE sub-samples |
| `GT4_DEINTERLACE` | auto, off, weave, bob, blend, adaptive | auto | runs only when the frame is really interlaced (donor halved read); auto = adaptive then, off for progressive output (480p untouched) |
| `GT4_AA` | off, fxaa | off | edge-only FXAA-class pass before scaling |
| `GT4_PRESENT_FILTER` | nearest, linear, fsr | automatic | final scaling to the window: automatic is nearest at scale 1 and linear when scaled; fsr = FSR 1.0 EASU then RCAS to the 16:9 rectangle (the blit is then 1:1) |
| `GT4_FSR_SHARPNESS` | 0 to 2 (stops) | 0.2 | RCAS sharpness; 0 is the sharpest |
| `GT4_DUMP_NATIVE_SIZE` | 1 | unset | headless frame dumps at the scaled size (default: 1x box average) |

Launcher (after task/028 is merged, from the main checkout; `--mode upscale` only):

```powershell
C:\Antigravity\gt4-staticrecomp\private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root . --mode upscale --scale 2 --force-480p
C:\Antigravity\gt4-staticrecomp\private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root . --mode upscale --scale 3 --force-480p
C:\Antigravity\gt4-staticrecomp\private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root . --mode upscale --scale 3 --force-480p --present-filter fsr --aa fxaa --deinterlace auto --fsr-sharpness 0.2
```

Build (fresh directories, main checkout private/; run from the worktree root):

1. Shader tool: `cargo build --release --manifest-path tools\wgsl_spirv\Cargo.toml --target-dir C:\Antigravity\gt4-staticrecomp\private\slice176-wgsl-target`.
2. Donor shaders for each scale (`gs.wgsl`, entries upload..decode, scale N) and the post pass
   (`native\post\present.wgsl`, entry `post`, arguments `<shader> <dir> 1 post`) into
   `private\slice182-coregs11-shaders` (scale 1) and `-s2`, `-s3`, `-s4`.
3. Port: `python scripts\prepare_external_vulkan.py C:\Antigravity\gt4-staticrecomp\private\slice182-coregs11-port --assets-root C:\Antigravity\gt4-staticrecomp`.
4. Pins: `docs/inputs/slice182-coregs11-runtime.json` (PORT and SHADERS sha256 of steps 2 and 3, checked by CMake).
   Recompute them whenever the port or a shader changes; the launcher run manifest
   `docs/inputs/slice182-coregs11.json` pins the executable and the scale shaders.
5. Configure `private\slice182-coregs11-build\build` as slice 176 but with the slice182 port and shader directories.
6. Controls: `ctest --test-dir private\slice182-coregs11-build\build --output-on-failure`: 28 of 28, including
   `native_post_passes` (deinterlace weave/bob/blend/adaptive exact against the CPU reference over 8 interlaced
   frames; automatic = adaptive; progressive untouched; FXAA within one level; FSR EASU+RCAS within one level at
   32x24, 48x36 and 40x30) and `native_upscale_scale1..4`.

Results (Confirmed locally; no owner window check; no performance claim):

- Scale 1 headless: 42 of 42 identical to slice157. Scale 2 and 3: exit 0, 42 dumps each.
- `post_fixture` and the Python suite (264 tests, 12 skipped as before) pass.
- Samples from the headless frame 2100 (garage, scale 1, 640x448), in `private/slice182-coregs11-samples`:
  `none.png`, `fxaa.png`, `adaptive-interlaced.png` (640x448) and `fsr-960x540.png` (FSR 1.0 to the 960x540 window).

Limits:

1. The field parity is taken from the presentation count (no VBlank field from the donor), so adaptive's
   field alternation follows presents; the spec's VBlank field is not yet wired into the present path.
2. The donor's halved display read repeats each field line on both rows of a pair; weave and adaptive therefore
   operate on that frame, and their gain on real interlaced races is not measured.
3. FSR's RCAS runs on the 8-bit EASU output; the header's float path is not used between the two stages.
4. FSR output must fit the scratch buffer (`g_copy_words` pixels, 4,194,304 at scale 1): windows up to about
   2560x1600 with the default buffers. Larger windows raise an error at present.
5. FSR and FXAA are not validated against AMD's compiled header or NVIDIA's FXAA binary; the CPU references are
   transcriptions (see `native/THIRD_PARTY.md`).
6. Save states across scales, owner gameplay and window quality are not checked by these controls.

## Slice 184 - window at the scaled size, FSR sharpness as a percentage (TASK-028)

Owner report on slice 182: at `--mode upscale --scale 3` the window looked like a blurry upscale of 640x448. The
window was fixed at 960x540, so the 1920x1344 scaled image was downsampled before presentation.

Changes (Confirmed by build and CTest; owner window check pending):

1. Window size (`native/window.cpp`, `native/window_geometry.hpp`). Scale 1 keeps the 960x540 client and CW_USEDEFAULT.
   Scales 2 to 4 use 1593x896, 2389x1344, 3185x1792 (16:9 at 448 x scale lines). If the outer window (client plus
   AdjustWindowRectEx frame) does not fit the work area (rcWork) of the monitor under the cursor, the client becomes the
   largest 16:9 that fits (width = floor(16 x height / 9)). The window is centred in the work area. Alt+Enter and F11
   restore the saved windowed rectangle, so they return to this size.
2. One scale parser (`native/coregs_options.hpp`). `PS2_SCALE` is read by the same `parse_scale` in the window host and
   in the GS adapter (`gpu_init.inc`). The window reads it at open, before `init_graphics`, so it cannot use the
   adapter's value; both call one function.
3. FSR sharpness as a percentage. `GT4_FSR_SHARPNESS` is 0 (softest) to 100 (sharpest); stops = 2 x (1 - percent / 100):
   100 gives 0 stops, 50 gives 1.0 (default when unset), 0 gives 2.0. Values outside 0 to 100 are refused with
   `GT4_FSR_SHARPNESS must be a percentage from 0 to 100`. `play_performance.py --fsr-sharpness` takes 0 to 100.
   FSR still runs from the scaled internal image (640x448 x SCALE) to the 16:9 rectangle.
4. The donor shaders are unchanged; `scripts/prepare_external_vulkan.py` adds `#include "coregs_options.hpp"` to the spliced
   `gpu.cpp`. Pins: `docs/inputs/slice184-coregs11-runtime.json` and the launcher manifest
   `docs/inputs/slice184-coregs11.json` (executable sha256 ce047ad4...). `UPSCALE_MANIFEST` now names the slice 184 pin.

Build (fresh directories in the main checkout `private/`; the worktree has copies of every pinned input):

- Port: `python scripts\prepare_external_vulkan.py C:\Antigravity\gt4-staticrecomp\private\slice184-coregs11-port --assets-root C:\Antigravity\gt4-staticrecomp`
- Shaders: `private\slice184-coregs11-shaders` and `-s2`, `-s3`, `-s4` are byte copies of the slice 182 SPIR-V
  (the donor `gs.wgsl` and `native/post/present.wgsl` are unchanged; tools/wgsl_spirv was not rerun).
- Configure `private\slice184-coregs11-build\build` with the slice182 variables, source = this worktree; CMake recognises
  the slice184 port and shader paths (native/CMakeLists.txt).

Controls (Confirmed locally):

- CTest 29 of 29 on the slice184 build, including `native_window_geometry` (scales 1 to 4 on a 1920x1080 and a 4K
  work area; clamps by height and by width; 16:9 within one pixel; centring; `parse_scale` refusals) and `native_post_passes`
  with the FSR percentage mapping (100, 50, 0, unset, refusals).
- Python `unittest discover` with `GT4_NATIVE_BUILD` on the slice184 build: 264 tests, 12 skipped (as before).
- Headless, 4,200 frames, the slice157 schedule, `PS2_SCALE` 1: 42 of 42 dumps byte-identical to
  `private/slice157-smoke/headless-157/frames`.
- Headless, scale 3, `GT4_DUMP_NATIVE_SIZE=1`: 42 dumps at 1920x1344, byte-identical to the slice182 scale 3 native dumps.

3x detail check (the lead in the owner report). Confirmed for the headless schedule: the lead is not the cause here.

| Run (slice182 binary unless noted) | Result |
|---|---|
| 3x native, decode on vs `PS2_DECODE=0` | 42 of 42 dumps byte-identical (the 3x decode dispatch count 32,396 changes nothing in these frames) |
| 3x native, 3x3 blocks with unequal samples | frames 100 and 700: 2.2% and 0.4% (flat menus); frames 1300 to 3700 (3D): 55.5% |
| slice184 3x native vs slice182 3x native | 42 of 42 identical |

The per-draw count of textured draws that read a GS-rendered page (decoded, resolve or direct path) was not
instrumented; the A/B above answers the question for the headless schedule. Samples (nearest-neighbour enlarged 1x beside
3x native, same region): `private/slice184-upscale-samples/frame_01300-crop-center-1x-vs-3x.png`. The region is flat and
low contrast; the 3D frames carry detail that this crop does not show. The owner's window is the next check.

Owner command (PowerShell, from the worktree; its `private/` holds copies of the pinned inputs):

```powershell
python scripts\play_performance.py --assets-root . --mode upscale --scale 3 --force-480p --present-filter fsr --aa fxaa --deinterlace auto --fsr-sharpness 50
```

Limits:

1. Not verified in a window: the 2x, 3x and 4x window sizes and the centring on the owner's monitor, and the look of FSR at
   percent 50 versus the slice 182 default of 0.2 stops (now 1.0 stop).
2. The 2x, 3x and 4x sizes come from the owner's table; 2x and 4x are not exact 16:9 (within one pixel).
3. The lead's per-draw path counts are not measured; the decode-on/off identity is for the headless schedule only.
4. Save states and fullscreen at the scaled size are not exercised.

## Slice 186 - owner capture and sprite sub-sample switches (TASK-028)

Owner report on slice 184 (race, progressive 480p, `PS2_SCALE=3`): the image is sharper than 1x, but tree silhouettes,
guardrails and the car body still show stair steps about one native PS2 pixel wide; PCSX2 at 3x looks cleaner.

Correction to slice 184: the "55% of 3x3 blocks unequal in frames 1300 to 3700" figure is wrong. Those frames are the garage
menu, not a race (the slice157 headless schedule never reaches 3D: the frame mean colour is constant from about frame 1000 to
4200). The block metric also counts sub-sample detail, not steps: the menu text at 3x is smooth (see the crops below).

### What the code says (Confirmed, donor `gs.wgsl` and `gpu.cpp`, read in this slice)

- Triangles: XY keeps its 1/16 fraction (`sx = (x - ofx) / 16`, no snap in setup). Coverage and attributes are evaluated
  at the sub-sample positions `px, py`. Triangle polygon edges are therefore sub-sample accurate at scale > 1.
- Sprites (kind 1): coverage tests the integer pixel `(cx, cy)`. Texel coordinates snap to `f32(x)` for point-sampled
  textures when the texel rate is at least the pixel rate. A 1:1 texel gives the same texel index with or without the
  snap, so the snap matters for minified sprites. Coverage at the integer pixel cuts fractional sprite edges to native size.
- Lines (kind 2): coverage is per integer pixel, so every sub-sample of a line pixel takes the line colour.
- Textures: `fetch` reads texel `floor(u)` and sub-sample `k` of that texel. A 1:1 point-sampled texture therefore has
  native-size texel steps, whatever the triangle edge resolution (texel content, not coverage).
- Decoded textures: `decode` reads sub-sample 0 only. The donor skips decode for pages rendered at scale > 1 (`rendered`
  guard in plan_decodes). A page written by the same batch can still be decoded stale.
- Display and present read every sub-sample (no native-size step there).

### Measured (Confirmed, headless, slice157 schedule, PS2_SCALE 3, GT4_GS_PRIM_STATS=1; menus only, no race)

| Quantity | Value |
|---|---|
| Batches / primitives | 5,114 / 1,113,131 (exit totals) |
| Triangles / sprites / lines / points | 1,054,914 (94.8%) / 58,217 (5.2%) / 0 / 0 |
| Textured / untextured | 994,035 / 119,096 |
| Alpha test / bilinear | 7,600 / 1,087,789 |
| Texture format (tpsm) | 0x14 (4-bit CLUT) 898,213; 0x13 (8-bit CLUT) 81,880; 0x01 (24-bit) 7,600; 0x00 (32-bit) 6,342 |
| Decoded / direct textured | 124,760 / 869,275 |
| Stale decode (decoded page written in the same batch) | 0 |
| Sprites snap-eligible texel rate (u / v) | 13,300 / 13,300 |

The race (trees, guardrails, car) is not measured: no race in headless. The owner's F10 capture is the next input.

### Evidence on the stepped edges (menus; the race is not measured)

- `private/slice186-aliasing-samples/crop186_photolab_icon_off_vs_fixed.png`: both panels are 3x native, nearest x4, switches
  off (left) and on (right). The rotated photo prints and the sky show native-size steps in both. The white print border is
  texture content, so these steps come from point-sampled 1:1 texels (Confirmed by crop; Hypothesis that this is the same
  class as the owner's trees and guardrails).
- `check_garage_title.png` (left: 1x nearest x12; right: 3x native x4): the text edges are smooth at 3x; no native steps in
  the triangle-drawn text.
- `f900_fixed_1x.png` and `f900_off_1x.png`: frame 900 (garage home menu) is the only dump that changes with the sprite
  switches: 342,854 of 2,580,480 pixels, mean absolute difference 1.09 per byte, max 109 (Confirmed, local). The change is
  in minified sprite sampling, not in the edge placement of the sprites shown in the crops.

### Changes

1. `native/gs_prim_stats.hpp` (new): statistics (`GT4_GS_PRIM_STATS=1`), the F8 line, exit totals, the per-frame trace
   (`GT4_GS_PRIM_STATS_TRACE=1`), the F10 capture writer (PNG, stored deflate, no library). `upscale_setup_bits(scale)` sets
   setup flag bits 16 (sprite sub-sample coverage) and 18 (no texel snap) only when the scale is above 1. Bits 16 and 18 do
   not overlap the donor flags (bits 0 to 15).
2. `native/gs_prim_stats_hook.inc` (new): read-only hook after `plan_decodes` in `flush_batch`; counts primitives, textured
   and decoded setups, sprite snap eligibility and stale decodes.
3. `native/gpu_tail.inc`: `write_capture` (F10) reads the presented frame at its scaled size and as a 1x box average.
   `present()` calls it when a capture was requested, then `frame_presented()`.
4. `native/window.cpp`: F10 (F7, F8, F9 and F11 unchanged) requests a capture; the F8 report prints the stats line first.
5. `scripts/prepare_external_vulkan.py`: anchored splices for the hook, the flag bits, the stats include and the sprite box
   floor (`sprite_box_min`). At scale 1 the box is `ceil` as before; `floor` applies only when the sub-sample bit is set.
6. `scripts/derive_upscale_gs_shader.py` (new): derives the slice 186 `gs.wgsl` from the pinned donor (sha256 `a6ed68e9...`,
   CRLF normalised). Changes: sprite coverage at `px, py` (bit 16) and the texel snap skipped (bit 18), both only at
   `SCALE > 1`. Derived shader: `private/slice186-gs-src/gs.wgsl`, sha256 recorded in the runtime pin.
7. `native/CMakeLists.txt`: slice186 port and shader branch; runtime pin path.
8. `scripts/external_runtime_probe.py`: `UPSCALE_MANIFEST` = `slice186-coregs11.json`; validates
   `GT4_GS_SPRITE_SUBSAMPLE`, `GT4_GS_SPRITE_SNAP`, `GT4_GS_PRIM_STATS` and `GT4_GS_PRIM_STATS_TRACE` (0 or 1); records the
   effective values in `result.json` under `gs_slice186_switches`.
9. Pins: `docs/inputs/slice186-coregs11-runtime.json` (PORT and SHADERS hashes, derived gs.wgsl hash, receipt of the
   unchanged vu0 runtime capsule) and `docs/inputs/slice186-coregs11.json` (executable sha256 16da396b...).

### Switches (owner A/B; the two sprite switches act only at PS2_SCALE > 1)

| Variable | Default at scale > 1 | Effect |
|---|---|---|
| `GT4_GS_SPRITE_SUBSAMPLE` | 1 | sprite coverage at sub-sample positions; 0 = integer pixel (slice 184) |
| `GT4_GS_SPRITE_SNAP` | 0 | 0 = no texel snap for sprites; 1 = snap to the integer pixel (slice 184) |
| `GT4_GS_PRIM_STATS` | 0 | counts and prints the primitive statistics (F8 line, exit totals) |
| `GT4_GS_PRIM_STATS_TRACE` | 0 | with stats on and no window, one line per `display()` call |
| `GT4_GS_LINE_SUBSAMPLE` | not implemented | see limits |

With `GT4_GS_PRIM_STATS` unset nothing is counted, and F10 does nothing until pressed. The statistics change no output.

### Controls

- Scale 1, headless slice157 schedule, final binary: 42 of 42 dumps byte-identical to `private/slice157-smoke/headless-157/frames`.
- Scale 3, `GT4_DUMP_NATIVE_SIZE=1`, switches off (`GT4_GS_SPRITE_SUBSAMPLE=0 GT4_GS_SPRITE_SNAP=1`): 42 of 42 dumps
  byte-identical to the slice 184 scale 3 native dumps. The switches are the only change in this slice's output.
- Scale 3, switches on (default): 41 of 42 identical to switches off; frame 900 differs (see above).
- CTest 29 of 29 on the slice186 build. Python `unittest discover` with `GT4_NATIVE_BUILD` on the slice186 build: 264 tests,
  10 skipped, OK.
- Pinned files copied into the worktree `private/` and checked against the pins (port, shaders, derived gs.wgsl, executable,
  runtime dependencies): no mismatches.

### Owner commands (PowerShell, from the worktree; its `private/` holds the pinned copies)

Default (fixes on):

```powershell
C:\Antigravity\gt4-staticrecomp\private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root . --mode upscale --scale 3 --force-480p --present-filter linear --aa off
```

All switches off (slice 184 image):

```powershell
$env:GT4_GS_SPRITE_SUBSAMPLE="0"; $env:GT4_GS_SPRITE_SNAP="1"; C:\Antigravity\gt4-staticrecomp\private\tooling-venv\Scripts\python.exe scripts\play_performance.py --assets-root . --mode upscale --scale 3 --force-480p --present-filter linear --aa off
```

Capture: with `GT4_GS_PRIM_STATS=1` in the environment, play a race scene with trees and guardrails, press F10 (writes
`private/manual-play/session-*/frame-NNNN-3x.png`, `frame-NNNN-1x.png` and `frame-NNNN-stats.txt`), then F8 for the report.

### Limits

1. Not verified: the F10 capture and the F8 stats line in a window (no windowed launch in this slice). The capture path
   compiles and reuses the display pass of the dumps; it is unexercised at runtime.
2. No race measured: the primitive mix above is menus only. The stepped race classes (trees, guardrails, car body) are
   unmeasured; the owner's F10 capture is the input for the next slice.
3. `GT4_GS_LINE_SUBSAMPLE` is not implemented: the measured schedule has no lines, and a line sub-sample path needs a
   per-sub-sample hit test that is not in the donor.
4. Texel point sampling is not changed. If the race capture confirms texel-size steps on trees and guardrails, the next
   candidate is bilinear sampling at the sub-sample rate for point-sampled 1:1 textures. That is a visible filtering change,
   so it needs its own switch and owner A/B; it is not in this slice.
5. The half-sample convention (sub-sample at `x + sub/SCALE`, not the centre) is not changed (Hypothesis: small bias).
6. Stale decode is counted per decoded setup and is 0 in the measured menus; the race may differ.
