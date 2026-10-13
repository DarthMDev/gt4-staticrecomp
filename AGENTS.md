# Persistent project instructions

This file is auto-loaded at the start of every agent session. It is the working
agreement for GT4Recomp in this fork and the entry point to persistent project
knowledge. Keep it current whenever the rules change.

## Mission and context

GT4Recomp statically recompiles the pinned Gran Turismo 4 (USA) v2.00 R5900
code ahead of time into C++20 for Windows x86-64, supplying the PS2 services
execution requires. Learning and evidence quality take priority over speed.
This repository (github.com/moesuito/gt4-staticrecomp) is the working fork.
Upstream (github.com/Kaezzey/gt4-staticrecomp) is a read-only reference: we do
not push or open pull requests there.

Owner priorities reaffirmed 2026-10-11: continue verified small slices
autonomously toward real game boot, reporting progress and asking when a
decision needs owner input. After causal boot progress, pursue native Vulkan
rendering, audio, XInput controls and real game saves; preserve those goals
without substituting a replay/mock screen for guest output.

Owner-directed acceleration study 2026-10-12 supersedes the next ordinary
slice110 diagnostic priority: audit/use external bases to reach functional
boot sooner. Decision0051/report external-recomp-acceleration-2026-10-12
qualify a private ps2-web-recomp native Windows/Clang CPU pilot reaching
real initial screens with our pinned inputs. Production remains MSVC and
unchanged; external model/checkpoints are separate. Slice110/decision0052
qualify profile entry/card creation, reread coldboot mainmenu, navigable Arcade
and GT Mode map. Reduced menu repeat has14 byte-identical captures. Next
qualify coherent runtime integration and Win32/GPU/native Vulkan, preserving
contract gates. No race, broad save coverage or stable production adoption.
Decomp CORE published SHA1 matches ours; reuse metadata with verification.
PS2Recomp GPLv3 and contract differences require explicit boundaries.
EE Reloaded is auxiliary, not a prerequisite for the execution pilot.

Slice111/decision0053 qualifies a separate direct native Vulkan compute
backend and Win32 presentation: real menus/circuit selection/FordKa3D,
normal3000-frame stop,15/15headless/window PPM identical. Production stays
unchanged. Next112: first CPU/GPU pixel divergence (frame400) and Vulkan
validation evidence, then controlled race/loading qualification. Do not call
menu/car3D stable racing or adopt external semantics without contract gates.
See docs/reverse-engineering/slice111-native-vulkan.md and its input manifest.

Owner priority2026-10-12/13 supersedes that historical next112: Windows audio
and XInput now, Vulkan customization later, prepare manual racing. Slice112/
decision0054 adds native WinMM stereo16/48kHz owned-buffer output and XInput
DS2 mapping/pressure/axes/rumble, keyboard, focus/disconnect/close safeguards.
Jogar-GT4.cmd verifies inputs and preserves a private persistent card with
per-session backups and native exclusive lock. Physical controller deferred by
owner (away; initial four slots disconnected). Correct API-permitted zero-work
Vulkan dispatch to pass the first race-intro blocker; invalid bounds still stop.
6000-frame Nürburgring repeat completes normally,48 common captures exact,
all4793888 audio frames completed, but output starvation/fidelity remains open.
9000-frame driving control also completes,60 common captures/PCM prefix exact;
direction/brake/reverse observations are consistent but not causally isolated.
Manual owner test and measured audio/runtime starvation are next113. Do not equate this with stable
full races or broad saves. Frozen111 and production remain unchanged.

## Roles and autonomy

Owner priority2026-10-13: native16:9 default with4:3 optional, then real race
480p/1080i qualification. Record unstable frametime and severe3D performance
(19FPS race,11FPS car selection), but do not investigate/optimize performance
yet; owner explicitly deferred it. Preserve later144FPS/monitor-refresh rendering
with100% game speed independent of physics, progressive-only output and native
internal1080p/1440p/4K plus preboot launcher configuration. Window enlargement
does not establish internal resolution, new rendered frames or correct simulation.

- The agent is the engineer and decision maker for technical work: it plans,
  implements, verifies, documents, commits, branches and pushes to `origin`.
- The human owner sets goals and priorities and reviews plain-language
  summaries. Never require code knowledge from the owner.
