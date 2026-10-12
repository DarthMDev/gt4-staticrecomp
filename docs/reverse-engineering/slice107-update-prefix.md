# Slice 107 — Prefix before the first message and frame synchronization

2026-10-12; baseline `5fc4e81`, branch `slice107-update-prefix`.
Evidence only. Runtime source, clock, interrupt return and model identities
are unchanged. The executable was rebuilt during final environment checks,
with its separate identity qualified below. Functional boot is not reached.

**Confirmed:** the first captured update iteration enters with current/synced
`3/3` in both executions. Both acquire the update lock, move the main thread's
waiter from receiver to pending, and issue the same message priority/wake
sequence. Between the qualified notify and release getters, the model advances
current to `4`; the reference remains `3`. The different sync inputs from106
can now be traced to this earlier, much smaller semantic interval.

**Unknown:** the exact counter writer, delivered interrupt's cause/ownership,
residual time/phase and first causal source of that extra transition. Equal
local counters and callers do not establish equal elapsed boot time. No
instruction/cycle conversion or replacement clock policy is accepted.

## Inputs and observation identity

Inputs were verified without modifying game manifests:

| Input | SHA256 |
|---|---|
| CORE.GT4, 2,020,861 bytes | `85d26aa8430154967b2633eede929286694ac39e99762527edcec365fd642ff9` |
| USA v2.00 ISO, 5,314,478,080 bytes | `67b6c0075837f3ae1132d608acf2858bf13b2dd62d6eae83dff76df02e4e824f` |
| Reconstructed native ELF, 6,123,004 bytes | `10f82e2231a51404b95682ed3ea81171100a1af2fefdeed3391943016c7c935c` |

The reference remains PCSX2 v2.9.114, despite its historical folder name
v2.9.94: executable `0506a196f5bc76d47f618e68d40519c766bbf5959b8aa88d5b91cee1dcb48037`.
Its official matched PDB is `99b483451a79da4237585aa9da2ffc9a844dcd128e8628921a0f12832336de6f`,
CodeView GUID `7833e0ae-b1fc-c437-4c4c-44205044422e`, age1; source commit
`aa7ab4306e269075784c7ac3eb4b45e6e6c53445`. The103/106 sparse host route,
CORE-loaded gate, original SYSCALL/ERET bytes, global layout and materialized
EE-dynarec boundaries are retained. Each recorded state checks that engine.
This is not guest word stepping or an interpreter observation.

All captures, BIOS copies, checkpoints, helpers and translated output stay
ignored. Public metadata records hashes and interpreted facts only:
`docs/inputs/slice107-update-prefix.json`, SHA256
`0adaac422a297a5ed8a3ff9708756554950e9f3c60329e95f0f66f460d023f2c`.

## Newly qualified native fields and callers

Independent reading/decoding of the pinned ELF qualifies these fields; names
are limited to their demonstrated uses:

| Region | Qualified layout/use |
|---|---|
| Queue `6D6774`, prefix64bytes | receiver head/tail `+08/+0C`; pending head/tail `+10/+14`; owner `+18`; depth `+20` |
| Pointed waiter,8bytes | next `+0`, thread `+4`; aligned, validated main-RAM address before dereference |
| Scratch `70002110..211B` | semaphore ID, owner, depth, three32-bit words |
| Scratch `70002050..2097` | previous106 current/synced64-bit counters and frame waiter head `2090` |
| Update object `6DE790`/global `617D80` | routine `1016E8`, quit `617D84`; qualified106 fields retained |

`4A0690` implements a recursive semaphore lock, distinct from frame Sleep:
`4A06B4` reads owner after GetThreadId; `4A06C4` calls WaitSema if owner differs;
`4A06C8` stores owner; `4A06DC` increments depth. Release `4A06F0` decrements
depth and, on zero, clears owner to−1 and signals the semaphore.

GetThreadRA`4A06B0` and WaitRA`4A06C8` are attributed to this update using
stack`+10=101748` and `+50=57871C`; stack`+0` preserves the caller's result.
The live `s0` at that getter is scratch base, not the saved update result.

The first notify route is `574EE8 → 574E00 → 5768D8`, then
`5767C0 → 576640`. `574E00` reads object`+28`: nonzero takes the notify route;
zero writes1 to`+2C`. No broader meaning is inferred for those fields.
`5768D8` moves a receiver node to pending when owner equals the current
thread and depth is nonzero. `576640` releases ownership and consumes pending
nodes before priority/wake. The qualified getters are:

| Getter RA | Additional identity |
|---|---|
| `57644C` acquire | `s0=6D6774`, stack`+50=576798`, `+60=574EFC` |
| `5768F0` notify | `s0=6D6774`, stack`+10=574F04` |
| `576674` release | `s0=6D6774`, stack`+30=5767D0`, `+40=574F0C` |

