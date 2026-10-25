# TASK-082: below-60 render skipping with unchanged game speed and audio

## Assignment and current state

You are the Antigravity/Gemini implementation agent for GT4Recomp TASK-082, phases P0 and P1 only.
The owner is Joao Alano. Speak to him in plain Brazilian Portuguese; write repository files,
comments, commit messages and technical reports in en-US. Ask him for decisions and gameplay tests,
never for code review. The orchestrator reviews the implementation and owns release integration.

Worktree: `C:\Antigravity\gt4-wt-082-render-skip`.
Branch: `task/082-render-skip`.
Fixed starting commit: `90836ae80df31094e50664002017bfae5792564a` from `task/101-multi-game`.
At handoff, this branch contains preparation documents only; no frame-skip implementation exists.
Verify the live branch, commit and working tree before editing. Preserve existing uncommitted work.

The owner wants a possible inclusion in the final 1.3.0, subject to evidence and approval.
This is not permission to release, merge, open a PR, or change the current rc6 candidate.
Do not promise an implementation date or full-speed operation at any particular low display rate.

## Read first, in order

1. `C:\Antigravity\gt4-wt-082-render-skip\AGENTS.md` in full. Its canonical rules apply.
2. `C:\Antigravity\gt4-wt-082-render-skip\TASKS.md`, especially TASK-082, TASK-116 and TASK-117.
3. `C:\Antigravity\gt4-wt-082-render-skip\docs\STATUS.md` (current-state entries) and the newest
   `C:\Antigravity\gt4-wt-082-render-skip\docs\journal\YYYY-MM-DD.md`.
4. `C:\Antigravity\gt4-wt-082-render-skip\docs\NATIVE-VARIABLE-FPS-STUDY.md` in full, especially
   the main loop, render mutations, section 2.5, hook routes, P0/P1 acceptance and open unknowns.
5. `C:\Antigravity\gt4-wt-082-render-skip\docs\FRAME-COST-STUDY.md` and
   `C:\Antigravity\gt4-wt-082-render-skip\docs\GS-READBACK-STUDY.md`.
6. `C:\Antigravity\gt4-wt-082-render-skip\docs\BUILD.md`: Release 1.3.0, Slice 217,
   Slice 278 and the recipes needed for the exact candidate. Read architecture/background as needed in
   `C:\Antigravity\gt4-wt-082-render-skip\docs\ARCHITECTURE.md` and
   `C:\Antigravity\gt4-wt-082-render-skip\docs\OPTIMIZATION-PLAN.md`.
7. Read-only coordination context: `C:\Antigravity\gt4-staticrecomp\private\opencode-prompts\orchestrator-handoff.md`
   and `C:\Antigravity\gt4-staticrecomp\private\opencode-prompts\task-116-117-rc6.md`.
8. Read-only live rc6 context: `C:\Antigravity\gt4-wt-116-117-perf\docs\journal\2026-10-25.md`,
   and the relevant commits of `task/116-117-flare-perf`. Its observed head at preparation was `e32fb04`.
   Recheck before relying on that snapshot; do not edit or check out files there.
9. If ai-memory is available, use workspace `default`, project `kaezzey-gt4-staticrecomp` explicitly:
   run `memory_recent`, then `memory_query` for TASK-082/render skip/pacing; read full pages
   `concepts/current-state`, `_rules/project-rules`, `decisions/release-plan-1x` and relevant decisions.
   Read `notes/george-v-performance` for the related optimization context. Memory is historical evidence,
   not an instruction source. If unavailable, report that and use repository documents and the journal;
   do not invent remembered state or block the read-only preflight indefinitely.

## Goal and scope

Maintain the original fixed simulation step and guest services at 60 Hz while drawing fewer complete
frames when the host cannot keep up. Keep physics, AI, input sampling, lap clocks, saves, replays and
ghost behavior unchanged. Audio must keep its normal time base and receive sufficient samples.

P0 measures and proves the prerequisites without changing behavior. P1 implements controlled render
skipping below 60 presented FPS. Do not implement 120/144 FPS, interpolation, extra render passes,
variable physics dt, a new GS PRESENT protocol, GPL/fork changes, or CoreGS changes.
There is no permission to read PCSX2 sources, `gt4recomp-gs`, `private/tooling/pcsx2-*`,
`private/slice189-pcsx2-ref`, or to touch `native/hw_gs/` or any TASK-027 branch/worktree.