- Commits, branches and pushes are authorized and expected. Keep `main` green
  and pushed. Do not rewrite pushed history.

## Documentation is part of the work

Assume the chat may be compacted or lost at any moment; nothing important may
live only in chat. Write knowledge into the repository as it is produced, not
at the end of the session.

- `docs/STATUS.md` — read this first in a new session: current state,
  environment, open items, next actions. Update whenever the state changes.
- `docs/journal/YYYY-MM-DD.md` — append-only session log: what was done, what
  was discovered, what failed and why, what comes next.
- `docs/decisions/` — numbered records for choices with trade-offs, continuing
  the existing sequence.
- `docs/reverse-engineering/` — evidence documents per technique or milestone:
  hashes, commands, observed results and limits.
- `docs/lessons/` — worked explanations (EXPLAIN) per milestone. Tracked in
  git in this fork; upstream kept them local.
- `docs/inputs/` — only hash-pinned, distributable metadata; never payload bytes.

A change is only done when its documentation is updated. Before ending a
session: update `docs/STATUS.md`, append the journal entry, commit and push.

## Evidence discipline

- Label claims Confirmed / High confidence / Hypothesis / Unknown.
- Record input hashes, guest addresses or file offsets, tool and version,
  observed state, comparison method and the next experiment
  (see `docs/requirements.md` for the full convention).
- Prefer independent verification (Ghidra, a second tool, a hand-computed
  value). A self-written implementation can share a bug with itself.
- Never guess in code: unsupported instructions and services stop with useful
  context; returning success cannot establish correctness. No silent fallbacks.
- Pin versions and hashes; reject changed inputs.

## Inputs and repository hygiene

- Game payloads, BIOS, extracted files, captures and translated code live only
  under ignored directories (`private/`, `iso/`, `generated/`), never in git.
- Verify inputs against `docs/inputs/` manifests before use; verification never
  updates a manifest.
- Check `git status --short` before every commit; never stage local game data,
  generated output or tooling.

## Code standards

Explicit owner requirement, recorded 2026-09-13:

All code written for this project must be HUMAN READABLE. Always assume a
human will read, review, and learn from it. Never justify obscure or compressed
code by assuming it will not be inspected by a person.

Use descriptive names, straightforward control flow, and clear formatting.
Prefer explicit, understandable steps over clever expressions or unnecessary
abstractions. Explain non-obvious intent and assumptions where they matter.
This requirement also applies to generated C++ and project tooling scripts.

The owner's exception permits assuming that test files and test scripts may
not be read by a human. This does not extend to general project scripts.

Additional standards:

- C++20, MSVC x64, Ninja, CMake 3.24+; keep the build warning-free and green.
- New behavior needs automated tests; extend the existing CTest and Python
  fixtures instead of adding parallel harnesses.
- Keep guest state explicit: fixed-width integers, controlled guest addresses,
  defined wrapping; no host undefined behavior as a stand-in for guest
  semantics.

## Git workflow

- `main` always builds and passes `ctest`; push it to `origin` regularly.
- Work in small verified slices. For multi-step work use a short-lived branch
  (e.g. `m7-control-flow`) and merge to `main` when green.
- Commit style follows the project history: short imperative subject lines with
  the evidence numbers that justify the change. One logical change per commit;
  code, tests and docs travel together.
- Fetch `upstream` occasionally; merging it is a deliberate, documented
  decision, never automatic.
- Push every verified, documented commit to `origin`.

## Quick reference

Live state: `docs/STATUS.md`. As of 2026-10-12 (slice110, evidence `67f65ba`, green):
External menu pilot qualified separately; production semantics unchanged.
The slice95–109 observations below remain historical/contract context.

Owner-requested model-switch handoff: `docs/RETOMADA.md` (2026-10-09), now
prefixed with closed slice101/102/103/104/105/106/107/108/109 updates. Its original checkpoint is
historical; root `HANDOFF.md` is archival machine-move material.

- Recompiler pipeline M0–M29 complete: decoder covers 349 operations;
  whole-text scan finds 497 unsupported words of 1,334,917 (467 inside
  the trailing 700-word data table, 30 in real code: 26 COP2 macro
  function-0x38 words plus two DMA-dependent BC0F and two unassigned
  function-0x28 encodings — slice-58 address audit). The translator
  handles 99.5% of direct-call targets, and `--all` generates the whole
  game as one module (15,068 functions, 924,991 static instructions,
  ~216 MB with slice-98 observation calls).