The final selector requires the queue pointer for those getters, and the
update outer caller for the lock getter/wait. It retains previous timer,
Sleep, priority and restored-main selections. Stop remains the first complete
post-result followup window; an earlier update getter does not stop the run.
WakeRA`576730` has already replaced `s0` with the next node, so it must not
use that getter-only queue predicate.

## Accepted reference and model observations

Reference `qualified2` completed40windows/280states; reduced2 completed the
same40windows/120commonstates. **120/120 common states are exactly equal**,
including cycles, registers, queue/node, lock, stack, timer and frame fields.
Only sequence, host thread and host RVA are excluded, as in106. Neither run
ended at the time/state limit. Both restored instrumentation and closed their
owned processes normally.

| Semantic point | Reference sequence/current/synced | Model service/current/synced |
|---|---|---|
| Main receiver SleepRA`57689C` | Q91, `1/0` | N1284, `2/2` |
| First update getterRA`4A06B0`, outer`101748` | Q105, `3/3` | N1292, `3/3` |
| Same update lock WaitRA`4A06C8` | Q112, `3/3` | N1293, `3/3` |
| Notify acquire getterRA`57644C` | Q119, `3/3` | N1299, `3/3` |
| Notify getterRA`5768F0`, receiver present | Q126, `3/3` | N1300, `3/3` |
| Intermediate private return `100`/PC`1604`, receiver still present | No such selected state | N1301, `4/3` |
| Release getterRA`576674`, same node pending | Q133, `3/3` | N1302, `4/3` |
| Boost / wake main1 | Q140/Q147, `3/3` | N1303/N1304, `4/3` |
| Sync raiseRA`1078F0` | Q161, `3/3` | N1316, `4/3` |

This table explicitly retains the earlier receive-phase difference; it does
not align absolute timing by matching the later number3. The first update,
ordinal notify, semantic stack and same main waiter establish the local
comparison. Full model/reference state equality is not claimed.

Reference receiver node `01FFFB90={next0,thread1}` is present at Q91/Q105/
Q112/Q119/Q126, moves to pending at Q133, and is consumed before Q140.
Model receiver `01FFFE30={0,1}` follows that same route at N1284/N1292/N1293/
N1299/N1300, remains receiver at N1301, then becomes pending at N1302.
These are different local stack addresses for the same demonstrated role.
Lock before/after is `{sema21,owner−1,depth0} → {21,5,1}` in reference,
`{87,−1,0} → {87,3,1}` in model. Numeric update IDs/semaphore IDs are not
equated across environments.

The106 result chain repeats: reference frame wait restores actual main's
receive continuation and subsequent RPC_BIND work; sync returns0, saved at
next getterQ273 with outer`101748`. Model sync returns1, saved at N1325,
then follows the qualified update-delay path. Neither a RUN flag nor a later
pre-service `v0` alone establishes ordinary progress or a service return.

## Cycle endpoint qualification and its limits

All80 recorded SYSCALL/ERET debit stores have the independently decoded
load/add2/store prefix, ending at return address+18. Existing inert102COFF
objects were inspected read-only with dumpbin14.44.35228.0: their first23bytes
match the new callers. Later branch displacements differ; old objects are
not claimed to decode whole107blocks. Source `iR5900.cpp` hash
`4bff040da4c4a4a4f7f6531c0eddaa3745ea2f4cd26dffda4968df6db80053da`
qualifies R5900_TEXTPTR at GPR9/offset90; store`+3B0` reaches cycle1088 and
compare`+3D0` reaches nextEvent1120. CPU base is RVA`2EA2880`.

Q126 raw cycle1810613441 becomes1810613443 at SYSCALL block debit;
ERET entry/return1810613506 becomes1810613508 at ERET debit and the
materialized event Q132. Next getterQ133 is raw1810613592. Differences are
67 raw-entry→event,65 post-debit-vector→event,84 event→next raw entry and
151 raw-entry→raw-entry. **These are endpoint differences, not integral
service cost, physical duration or a model clock conversion.**

## Read-only diagnostic qualification and rejected attempts

The private model CLI changes only provenance and the pre-handler service
callback, as audited in Driver145..155. It records121boundaries1220..1340;
all pointed queue/frame nodes and stack are restricted to aligned main RAM
before `contains`/reads. Fixed scratch/RAM reads cannot invoke MMIO. No
setters, extra services, changed clock or accounting are added.

Diagnostic10k is byte-identical to actual105production for context_memory
33,580,532bytes, kernel8,212bytes, banks496bytes, five model IDs and10,000
services. Provenance explicitly differs. Independent framing review confirmed
this without relying on the comparator. Work8,946,341 and full state also
match the interpreter; this is shared-model consistency, not PS2 fidelity.
No diagnostic checkpoint is resumed as production. The final model preparer
reproduces the qualified source exactly; final reference template is pinned.

