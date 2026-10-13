# Current Status

2026-10-13 — Slice 123, conditional bounded VIF command framing.
Playable base remains slice 114. Implementation branch:
`codex/gt4-vif-framing-123`, based on commit 966297b.
Slice 118 candidates remain separate and owner gameplay qualification is pending.

## Authorized P0/P1 Progress

The owner authorized P0/P1 while OpenCode continues its separate performance
branch. Work uses an isolated checkout; no OpenCode edits, session or saves changed.

- **Confirmed:** fresh CORE-to-ELF reconstruction matches the pinned native ELF;
  independent verified ISO extraction produces the same CORE/ELF.
- **Confirmed:** the donor translator rebuilt with Clang 23.1.2 generates all
  328 source files identically to the frozen producer. No old CMake cache is used.
- **Confirmed:** the fresh library's 327 AMD64 object payloads match the baseline
  after normalizing only their COFF timestamps; archive paths explain the size delta.
- **Confirmed (slice 117):** producer CTest 2/2 passes; Python discovery 55 tests,
  52 passing and 3 native integration skips in the isolated checkout. These are
  not candidate runtime or gameplay qualification.
- **Confirmed:** seven model/camera candidates map to real generated registry
  entries and prefix hashes; camera helpers and model VM/callbacks have state effects.
- **Confirmed:** baseline callback starts `0x0044EA58`/`0x0044EAC8` lack generated
  bodies/entries. An offline supplier-only seed recovers both, adding 45 instructions
  and nine entries. Supplier-only/both source matches; all 18 changed units compile.
  Existing bodies/callers match under independently reviewed dispatch-ID normalization.
  Production seeds/library are unchanged. The donor EE dispatcher halts on missing code without a module
  replacement; it has no EE interpreter fallback.
- **Unknown:** live reachability of these omissions and any performance relationship.
  Generation equality is not complete-code coverage or PS2 fidelity proof.
- **Confirmed (slice 118):** complete recovered library compiled in a fresh cache;
  all 327 normalized AMD64 object members checked: exactly 18 reviewed differences,
  309 unchanged. Baseline remains the qualified rebuilt slice 116 library.
- **Confirmed (slice 118):** both isolated native runtime profiles link and pass
  CTest 3/3 and Python 59/59 with zero skips and zero observed compile warnings.
  All nine shaders match production. The actual candidate executables are used
  by native integration fixtures; no manual game session was launched during build qualification.
- **Confirmed (slice 118):** bounded research located the actual GT4.VOL header/
  offset table; parent independently verified its metadata. Page decoding, named
  car/asset/live-instance identification were Unknown at that checkpoint. No car payload was extracted.
- **Confirmed (slice 119):** six pinned research repositories cloned; Luna decoded
  all 407 ToC pages with bounded raw-DEFLATE-before-XOR processing. Parent replay
  independently matches decoded hash, 18,562 records, 210 directories, directory
  reachability and 18,352 file extent bounds. Car/SpecDB paths are now observed.
  Packed version matches GT4FS's 0x00030001 constant.
- **Unknown:** production reader/lookup qualification, named-car crosswalk,
  model/texture payloads and live instance/camera effects. No asset payload was
  read and no renderer or performance change was made in slice 119.
- **Confirmed (slice 120):** public bounded volume reader reproduces slice 119
  catalogue hashes/counts. Five selected SpecDB files extracted after whole-ISO
  verification on the same handle; strict PS2ZIP framing, sizes and hashes pass.
- **Confirmed (slice 120):** offline `ka_01` / car code 59 resolves to Ford Ka '01
  in USA GT4_US2560. Name/Grade/ShortName use localized string 84; NarrationID
  is raw integer 435. Worker and independent parent code-list Huffman replay
  match both selected rows. SpecDB decoding remains a private research probe.
- **Confirmed (slice 120):** 22 new public controls pass; existing baseline
  candidate CTest 3/3 and Python 81/81, zero skips. No native code/build change.
- **Unknown:** which menu/LOD file corresponds to this car; exact `ka_01`
  basenames and MODEL_INFO pair are absent. Eleven Ford-prefixed leaves remain
  candidates, not identified assets. No model/texture payload read or live hook added.
  B-tree key routing and comprehensive format/fidelity qualification remain open.
- **Confirmed (slice 121):** CAR_VARIATION row 59 selects VARIATION row 444/table
  42; all 11 physical variations use SDB model index 1598, `fdeu0006`. Independent
  Huffman replay matches all 12 selected row hashes. Exact garage/LOD leaves,
  not guessed Ford prefixes, are identified and extracted with verified pins.