- Boot + model: historical slices reached disc/archive/sound/font, but the
  slice-95 corrected interrupt return exposes an earlier timing frontier
  (details below). The translator-vs-interpreter 90,000-service gate is
  green; old 800k/5M horizons used different semantics and are historical.
  Contracts P00–P10 are done (decisions 0028–0036
  accepted; 0034/0037/0038 drafts open): checkpoints with semantic
  identity, 16-bit timers with W1C, separate INTC/DMAC domains, one
  advance machine, handler arguments with explicit idle, jr capture with
  explicit module-exit reasons, real DMA payload with chain walking, RPC
  telemetry with `--strict-rpc`, and a widened state comparator.
- Frontier: **update loop excludes READY main before any instruction** (slice 96).
  Corrected final interrupt-return preemption (decision 0013), exact PC
  preserved; fresh 10k/100k census now only threads 1–3, main READY/prio64,
  update RUN/prio0, 2 RPC pairs, zero GIF payload. Less boot progression is
  recorded honestly; old later phases depended on omitted preemption.
  Production still uses 1 ms/service; reference-backed time policy remains
  open. Semantic interrupt_model 4, time 3, kernel 3, RPC 2, translation 2;
  GT4CPT3/GT4KERN2 layout unchanged, old interrupt-model-3 checkpoints refused.
  Slice 96: all 270 one-shot 1000-us waits really block with a full 1 ms
  left; WaitSema's own 1 ms charge expires them and TIM2 interrupts root
  at its exact restored PC, before any guest instruction. Handler/return
  exclusion reproduces 273/273. Reference same loop has VBlank waiter;
  elapsed timing/phase alignment still unknown. No replacement adopted.
  Slice 97 reference: root retry 2000-us WaitSema dispatch 1111 EE cycles
  (~3.77 us nominal), 294400/294912 BUSCLK ticks remain; actual frame
  SleepThread dispatch 1630 cycles. Both select idle EPC 0x81FC0, not root.
  No matched reference update 1000-us/root-work interval. Offline timing
  reader pinned to audited save/build; no new clock policy. Reference closed,
  breakpoints removed and original slot9/backup restored with hashes.
  Slice 98: optional --count-work (decision 0040), 26 hand-counted paths;
  90k differential reports 22,566,319 completed / 90,000 accepted services
  identically, full state identical. 10k counted/plain output otherwise exact.
  Host counter outside contexts/checkpoints, no clock conversion. Preexisting
  handled likely-slot syscall gap explicitly tested: native5/1 vs reference1/0.
  Slice 99 source/fixture audit (decision 0041): pinned PCSX2 interpreter
  BEQ/BNE-false tests at next word; normal dynarec pairs branch/slot before
  event test. Other families differ; no universal exclusion fix adopted.
  Qt Step Into resumes to temporary breakpoint with selected motor, not
  forced interpreter/one-word execution. 24 characterization controls:
  six branch paths, direct/segmented event probes, three equal-state stopped
  slots with different pending ownership, native callee poll/restore work3->7.
  BIOS slot return/live trap context and matched update interval unknown;
  production binary identical to slice98, no new clock or compatibility.
  Slice100 live: plain dynarec GetThreadId vector -> ERET -> continuation
  captured, v0=1. Six syscall-slot paths: five EXL1/EPC=B/BD0/v0=-1
  continuations with no vector hit, BEQL false annuls. Six effect controls
  confirm slot/annulment. Pinned BEQ emitter overwrites exception-selected
  PC before dispatch; do not copy this reference defect into production.
  Latch-only and BEQL no-range-breakpoint repeats agree. Interpreter
  per-word breakpoint probe build-guarded and ignored by this binary;
  BEQL run logs cpuException's BD warning then later BIOS PC, not latch.
  Warning is PCSX2 output, not BIOS text; exact failure/selected return
  still unknown. 31 captures re-extracted; owner INI/BIOS/slot9+backup
  hashes preserved, isolated process closed normally. No runtime change.
  Evidence: `docs/reverse-engineering/slice100-live-branch-controls.md`.
  Slice101 resolves that synthetic interpreter unknown using the exact
  release's matched official PDB and a separately qualified host observer.
  Thirteen controls plus two reduced repeats: taken BEQ/BGEZ/BEQL first
  vector EPC=B/BD1; BIOS selects slot S; ERET returns S/EXL0/v0=1, repeated
  syscall with BIOS-modified v1=BC -> JALR0 -> TLB Cause8/EPC0 and bounded
  later BIOS chain. Plain/ordinary false return S+4; BEQL false annuls;
  six effect controls agree. State writes are not instruction counts.
  Reduced repeats match76/500 observations; independent review approved.
  No production or clock change; fake-service likely gap remains5/1 vs1/0.
  Fifteen accepted traces/17 helpers private; owner INI/saves/BIOS preserved,
  all78 isolated BIOS copies match. Reference closed normally. Decision0042
  accepts this observation route only. Evidence/metadata:
  `docs/reverse-engineering/slice101-interpreter-trap-observation.md`,
  `docs/inputs/slice101-interpreter-traps.json`.
