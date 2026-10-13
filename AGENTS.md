# Persistent project instructions

## Read first

Read `docs/STATUS.md` first. Then `README.md`, `docs/ARCHITECTURE.md` and
`docs/BUILD.md` as needed. Current implementation lives in `native/`.
The old EE recompiler/runtime and its tests are archived on
`codex/archive-original-recomp-20261013`, commit
`95f615519b992c65505d77da35af825fc6a13a53`. Do not restore them into main
without a documented technical reason. See `docs/LEGACY.md`.

## Mission and current base

GT4Recomp targets pinned Gran Turismo4 USA v2.00 on Windows x86-64.
First make real gameplay functional and stable; then make game code readable,
modifiable and extensible. Learning and evidence quality remain important.
Working fork: github.com/moesuito/gt4-staticrecomp. The original upstream
github.com/Kaezzey/gt4-staticrecomp is read-only: no pushes or PRs there.

The active base is the MIT ps2-web-recomp donor at
1b4808793a12c45125262fd2d5f50a9ec9f7c020, adapted to native Windows/Vulkan.
EE game code is AOT C++/x86-64. IOP and current VU microprogram execution
remain interpreted; PS2 hardware/services are software models. No browser,
WebAssembly or embedded PCSX2. Never claim zero emulation or full PS2 fidelity.
No PS2Recomp GPL implementation is linked. Preserve third-party attribution.

The qualified native build uses Clang/clang-cl,C++20, VS2022 x64 SDK/ABI,
Ninja and CMake3.24+. The former MSVC engine is archival, not a second active
production target. Native internal scale remains1;16:9 viewport is not HD/4K.

## Owner priorities and current limits

Real menus/racing, native Vulkan, Windows audio and keyboard/XInput code exist.
Owner confirms480i/480p/1080i gameplay at16:9;480p subjectively most stable.
Physical XInput is untested. Keyboard directions reportedly double-trigger.
Low3D performance and inconsistent frametime are recorded; cause unknown.
Owner explicitly requested recording only: no input/performance fix or audit
until further instruction. The owner has now authorized performance investigation
and experimental builds delegated to OpenCode; Codex only prepares the handoff.
See docs/OPENCODE-PERFORMANCE-HANDOFF.md. Input fixes remain outside that task.
No Computer Use, GUI automation, simulated input or scripted game navigation:
the owner controls gameplay. Launching a tested candidate is authorized; preserve
their card/backups/locks and do not interfere with an existing session.

Future goals:144FPS/monitor refresh independent of physics at100% game speed,
progressive-only output, internal1080p/1440p/4K and setup launcher; later effects,
textures and native gameplay ultrawide with undistorted/repositioned HUD.
Do not raise the guest clock or repeat frames to claim these goals achieved.
Finished races, broad saves, audio continuity, known CPU/GPU divergence,
CFC2(28) and PS2 scan-mode fidelity remain qualification work.