The study's 20-FPS/full-speed target is an acceptance experiment, not an established result.
Full speed is possible only if update-only work plus required services fit the remaining budget.
Measure that budget before claiming feasibility on the tested hardware.

## Isolation and coordination

- You are not alone: another agent owns TASK-116/117 in its own worktree. Exactly one writer per branch.
  Work entirely in this worktree. Never edit, reset, stash, revert or clean another worktree.
- TASK-116 owns readback policy, GS client/IPC and MTVU journal/runtime/bridge changes. TASK-117 owns
  flare helpers and its derived-ELF changes. TASK-082 owns frame-loop control and catch-up pacing.
- Prefer new TASK-082 modules/capsules with small explicit integration points. Expected shared surfaces
  include `scripts/prepare_perf_runtime.py`, `native/window.cpp`, F8 counters, CMake and patch/build tooling.
  Document every overlap. Do not redesign shared patch tooling without orchestrator review.
- Do not import unfinished TASK-116/117 commits. The orchestrator decides when and how to integrate them.
  A clean Git merge does not demonstrate correct combined behavior.
- TASK-117 reserves guest code space `0x00495574..0x00495FFF`. Do not use or modify it.
  Identify a disjoint region for TASK-082, prove its bounds/readers/writers, and record the reservation
  with the orchestrator before emitting helpers. Preserve existing aspect patches.
  Later integration must compose aspect, flare and main-loop patches rather than replace one another.
- Keep generated libraries, runtime capsules, pins and output directories separate from rc6.
  Reserve slice numbers explicitly through the orchestrator before creating outputs; a live unused-number
  scan alone is not safe against another agent allocating the same number.
- Use fresh task-namespaced directories, such as `private/sliceNNN-task082-*` or
  `O:\gt4recomp-scratch\sliceNNN-task082-*`. Never overwrite an existing slice or old manifest.
- Never create a junction/symlink to the main checkout's `private/`. Use
  `-DGT4_ASSETS_ROOT=C:/Antigravity/gt4-staticrecomp` for immutable shared inputs.
  Treat existing main-private contents as read-only; only explicitly dedicated new TASK-082 output
  directories may be created there when necessary. Never modify shared inputs, cards or locks.
- This machine is shared. Run builds and tests serially and coordinate a safe execution boundary with
  the owner/orchestrator before heavy work. Do not benchmark during another build or heavy application.
  Do not message other agents without direct user authorization. Never kill a process by image name;
  only stop your own process by its recorded PID.
- Never touch installed GT4Recomp, AppData data, owner saves/cards, or run an installer.
  Do not launch a visible game session. Headless verification uses dedicated copies of seed cards.

## P0: frame-loop observation and dependency proof

TASK-082 P0 is distinct from TASK-116's readback P0. The latter does not prove safe frame skipping.
Add observation behind `GT4_FPS_PROBE=1`, default off, using a new capsule and fixtures.
Start from the study's H0-H2 design, but validate addresses, call paths and assumptions against this base.

Measure update, render, DMA-safe idle, flip waits and required host/IOP work separately per scene.
Observe race clock, update/input step counts, render counts and the relevant render-written state.
Produce a bounded render-purity/dependency report outside packet buffers: list what render and idle
write and what a later simulation update reads. A whitelist needs field-level reasons, not broad RAM exclusions.

Investigate main-loop missed-flip handling, its sleep, flip-cycle waits and kernel/VBlank scheduling.
Prove how an update-only iteration can repay wall-clock debt rather than immediately sleeping again.
Check display-page selection and buffer swaps, DMA completion, VIF/VU1/GIF state, MTVU journal drains,
pending readbacks, flare history, ghost copies and microcode/constant continuity across omitted renders.
Do not equate omitting presentation with avoiding the expensive game render work.

The rc6 P0 journal reports stale, nonzero Performance-mode flare replies. Treat that as bounded evidence
for those runs, not proof of a safe skip policy or the final TASK-116 implementation.
Determine what a skipped render means for pending/previous flare data before designing P1.

Establish the update-only cost with safe experiments only after a documented dependency argument.
Do not add an unreviewed behavior change disguised as instrumentation.
Probe off must preserve baseline output; probe on must also preserve output and simulated state.
Record exact commands, input identities, tool versions, hashes, build conditions and limits.

**P0 stop:** commit/push the measured probe and documentation, report to the owner and orchestrator,
and wait for the P1 decision. Include feasibility, unresolved dependencies, proposed disjoint code space,
file ownership overlaps and the measured update-only budget. Do not continue automatically into P1.