- Gates: 54/54 CTest + Python 97 (91 run, 6 skip), re-run at slice 107;
  no existing acceptance expression weakened.
  Tripwires armed; M35's pad promoter watched
  (the first padman bind reopens input work).
- Slice102 real-disc dynarec:429 complete windows/430 entries/3004 states;
  independently decoded block-cycle stores qualify vector/context endpoints.
  Two2000us retries cost1110 reference cycles between those boundaries,
  select idle with nearly full deadlines.89 frame waits; no observed1000us
  update match. Message wake a0=1 precedes frame wait restoring the earlier
  receive context. Reduced repeat54/54 recorded states identical, including
  cycles/arguments/stack/timer/frame. No production policy change;0043 accepts
  observation only. Five references closed; owner files/sixBIOS/18copies
  preserved.22helpers private; docs/inputs/slice102-reference-intervals.json.
- Slice103: first message priority chain confirmed independently. Normal
  current-thread24->1 returns24 and1->24 returns1; game saves that return
  and restores24. Model returns0, consumes it and restores0. Nine fresh
  snapshots/two diffs;20 reference windows/140 states, reduced60/60 exact.
  Reference main resumes receive then blocks in synchronous RPC_BIND,
  selecting idle. Model RUN root in timer callback is not ordinary progress.
  No production/time change, owner files/BIOS preserved, two references closed.
  Evidence: `docs/reverse-engineering/slice103-message-wake-prefix.md`,
  `docs/inputs/slice103-message-priority.json`; decision0044.
- Slice104 corrects return-only contract: previous target priority before
  dispatch; kernel_model3 rejects old2. Nonzero restore/preemption/snapshot/
  resume/other-target tests; documentary PS2Tek i* return support, remaining
  TH_SELF/dormant/i-reschedule audit debt explicit. Fresh update priority24,
  root stillREADY64 at5ADBC8, three threads/twoRPC pairs/zeroGIF at10k/100k.
  53/53CTest+Python91(85/6);90k/10k differential fullstate/work green.
  Strict mode refuses preexisting80000592/fn0; defaultempty reply is not
  validated boot. Evidence104/metadata and decision0045 in docs.
- Slice105 qualifies CDVD_INIT80000592/fn0 mode0/send4/recv16 with two real
  completions{1,226hex,226hex,0}, reduced12/12 states and native disc modules.
  Other variants stop; real SIF result/endpacket/DMAC path retained. RPC2
  refuses actualRPC1, strict/default10k checkpoints identical;0unknown at
  strict10k/100k. RootREADY64/5ADBC8/updateRUN24,0GIF, no functional boot.
  Main header dependency bug fixed: UTF-8 localized metadata normalized to
  ASCII,16headers recorded/header timestamp schedules recompile; C/CXX only,
  RC audit debt. Six launcher fixtures and54CTest/Python97(91/6) pass.
  Evidence105/metadata, decision0046 and lesson in docs; clock/IRQ unchanged.
- Slice106 evidence-only:31referencewindows217states/reduced93/93exact.
  First post-message sync entersreference3/3/model4/3 withcontrols0/1;
  referenceSleep restoresactualmainreceive/work/RPC_BIND then idle. Restore
  s5=0reference/1model reachesnextGetThreadRA4A06B0/outer101748 in savedstack.
  Model timerWait retainsupdateouter578B20/101760;1000us value staticcaller,
  notlivea0(semaid). Read-only modelobserver10k fullstate/identity/services
  byteequalproduction with explicitdiagnosticprovenance; code/module/libs
  pinned. ExtraW4generatedwarnings retained; unchangedproduction54/97gates
  inherited. Owner3/BIOS6/copies12 preserved,14helpersprivate, refsclosed.
  Evidence106/metadata/decision0047/lesson. No clock/IRQ change orbootclaim.
