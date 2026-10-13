# Multi-Model Engineering Workflow

2026-10-13. Planning companion to [Native Port Plan](NATIVE-PORT-PLAN.md).
No external agent dispatch, provider purchase/configuration, scheduler or
unattended execution is started by publishing this workflow.

## 1. Use inexpensive workers behind explicit contracts

An economical model can do substantial research and implementation when its
task has bounded inputs, a known interface and objective acceptance controls.
Longer runtime is acceptable; repeated plausible semantic errors are not.
Reserve a stronger coordinator/reviewer for ambiguity, reverse engineering,
timing, numerical semantics, concurrency and subsystem cutovers.

Official OpenAI guidance describes Luna as suited to scoped/high-volume tasks
and GPT-6.1 Sol to complex technical work. It also recommends comparing models
on representative inputs. This is selection guidance, not evidence that any
model has solved a GT4 port or that an account has unlimited usage.
[Model selection](https://developers.openai.com/api/docs/guides/model-selection),
[GPT-6 Luna](https://developers.openai.com/api/docs/models/gpt-6-luna), accessed
2026-10-13. Check actual product/model availability and usage before dispatch.

Gemini 3.8 Flash, DeepSeek V4.1 Flash, Claude Opus 5.5 and Claude Haiku 5.5 are
owner-mentioned candidates. Their exact availability, account terms and relative
GT4 performance were not independently qualified in this planning pass. Do not
assume equivalence between providers or purchase a model on this plan alone.
OpenCode and Claude Code are execution environments; select and record the
actual model behind them, its tools, settings and task permissions.

### Initial routing hypothesis

| Task class | Initial assignment | Promotion/acceptance rule |
|---|---|---|
| File/symbol inventories, provenance, coverage tables | GPT-6 Luna or available economical Flash/Haiku worker | Verify every path/address/source; semantic inference labeled |
| Documentation from verified findings, bounded tooling | Qualified economical worker | Reviewer checks facts; deterministic fixtures for changed behavior |
| Decoder/parser, small native routine with approved contract | Best worker from calibration | Invalid-input and independent result fixtures; semantic review before integration |
| Render hooks, camera conventions, VU math, callback/state ownership | GPT-6.1 Sol or separately qualified stronger specialist | Reviewer derives contract independently; escalate unresolved evidence |
| Temporal architecture, graphics feedback, concurrency, service removal | Strong coordinator plus independent specialist | Explicit dependencies, negative controls and removal gate |
| Build/evidence packaging | Single designated integrator | Serial build/test, immutable outputs, verified manifests and exact commit |
| Live gameplay qualification | Owner | No model operates the game; reviewer records observations and limits |

Use Luna medium initially for bounded engineering tasks and lower effort for
simple extraction if it passes. Use stronger reasoning only when uncertainty
requires it; more effort is not a substitute for source evidence or tests.
These settings are hypotheses to calibrate, not mandatory provider rankings.

## 2. Roles and ownership

1. **Coordinator:** creates ready tasks with dependencies/acceptance, resolves
   architectural decisions, reviews evidence and maintains the migration ledger.
2. **Workers:** read-only research or isolated patch proposals. Each receives
   exact paths and constraints; they do not change shared integration files.
3. **Independent reviewer:** re-derives risky claims from sources/controls in a
   separate context. It must not approve solely from the worker's explanation.
4. **Integrator:** sole writer to the active integration checkout. Applies
   accepted patches, serially builds/tests, records candidate pins and publishes.
5. **Owner:** selects hardware/content priorities and performs live gameplay tests.

Workers can write in their own isolated worktree only when a task explicitly
allows it; that produces a proposed patch, not a merge. The integration checkout
still has one writer. Do not share mutable linker outputs or producers.
Existing OpenCode performance work retains ownership of its files until handed
back. A renderer task cannot overwrite or repin those changes.

Suggested starting capacity: one coordinator, two independent workers and one
reviewer, subject to actual tool/account capacity. Scale only after accepted-task
throughput demonstrates spare reviewer capacity. Do not spawn idle agents simply
to fill slots or make overlapping investigations appear parallel.

## 3. Model qualification before long runs

Give candidate models the same bounded inputs and constraints in isolated
contexts. A useful initial pilot contains six tasks:

- Two evidence tasks: exact EE producer inputs/options; symbol/address/caller map.
- Two implementation proposals: packed vertex decoding and texture/palette
  conversion from an already approved specification, including malformed inputs.
- Two reasoning tasks: matrix convention proof and a state-changing callback
  review. These expose escalation behavior, not just code-writing fluency.

Record provider/model/settings, input revisions, token or provider-credit usage,
elapsed time, first-pass acceptance, review corrections, failed controls,
unsupported claims and regressions. Compare **cost per accepted task**, including
review/rework, rather than tokens per response. Run consequential controls outside
the worker's self-assessment. A small pilot is provisional and must be reassessed
as tasks become more semantic or complex.

Do not invent pass rates, speedups, completion forecasts or token budgets.
Select the least expensive setting that meets the task's quality bar. Keep a
fallback route to a stronger model for uncertain contracts. A strong model's
output also requires validation.

## 4. Task contract

Every executable task must contain the following fields. The task author fills
the paths, hashes, allowed actions, controls and budget before it becomes Ready.

```text
Task ID and phase:
Goal and explicit non-goals:
Authority: read-only | isolated proposed patch | integration | offline build/test
Approved baseline commit and input/source pins:
Dependencies and required completed gates:
Input paths and exact allowed output/files:
Prohibited paths/actions:
Required references and licenses:
Function/data preconditions, postconditions and side effects:
Expected deliverables:
Acceptance controls, independent expected results and failure behavior:
Claim labels: Confirmed / High confidence / Hypothesis / Unknown
Tool/model settings and task-specific token/time/retry budget:
Escalation triggers and required evidence:
Owner-operated gameplay checkpoint, if any:
Stop conditions:
Assigned reviewer and integration owner:
```

Expected values must come from an independent fixture, specification or reference,
not simply a copy of the candidate implementation. Never loosen a failing gate,
change baseline pins to match an unexplained result, or accept unsupported
instructions/services silently. No host undefined behavior substitutes for guest
wrapping/flags/memory semantics.

### Reusable worker instruction

```text
Read docs/STATUS.md first, then the task's named references. Follow AGENTS.md.
Execute only the attached task contract. If a required semantic contract is
missing, return the unknown and the smallest evidence needed to resolve it.
Do not invent permission to launch gameplay, profile, modify another task's
files, import unqualified code, or advance to the next migration phase.

Keep all reports, prompts and new comments in en-US. Return findings with
source revisions, paths, addresses/offsets, methods, limits and claim labels.
For an allowed patch, supply readable code, independent controls, exact test
results and the proposed diff. Mark tests not run and owner checks pending.

No Computer Use, GUI automation, simulated input or scripted game navigation.
Do not touch owner cards, backups, locks, frozen producers or active sessions.
Do not message external tasks/people or configure recurring runs without the
owner's explicit authorization. Follow the contract's retry/escalation limit.
Do not claim completion solely because code compiles or a model agrees.
```

## 5. Initial ready-to-specify backlog

The owner authorized bounded P0/P1 work in slice 116. R00's initial inventory is
reviewed; R01's producer is built but full P0 runtime/coverage gates remain open.
R02/R03 have initial static evidence, and R04 has a local side-effect ledger with
real-car dependencies still open. Slice 117 adds offline callback recovery and
changed-unit compilation; candidate integration and live coverage remain open.
R05-R09 remain proposed. The table is not an
instruction to execute the entire backlog unattended.

| ID | Deliverable | Dependencies | Suggested role | Completion evidence |
|---|---|---|---|---|
| R00 | EE generator/tool/options and private preparation inventory | None | Economical researcher | Every recipe step has known inputs/outputs; unknowns listed |
| R01 | Fresh producer/build design with separate baseline/candidate pins | R00 | Worker + coordinator | No prebuilt-only step, no frozen overwrite, explicit failures |
| R02 | ModelSet2/camera/car/race symbol qualification table | None | Economical researcher | Local decomp revision, addresses, generated mappings/callers and version limits |
| R03 | Model/texture/render-command format ledger | None | Economical researcher | Packing, fields, callbacks/VM, provenance, unknowns and independent fixture design |
| R04 | One-car hook pre/postcondition and side-effect contract | R02/R03 | Strong specialist | Mutations, callback cadence, ownership, completion and composition boundaries |
| R05 | Coordinate/texture/primitive qualification specification | R03 | Worker + semantic reviewer | Independently derived matrix/depth/packing/alpha expectations |
| R06 | Minimal scene/resource/snapshot interface | R04/R05 | Coordinator | Explicit IDs, lifetime, ordering, concurrency and reference separation |
| R07 | Supported-content/manual qualification matrix | None | Economical researcher | Coverage tied to active gates; offline viewer and live integration distinguished |
| R08 | License/provenance adoption review | None | Economical researcher | Revision/license per candidate; no incompatible implementation copied |
| R09 | PS2 dependency retirement ledger | R02 plus service inventory | Worker + coordinator | Each dependency has caller, behavior, native replacement and removal test |

The [EE producer](EE-PRODUCER.md) and [render contracts](RENDER-CONTRACTS.md)
record this first package's evidence and remaining gates. Continue bounded P1
qualification before P2/P3 runtime replacement. Do not give an overnight
worker the prompt “make the whole renderer native.” Give one measurable contract.

## 6. Unattended queue policy for a future configured run

Use states **Draft -> Ready -> Running -> Review -> Accepted -> Integrated**;
Blocked/Failed are explicit outcomes. A dependent task becomes Ready only when
its required gate is Accepted/Integrated as specified. Persist task state and
evidence to avoid rediscovery after context loss or process restart.

- Execute only ready tasks within an owner-approved scope and configured budget.
  Stop on usage/spend limits, dependency changes, repeated failures or unsafe paths.
- Allow at most one corrective retry after an actionable control failure;
  repeated mismatch or semantic uncertainty escalates with a minimal reproducer,
  source evidence and rejected hypotheses. Independent research may continue.
- Batch independent read-only tasks. Keep builds/tests and integrations serial.
  Dependency/code changes invalidate stale reports until rechecked against HEAD.
- Do not merge self-approved patches. Review checks actual diff, independent
  controls, licensing, unsupported-path handling and build/source provenance.
- A successful offline build can produce a candidate. Live gameplay gates remain
  pending until the owner tests it. No automated navigation or input injection.
- Do not terminate an owner session, reseed a card, reuse a producer identity,
  force-push history or repin inputs as an error workaround.
- Checkpoint after each completed contract. Report meaningful milestones,
  failures and decisions needing owner input; do not generate progress noise.

The scheduler/provider mechanism is a separate setup task. Running 24/7 does
not imply unlimited usage, continuous supervision, or autonomous acceptance of
live gameplay. This plan configures no automation and dispatches no external job.

## 7. Integration checklist

- Baseline/revisions and task authority match current repository state.
- Single writer owns the affected files; external OpenCode changes preserved.
- Changed behavior has meaningful automated controls and reviewed semantics.
- Candidate build/tests run serially in a fresh private directory when required.
- Runtime/asset/library/shader pins identify that producer without changing old pins.
- Unsupported cases explicit; current acceptance checks not weakened.
- Owner gameplay/hardware gates identified as passed or pending, with limits.
- STATUS, journal, decisions/evidence and attribution updated in en-US.
- Inspect status and exact staged diff; commit only the task's approved files;
  push to origin, never the read-only upstream. Keep main green and preserve history.

Measure progress by accepted contracts, retired dependencies, qualified gameplay
coverage and performance on the selected hardware. Neither more agents nor more
generated code proves that the game is a portable, optimized native port.