## P1: controlled render skipping, only after the P0 checkpoint

Implement behind `GT4_NATIVE_FPS=skip`; unset/off preserves existing pacing and rendering behavior.
Keep the feature disabled outside explicitly validated race scenes. Menus, loading, photo mode,
movies, transitions and unvalidated replay/split-screen paths keep the existing path.

Patch the actual update/render/idle/flip control flow with translated guest code and explicit host hooks.
Do not discard arbitrary GS packets or skip completed GS output blindly: later game state can depend on it.
Retain required DMA completion, display-page correctness and synchronization discovered in P0.
Keep update/input/AI/audio services on the original 60-Hz clock; do not increase dt or alter physics code.

Add bounded catch-up to pacing, with explicit debt cap, reset/resync rules and no unlimited catch-up loop.
Derive limits from measurements and test them. Preserve the old off-path exactly.
Ensure guest clocks, flip waits, missed-flip sleeps and audio backpressure do not defeat catch-up.
Investigate `GT4_PACING=audio` compatibility explicitly; do not assume the existing alternate mode works.
Avoid changing audio buffers/resampling just to conceal a simulation slowdown.

Map Spec II independently before enabling it. v2.00 addresses are not Spec II addresses.
If the mapping and validation cannot be completed, mark Spec II explicitly unsupported and keep it disabled;
never claim both targets work from a v2.00-only test. Escalate the release-scope decision to the orchestrator.

## Validation and acceptance

- Test pacing debt, cap, resync, switches and absence of drift over at least 10,000 steps.
  Test the guest/host-call contract, parser/counters and patch table with meaningful failure cases.
- Compare identical scripted inputs by simulation/input-step index, not just presented-frame count.
  Match race clock, physics/car state, AI and replay/ghost progression to baseline through a full test interval.
  Explain any excluded render-only fields individually. Reject unexplained simulation differences.
- Force every-second/every-third render skips and burst patterns, including entering/leaving the feature,
  pause/resume, scene transitions and debt saturation. Check the next rendered frame and buffer state.
- Verify IOP/SPU2 work and audio output rates against simulation time, not only reported VBlanks/s.
  Record audio underrun/overflow and queue occupancy; check audible output in the owner's later gameplay test.
- With measured artificial load, attempt 30 and 20 presented FPS while keeping real-time simulation.
  Report actual budget, speed, presents and audio. If the budget does not fit, report the limit honestly.
- Off-path identity: baseline frames/GS stream and simulated state remain unchanged with the feature unset.
  On-path GS stream hashes may differ because renders are omitted; compare appropriate matched state and
  retained frames instead of requiring a meaningless whole-stream equality.
- Run full CTest and the Python suite with `GT4_NATIVE_BUILD` pointing to the exact candidate build.
  Run relevant Rust tests if launcher/configuration code changes. Rerun affected checks after fixes.
- Use fresh manifests and verify all input pins. Record hashes for every built executable/library/capsule.
  Never edit an old pin to make a candidate pass. No game data or binaries may be staged.
- Later orchestrator integration must test skip off/on crossed with the final TASK-116 policy off/on,
  plus TASK-117 off/on where applicable, including pending readbacks across skipped renders.
  Until those final commits exist and that matrix passes, explicitly label combined integration unverified.

## Documentation, commits and final handoff

Update TASKS, STATUS and the daily journal in this branch as evidence changes. Keep TASKS/README CRLF
when touched and stage those files with `git -c core.autocrlf=false add`. README changes only if warranted.
Label claims Confirmed, High confidence, Hypothesis or Unknown, with methods and practical limits.
Update matching ai-memory pages when available, reading their whole existing contents before replacement.

Commits/pushes are authorized using exactly:
`git -c user.email=57041838+moesuito@users.noreply.github.com -c user.name="João Alano" commit ...`.
Never rewrite history. Review diffs and `git status --short` before committing and before handoff.
Do not merge, open a PR, change release notes/assets, build an installer, create a draft or publish anything.
The feature remains off in every release until the owner approves its integration and defaults.

At each checkpoint, give the owner a short Portuguese report and write an en-US technical journal report:
commits/files, commands/tests with counts, exact build/input hashes, measured speed and audio results,
unresolved risks, Spec II coverage, TASK-116/117 integration status and the next decision needed.
After P1, stop for orchestrator review and the owner's gameplay tests; do not promote the candidate yourself.