- Slice107 evidence-only:40referencewindows280states/reduced120/120exact;
  firstupdate/notify3/3 both, same main waiter receiver->pending, but model
  adds frame before release4/3 while reference stays3/3 through wake/sync.
  Qualified callers/stack/queue; numeric IDs/addresses local. Modelprivate100
  at1604 already4/3 while waiter stillreceiver. Earlier receivephase differs;
  no absolute elapsedtime alignment. All80cycledebits +2/RIP+18 independently
  qualified;67/151rawendpointdeltas are not full costs or conversions.
  Read-only121modelrecords1220..1340, explicitmainRAMpointerbounds, diagnostic
  10k fullstate/models/services byteequal105/work8946341/interpreterequal.
  Independent framing audit passes. Final restored build54CTest200.59s and
  Python97(91/6)61.739s pass; fresh10k gueststate/models/work identicalold
  producer/observer, only git_commit provenance differs. Newexe839adaa22f48...
  separately pinned; oldexe/six producerinputs frozen before rebuild. Main/
  module unchanged, libs rebuilt. VSamd64+UTF8 required before configuration
  and build/autoregen; outside-VS/OEM850 attempts failed loudly, retained.
  Broadgetter initial capture and preguardmodel rejected/retained; generated
  profileLF/CRLFhash correction verified, no inputmanifestchange. Owner3/BIOS6/
  copies24 preserved, all3refsclosed,25helpersprivate. Evidence107/metadata/
  decision0048/lesson. No runtime/clock/IRQ change or boot claim.
- Slice108 evidence-only: exact model N1300 notify charge147456 completes
  accumulator2310144->0 and queuesINTC2. Handler4AB430 overupdate3 saves
  PC5ADB98/RA5768F0; bridge slotstore4AB4AC writes70002050 frame3->4/synced3
  withsame mainreceiver{0,1}. Private100 returns thatinterrupt, notidle;
  deferredcontext ownsPC, CP0Cause/EPCunchanged. Queueentry isnewoccurrence,
  INTCSTATalreadylatched. Reference40windows280states/reduced120/120exact,
  PDB-qualified phase staysGSblank2/3/3; delayedGSdue notnextINTC2. Local
  phase doesnotalignelapsedtime. Model47/47records andfullrepeatCPTexact;
  freshproduction10k equals107wholeCPT, observerallgueststate/models/services
  equal withdiagnosticbinaryprovenance. Work8946341/interpreterequal;
  finalmapprivateobjectsqualified. Productionexe/module/libs/sourceunchanged,
  gates54/97inherited107. Rejected ODR build never run; superseded no-map capture
  retained. Owner3/BIOS6/copies12preserved, bothrefsclosed.25helpers54artifacts;
  evidence108/metadata/decision0049/lesson. No clock/IRQchange orbootclaim.
- Slice109 evidence-only:534modelrecords/repeatexact, wholeCPTrepeatexact,
  productionwholeCPT equals108/fullstatework8946341 interpreterequal. N1267
  maingetterqueue617D04 charge2359296->49152 queuesINTC2, store4AB4AC1->2
  overmain1/savedPC5ADB98 before receiver1284already2/2. Reference42windows/
  298states,294full/126reduced commonexact; four calibratedfastmemDr2writes
  withhostEA/value/width+EEblock/ELF qualify4AB4AC, rawPC4AB460stale.
  Registration/enable0/0 alreadyINTC2latchedmasked; firstwriter1/0 same
  renderMode0/start1801186298 before nextVBlank. Priorpendingdelivery
  highconfidence, notimmediateEnable orqualifiedorigin/interruptedowner.
  ModelSTAT804/MASK800queueempty needs earlierlifetimeaudit; don't blindly
  reconstructqueuefromSTAT/duplicatepreviousdelivery. N1284chargequeues2
  beforeidle; resetrawPC107730stale/nativeSQ5A4924. Work405460/79services,
  405381nonservice/no conversion. Productionunchanged/gates54/97inherited107;
  owner3/BIOS6/copies18preserved,3refsclosed,33helpers53artifacts pinned.
  Firstwatchattemptrejectedretained; publicationpathrepaircheckedarchives.
  Evidence109/metadata/0050/lesson. No runtime/time/IRQchange orbootclaim.