- **Confirmed (slice 121):** both CAR4 main MDLS bases are 0xC00; garage/race
  have 4 models each and 22/113 shapes. Independent header checks match. Signed
  -1 VM words are opaque; five zero-size Tex1 slots are reported header-only.
  Geometry, texture records/pixels, relocation and live argument chain remain Unknown.
- **Confirmed (slice 121):** 17 public header controls; CTest 3/3, Python 98/98
  with zero skips. No native rebuild, live hook or runtime behavior change.
- **Confirmed (slice 122):** selected garage/racing main models contain 154/312
  bounded shape descriptors, 14/18 material records and 49/123 PGLU texture
  records plus 5/15 transfer records. Packet spans and fixed arrays are bounded;
  shape +4 is opaque, transfer storage extent and material semantics Unknown.
- **Confirmed (slice 122):** 11 new public controls; CTest 3/3, Python 109/109,
  zero skips. No VIF/vertex/pixel decoding or runtime change. Independent record
  review is recorded in [Model Records](MODEL-RECORDS.md).
- **Confirmed, conditional (slice 123):** under explicit initial CL/WL=(1,1),
  all 466 selected packet spans frame to their exact QWC end. No STCYCL appears,
  so live incoming cycle remains Unknown. No VIF/VU execution, GIF meaning,
  channels, topology, vertices or renderer substitution is qualified.
- **Confirmed (slice 123):** 11 public framing controls, CTest 3/3, Python
  120/120, zero skips. Fill cycles reduce source consumption; NUM0 expansion
  is explicit. Packed/reserved UNPACK and zero-cycle fields reject as unqualified.

See [EE Producer](EE-PRODUCER.md), [Render Contracts](RENDER-CONTRACTS.md) and
[Callback Coverage](EE-CALLBACK-COVERAGE.md), with [slice 116](inputs/slice116-ee-producer-contracts.json)
and [slice 117 evidence](inputs/slice117-ee-entry-coverage.json), for build/test
results and remaining gates. [EE Candidates](EE-CANDIDATES.md) and
[Car Asset Research](CAR-ASSET-RESEARCH.md) cover slice 118. P0/P1 remain **in
progress**: [Nenkai References](NENKAI-REFERENCES.md) and
[slice 119 metadata](inputs/slice119-nenkai-assets.json) record subsequent catalogue
progress. [Volume Reader](VOLUME-READER.md) and [slice 120 metadata](inputs/slice120-car-identity.json)
record the selected extraction and offline identity; candidate gameplay, remaining entry coverage and one real car's
data/conventions are pending. Production library, default launcher and runtime
have not been replaced. Candidate checks do not qualify game boot or racing.
See [Model Asset Probe](MODEL-ASSET-PROBE.md) and
[slice 121 metadata](inputs/slice121-model-file-map.json) for the now-confirmed
offline file mapping and exact header-only parsing boundary. P1 remains open.
[Model Records](MODEL-RECORDS.md) and
[slice 122 metadata](inputs/slice122-shape-records.json) extend this to fixed
record and declared packet-span inventory; renderable geometry remains pending.
[VIF Framing](VIF-FRAMING.md) and [slice 123 metadata](inputs/slice123-vif-framing.json)
record conditional framing, corrected source assumptions and remaining live-state gates.
The owner's active OpenCode baseline session was preserved during build qualification.
On the owner's subsequent explicit request, it closed normally and the slice 118
baseline candidate launched at 13:16 local time. Its window responded and module
loading progressed; owner gameplay confirmation remains pending. The existing
card, normal session locks and per-session backup were used. This startup
observation does not qualify racing or performance. Recovered remains unlaunched.

## Active Path

`native/` + MIT ps2-web-recomp runtime frozen at
1b4808793a12c45125262fd2d5f50a9ec9f7c020. EE translated AOT to x86-64,
IOP/VU interpreted, and PS2 hardware/services modeled in software. Vulkan compute,
Win32, WinMM, and XInput. Does not use a browser, WebAssembly, or embedded PCSX2.

The original engine, dedicated tooling, and engine-specific tests have been removed from `main`;
preserved on `codex/archive-original-recomp-20261013`, commit 95f6155, published to origin
prior to cleanup. Private data, saves, and frozen producers remain local.

## Confirmed Results and Limits

- Boot/menus, car/track selection, and genuine Nürburgring racing gameplay; limited 9,000-frame automated controls.
- In-game 16:9 camera/HUD and default 16:9 window framing; 4:3 optional.
- Owner manually tested 480i/480p/1080i during gameplay, all in 16:9; 480p subjectively
  most stable. Human functional confirmation without independent proof of scan fields.