Owner requested architecture planning after114: see docs/ROADMAP.md. It is a
proposal, not authorization to implement all stages or run performance probes.
Owner now authorized P0/P1: see docs/EE-PRODUCER.md and docs/RENDER-CONTRACTS.md.
The slice116 producer is separate from production and does not relax library pins.
Slice117 qualifies one explicit-seed callback recovery offline; see
docs/EE-CALLBACK-COVERAGE.md. Diagnostic source/objects do not replace production.
Owner authorized slice118 candidate integration and Luna's one-car research:
docs/EE-CANDIDATES.md, docs/CAR-ASSET-RESEARCH.md. Separate baseline/recovered
runtime/library pins do not change default production selection. Use one build/
test at a time; no owner session termination. Candidate cards use the existing
assets checkout locks/backups, never a frozen seed as a mutable card.
P0/P1 remain open until coverage, candidate integration and real-car contracts pass.
Missing callback entries44EA58/44EAC8 are recorded; live reachability is Unknown.
Luna is preferred for bounded inventory/research; semantic contracts need independent review.
Slice119 delegates Nenkai-reference research to Luna instead of Antigravity:
docs/NENKAI-REFERENCES.md and docs/tasks/P1-NENKAI-ASSET-RESEARCH.md. Six source
clones stay ignored; none is linked. A bounded private RoFS catalogue experiment
and independent replay pass, but production reader/B-tree lookup, named-car
identity, payload parsing and live hooks remain unqualified. Preserve slice119
results; new extraction uses a fresh capsule and reviewed caps/fixtures. The
packed RoFS version matches 0x00030001; qualified pages inflate before XOR 0x55,
including non-final flush segments. Do not adopt private experiments as production
tooling or assume GTPS2ModelTool has complete GT4 support.
Slice120 adds bounded offline scripts/gt4_volume.py and public fixtures:
docs/VOLUME-READER.md, docs/inputs/slice120-car-identity.json. Verify the whole
ISO and frozen catalogue hashes; extract explicit selections only into fresh
private capsules. Caps:8 files,16MiB per file,32MiB aggregate stored/decoded.
Ford Ka '01 is confirmed as SpecDB ka_01/code59, but menu/LOD mapping is Unknown.
Do not guess from11 Ford-prefixed files or assume GT5 MODEL_INFO exists in GT4.
SpecDB Huffman decoding remains private research, not an adopted production
decoder. No model/texture payload or live hook qualified; P1 remains open.
Slice121 now resolves the offline model map via CAR_VARIATION/VARIATION:
ka_01/code59 -> key444/table42 ->11 physical rows ->SDB1598/fdeu0006.
See docs/MODEL-ASSET-PROBE.md and docs/inputs/slice121-model-file-map.json.
Two exact car files extracted; bounded public header probe only. Preserve
repeated physical row IDs, signed opaque VM fields and zero-size Tex1 header
semantics as Unknown. Geometry/material/texture bodies and live getter argument
chain remain unqualified. Preserve frozen121 capsule; next scope is fixed records.
Slice122 bounds fixed shape/descriptors/material/PGLU/transfer arrays:
docs/MODEL-RECORDS.md, docs/inputs/slice122-shape-records.json. Shape+4 is
opaque, packet offsets are shape-relative and QWC*16 spans are MDLS-bounded.
Transfer starts are Tex1-relative; pixel extent remains Unknown. No VIF/vertex/
pixel decoding or live hook. Preserve frozen122; next scope is VIF framing.
Slice123 adds conditional byte framing: docs/VIF-FRAMING.md and
docs/inputs/slice123-vif-framing.json. Explicit initial CL/WL required; zero-cycle
and packed/reserved UNPACK reject. All 466 packet spans close under (1,1), but no
STCYCL appears and live incoming state is Unknown. Fill mode reduces input;
do not adopt the superseded worker report's destination-only/V4-5 formulas.
No VU/GIF/channel/topology/vertex semantics or renderer substitution qualified.
Slice124 static source/ELF contracts: docs/VIF-STATE-CONTRACT.md and
docs/inputs/slice124-vif-state-contract.json. Shape argument+4 flows to F12;
helper 49E548 conditionally writes transformed VF1-3 values and 144 buffer bytes.
Runtime descriptors use direct masked addresses; disk relocation remains open.
Buffered 0/4 then 4/4 commands do not prove live cycle/channel ownership. Do not
adopt PDTools channel/4096/UV flip/strip heuristics as game geometry contracts.
Next scope is controlled raw integer lanes plus concrete submission ownership.
Slice125 adds scripts/gt4_raw_lanes.py and literal public controls:
docs/RAW-CHANNEL-LANES.md, docs/inputs/slice125-raw-channel-lanes.json.
Explicit width/components/post-fill source count/signedness/alignment required;
32-bit patterns stay u32, padding is opaque, no float/channel/VU interpretation.
Selected raw counts remain conditional on initial (1,1) and USN assumptions.
Do not label raw values vertices or adopt channel/topology heuristics silently.
Slice126 research: docs/CHANNEL-SELECTION-AUDIT.md and
docs/inputs/slice126-channel-selection.json. Reference position predicates match
466 packets; float-normal predicate only 6/312 race, 23 garage UV candidates skipped
for 3 components. These are conditional matches, not proved game roles. Full
qualified-header audit matches derived counts; no legacy parser or converter
adopted. Preserve missing/multiple roles; next is authoritative observation/
ownership and bounded channel/topology contracts, not guessed conversion.
Separating presentation/game time does not require eliminating IOP/VU models.
Slice127 source contract: docs/SCENE-OBSERVATION.md. Two existing --probes at
455A5C/455A64 require a fresh profile/library, original-input tokens, ordinary
guest-owner incarnation/epoch and lifecycle invalidation across resume. Do not
infer helper FPR12 from D5 incoming bits or cursor deltas as buffer extents.
Next scope is bounded metadata correlation core, then separately qualified
generation/runtime integration; no whole-scene or live qualification yet.
Slice128 core/fixture: native/shape_observer.hpp/.cpp and
docs/inputs/slice128-shape-observer.json. Default-off,64owners/16pending/256records,
global stop+abort on ambiguity/capacity, bounded raw RAM samples, no live linkage.
Fresh native128 build CTest4/4/Python132/132; old118 caches omit observer fixture.
Next is exact two-probe generation and lifecycle adapter, preserving baseline
gates. Core IDs opaque; adapter must reject idle/unknown kernel owners.
Slice129 scripts/ee_observation.py produces only the exact two-probe source
profile in fresh private output. All328 files compared, only54 inserted D5bytes;
baseline ee_producer.py remains strict. D5 compiles, producerCTest1/1/Python141/141
using128 native fixtures. Full library/probe/lifecycle linkage remains pending.
Prioritize reproducible EE generation, then measured attribution and temporal
separation; native subsystem migration needs demonstrated cost/dependency.
144 distinct scenes require game render/update hooks, not repeated scanout.