Initial referencequalified1 selected other queues through generic getters,
reaching3004states/430entries/429complete before the message. It closed
normally, but is rejected as target evidence; its source/exe/logs are retained.
Initial model node guards used `contains`, which also includes MMIO windows.
That capture is retained and rejected; explicit RAM guards were added,
rebuilt and requalified. Publication also stopped on two manually created
profile hashes computed from LF text rather than saved Windows CRLF bytes.
Exact clone/content/CRLF checks justified correcting these generated profile
records. Owner/game input hashes and manifests were never changed.

25helpers and59capture/control artifacts are hash-recorded. Owner3files,
originalBIOS6 and all24isolated copies remain unchanged. Three owned
references closed normally (one rejected, two accepted). New private builds
are warning-free with their stated flags; whole-module model uses production
flags, without106's extra/W4. Historical106generated warnings remain retained.
Accepted observations used the unchanged105producer/module/libs. Those
identities and actual input copies are frozen in the private producer archive;
the final rebuild is a separately recorded artifact.

## Final build environment checks

The final build check was initially invoked outside the VS environment.
Autoregeneration cleared compiler detection/cache and failed to locate Ninja/C.
Restoring VS alone then decoded the compiler's UTF8 include prefix as OEM850
mojibake. The unchanged normalizer rejected that unknown metadata explicitly.
A separate fresh configuration with `chcp65001` detected the exact UTF8 prefix
for both C/CXX. Failed cache/compiler descriptions were archived, then only
those checked generated files were removed before fresh main configuration.

**Confirmed operational requirement for this installation:** enter VSamd64
and UTF8 codepage before configure, build or autoregeneration in that same
session. The launcher already decodes UTF8; CMake's initial detection also
needs the matching session. This is an environment requirement, not a universal
locale fix. Runtime/CMake/launcher source was not changed; RC remains unaudited.

Before rebuilding, old executable and all six accepted producer inputs were
copied and verified against original hashes. The module and main source stay
byte-identical. Rebuilt libraries have separate hashes; current executable is
`839adaa22f48ba674fc8f5f6c498299a9329598dedf23ca3ee507f1f1c76dfab`,110391296bytes.
Fresh warning-free build and54/54CTest pass in200.59s; Python97(91executed/
6skipped) passes in61.739s. Existing socket ResourceWarnings are retained.
Actual main header dependencies and header timestamp invalidation are verified;
restoring its timestamp leaves no build work.

Fresh current-producer10k full state/work matches the interpreter and the
accepted observer. A separate Python framing comparison against actual105
confirms five models,10kservices and all three serialized state sections
byte-identical. Complete checkpoint files differ **only in git_commit
provenance**, fromad703614... to5fc4e814...; a whole-file equality assertion
correctly stopped and was replaced by that explicit independent comparison.
No state exclusion or acceptance expression was weakened. New producer,
archived old inputs, operational logs and controls are pinned in final metadata;
its first provisional draft is retained. Captured-prefix review was accepted
independently; subsequent operational annex was checked with these controls.

## Next experiment

**Slice108:** instrument the bounded model notify→release interval to
identify the exact frame-counter store, interrupt owner/cause, CP0/INTC/DMAC
state and elapsed accumulator before/after service charge and dispatch.
Keep explicit diagnostic provenance and full-state qualification. Relate the
reference's independently qualified endpoints to its event/frame phase;
account non-service computation before considering any work→time policy.
Do not infer a quantum from67/151, undo correct preemption, force main wake,
or treat private service100 as idle. Matched1000us reference wait remains open.

Accepted capture commands from repository root, serialized:

```powershell
private/tooling/pcsx2-slice107-observer/host_observer.exe private/pcsx2/pcsx2-v2.9.94/pcsx2-qt.exe private/pcsx2/sstates/slice107-update-prefix/qualified2 private/pcsx2/sstates/slice107-update-prefix/qualified2/trace.jsonl
private/tooling/pcsx2-slice107-observer/host_observer.exe private/pcsx2/pcsx2-v2.9.94/pcsx2-qt.exe private/pcsx2/sstates/slice107-update-prefix/reduced2 private/pcsx2/sstates/slice107-update-prefix/reduced2/trace.jsonl reduced
cmd /c private\tooling\slice107-model-observer\build.cmd
private/tooling/slice107-model-observer/model_observer.exe private/fingerprint-check/CORE.GT4 --disc "Gran Turismo 4 (USA) (v2.00).iso" --services 10000 --strict-rpc --quiet --count-work --compare-interpreter --checkpoint-at 10000 private/pcsx2/sstates/slice107-update-prefix/model/observer-10000.bin
private/tooling/slice107-compare-observer-state.exe private/pcsx2/sstates/slice105-cdvd-init-response/verified-model/strict-10000.bin private/pcsx2/sstates/slice107-update-prefix/model/observer-10000.bin
private/tooling-venv/Scripts/python.exe private/tooling/slice107-publish-evidence.py
```

Reference build uses its own folder's build-observer.cmd. Helpers refuse
overwriting runs/archive/metadata. Those commands describe existing accepted
artifacts; preparation in a fresh directory is required for a new experiment.