- Next (owner priority2026-10-12): **external runtime integration pilot**.
  See decision0051/report external-recomp-acceleration-2026-10-12 and STATUS.
  Slice110/0052: complete Windows/Clang CPU runtime reaches mainmenu, Arcade
  circuits/manufacturers/FordKa3D and GT Mode map with pinned USA2.00 inputs.
  Guest-created private card is copied/hash-verified and reread on coldboot;
  original preserved.1500frame repeat normal,14/14 common100..1400PPM exact.
  Long3000car probe timeout180s/lastcapture2700 retained, not stability/race.
  Hash-checked launcher now tracked with seven synthetic controls;54CTest/
  Python104(98run6skip) green. Productionexe839adaa...6dfab unchanged.
  Next coherent runtime integration, actually used unknown services and
  WindowsGPU/Win32/nativeVulkan. Broader save fidelity/adoption open.
  The former slice110 diagnostic below is retained as historical evidence,
  not the current first action: pending INTC2 origin/lifetime before reset.
  Qualify occurrence/queue/STAT-W1C/delivery/mask/CPUgate and earlierreference
  phase/gatereturn; locate modelbit2withqueueempty beforechoosingcorrection.
  This replaces the former109action below, retained as its historical scope:
  Qualify native registration, reference frame writer/INTC2 context and model
  charge history before the earlier receive split, including non-service work.
  Keep correctIRQ and1ms/service until a replacement is justified. First
  preceding causal phase source and1000us reference match remain open;
  no guessedquantum/forcedwake or absolute elapsedtime alignment byframes.
  Qualify engine/phase and any new observation identities per segment before
  any work->time policy; do not assume both PCSX2 engines agree or call
  release Step Into a word trace. The synthetic trap chain is observed;
  a future production slot-resume change still requires explicit pending
  state, serialization/comparator coverage and separately justified semantics.
  Account identically, including non-service computation;
  do not guess an instruction/cycle conversion or special-case WaitSema.
  Slice-94 attribution/exclusion rates concern the old interrupt_model 3,
  not the corrected trace. Do not undo correct preemption to recover older
  boot counters, adopt a guessed quantum, or resume
  diagnostic timing with production checkpoint identity. Service 0x100 at
  0x1604 is deferred return (Patch/Interrupt), NOT idle. Draft decisions
  0037/0038 are superseded. Serialize builds and tests using their binaries
  on Windows: parallel CLI tests can lock linker outputs.
- Build (VS Developer PowerShell, UTF8 session):
  Run `chcp 65001` before configure and build/autoregeneration. Fresh CMake
  detection otherwise decodes this installation's UTF8 compiler include
  prefix as OEM850; the launcher correctly refuses the resulting metadata.
  A stale compiler-detection cache may retain that bad prefix; see107RE for
  the qualified recovery. Do not configure from a plain non-VS shell.
  `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=cl`
  then `cmake --build build` then `ctest --test-dir build --output-on-failure`.
- Python tests:
  `private/tooling-venv/Scripts/python.exe -m unittest discover -s tests/python`.
- Local inputs: ISO at the repository root; `private/fingerprint-check/CORE.GT4`;
  analysis ELF at `private/reconstructed/SCUS_973.28.elf`.
- Ghidra verification: `private/tooling/jdk-21` and
  `private/tooling/ghidra_12.1.3_PUBLIC`; full command in
  `docs/reverse-engineering/m6-disassembly.md`.

## Subagents

- Use subagents for exploration, research and parallelizable work. They start
  with fresh context: include every relevant path, constraint and expected
  output in the prompt.
- Review findings before acting or committing; never propagate an unverified
  claim.
- Persist knowledge that must survive: if a subagent produced something worth
  keeping, write it into the matching document.
- Only one writer at a time: serialize repository mutations across agents.

## Communication with the owner

- Speak Portuguese with the owner, in plain language, without unexplained
  jargon.
- Lead with what changed and what it means for the project. When a decision
  matters, state options, trade-offs and a recommendation.
- Do not ask the owner to review code; summarize behavior and evidence.