Owner subsequently requested a comprehensive scene-renderer/native-port plan:
docs/NATIVE-PORT-PLAN.md supersedes ROADMAP phase order; docs/AGENT-WORKFLOW.md
defines bounded model routing and independent review. Initial owner-selected
target: Windows, entry-level dedicated GPU, internal1080p at60FPS; exact hardware
and feasibility are not yet qualified. Final direction excludes JIT and retires
PS2 graphics/execution/device dependencies in distinct gated phases. No-JIT
already holds for current Windows; it does not mean zero emulation.
Recommended first package is reproducible EE generation and one-car model/camera
contract research. Publishing these plans does not authorize all implementation
stages, external worker dispatch/provider setup or scheduled24/7 execution.
Game script/content VMs are distinct from PS2 instruction interpretation.

## Autonomy and communication

- Engineer plans, implements, verifies, documents, commits and pushes to origin.
- Commits/branches/pushes are authorized; keep main green, never rewrite history.
- Use small verified branches for multi-step work; merge by fast-forward when green.
- Speak Portuguese plainly. Lead with behavior and evidence; ask owner only for
  decisions requiring their input, not code review or routine implementation.
- Keep repository documentation, reports, prompts and new code comments in en-US.
  The owner translated existing documentation; preserve that language standard.
- Use subagents for read-only exploration/review and parallelizable work.
  Give paths/constraints explicitly, review their claims; only one repository writer.

## Code and evidence

All project code and general tooling must be human readable: descriptive names,
straightforward flow and formatting, explicit guest addresses/fixed-width state,
defined wrapping and no host undefined behavior standing in for guest semantics.
Tests may be concise. New behavior needs automated controls in existing CTest
and Python fixtures. Never silently accept unsupported instructions/services.

Label claims Confirmed / High confidence / Hypothesis / Unknown. Record hashes,
tool versions, addresses/offsets, observations, method and limits. Prefer
independent controls. Original-model gates now exist in the archive; they are
not validation of this different runtime. Do not weaken active acceptance checks.

## Data, builds and documentation

- ISO/BIOS/CORE/ELF, translated game code, cards, captures, outputs and binaries
  remain ignored under private/generated/build or ignored ISO paths. Never stage.
- Verify against `docs/inputs/` before use; verification must not update pins.
- Preserve frozen producers/capsules. New slices use a fresh private output path;
  do not overwrite old slice directories or reuse their checkpoint identity.
- Preserve owner saves and backup/trava behavior. An archive branch saves tracked
  files only; ignored data stays local and must not be deleted by cleanup scripts.
- Current build: `cmd /c native\build.cmd`, then
  `ctest --test-dir private/slice114-native-base/build --output-on-failure`.
  Run builds/tests serially; Windows link outputs and manual cards can be locked.
- Python: `private/tooling-venv/Scripts/python.exe -m unittest discover -s tests/python`.
- Current build still needs the pinned private ps2game.lib. Follow BUILD.md;
  do not describe this as an end-to-end clean-checkout build.

Documentation is part of completion: update STATUS when state changes, append
`docs/journal/YYYY-MM-DD.md`, add numbered decisions/evidence/lessons as useful.
Only distributable metadata goes in docs/inputs. Before ending: check
`git status --short`, commit code/tests/docs together and push verified changes.