- 48 kHz stereo PCM delivered to Windows; buffer underrun/continuity issues remain open.
- XInput/analog/rumble implementation covered by test fixtures; physical controller hardware untested.
- Persistent memory card, profile/settings saved by the game, per-session backup, and mutual exclusion lock.

Severe performance and frametime degradation recorded: 19 FPS in-race, 11 FPS in 3D car showroom,
18 FPS / 72.5 ms in 1080i screenshot; overlay metrics are not independently calibrated.
Keyboard arrow keys produce double-movements per keystroke per owner report; other keys
suspect. Root causes unknown. The prior record-only performance restriction is now
superseded for an owner-delegated OpenCode investigation; Codex prepares context only.
Input fixes remain deferred. Gameplay is owner-controlled: no Computer Use,
GUI automation, simulated input or scripted navigation. Manual play window and memory card are untouched
by this cleanup. CPU/GPU fidelity, CFC2(28), temporal deinterlacing, 100% realtime rate,
prolonged stability, completed full races, and broad save scenarios remain open qualification items.

## Build and Verification

The root build now only covers the current Clang/C++20 pipeline, generated by
`native/build.cmd` into `private/slice114-native-base/`. Runtime 113 and nine original SPIR-V
shaders remain preserved. The private AOT library is a prerequisite,
validated by hash; the playable build does not yet consume the separate P0 producer.
Clean build with zero warnings; 3/3 CTest passing in 11.13s, 32 Python tests passing without skips (final rerun in 5.812s).
A 120-frame headless smoke test exits cleanly with memory card duplication,
without navigating or launching a new race. All 9 SPIR-V shaders match slice 113; frozen pins preserved.
The original 54 CTest / 117 Python tests are historical: tests for the removed engine reside
on the archive branch; the active test suite covers the surviving codebase.

## Native Port Direction

The owner requested a comprehensive native-renderer/native-port and multi-model plan:
[Native Port Plan](NATIVE-PORT-PLAN.md), [Agent Workflow](AGENT-WORKFLOW.md).
Initial target selected by owner: Windows, entry-level dedicated GPU, internal
1920x1080 at 60 FPS. Exact hardware/preset and achieved throughput remain Unknown.
Owner clarified that deferred rendering and a graphical remaster are outside the
current roadmap. Native graphics, progressive output, configurable internal
resolution, ultrawide and independent simulation/presentation timing remain goals.

The owner authorized reproducible EE generation and bounded model/camera research
in slice 116; slice 117 qualifies one callback recovery mechanism offline. The
planned following stage is one live showroom car drawn through a game-level
Vulkan renderer. Expand to racing, progressive/internal resolution and temporal
separation; retire all graphics paths, then remaining execution/device dependencies
through separate qualified service/core migrations. AOT alone does not mean zero
emulation. No Windows JIT is active today; the proposed port does not introduce one.

The new plan supersedes the phase order of [ROADMAP.md](ROADMAP.md), whose timing
analysis remains useful. Eliminating IOP/VU models is not a prerequisite for
FPS independence; 144 distinct scenes require qualified visual state/hooks.
Plans include economical worker routing, independent review, serial integration
and owner-operated gameplay gates. No workers, provider setup or recurring jobs
were dispatched outside the read-only planning review.

This revision is documentation only: no renderer/code generation implementation,
profiling, build, game run or new qualification result. Existing OpenCode changes
and the owner's session/card are preserved. Input, audio, save and stability work
continues to require its own authorized package and acceptance controls.

Technical reference: [Architecture](ARCHITECTURE.md), [Build](BUILD.md),
[Legacy Archive](LEGACY.md), [Slice 114 Report](reverse-engineering/slice114-native-base-cleanup.md).
Prior functional evidence: reports 110–113, corresponding manifests, and
`inputs/slice113-owner-results.json`. Slice 114 does not reclassify these results
as complete fidelity proofs or official benchmarks.

README updated for public presentation: project vision, confirmed features,
experimental boundaries, and future goals. Editorial change only; does not alter
runtime, requirements, priorities, or validation results.

## OpenCode Performance Handoff

The owner requested an investigation prompt, not debugging by Codex.
[Task prompt](OPENCODE-PERFORMANCE-HANDOFF.md) provides verified paths, build and
launcher constraints, hypotheses to test, fresh producer/manifest requirements,
manual feedback iterations and save protection. Initial source baseline is
cbccf80; executing agent must recheck HEAD. No profiling, build or game run occurred
while preparing this handoff. Maintain documentation and new comments in en-US.
