# Slice106 — first frame-sync result after message wake

Date2026-10-12; baselinecba6d81; branch slice106-frame-sync-result.
Status: accepted evidence; input/result split localized, earlier cause still open.
Evidence-only slice: production clock/IRQ/kernel/RPC and executable unchanged.

## Qualified target and fields

After the first message prefix from103, bracket sync107878 priority raise
RA1078F0 and restoreRA10799C. At restore s5 contains the boolean result copied
to v0 at10799C and saved by the update at101818. Controls6186DC and6186E4
are required: zero6186DC selects4A22C0 with negative6186E4; nonzero6186DC
keeps s5zero without that call. An already-reached signed frame difference
returns1; a path that waits returns0. Updating synced=current erases that
distinction from the counters after return, so capture inputs and s5.

The next actual update service is GetThreadId2F/RA4A06B0. In its128-byte
stack, +0 is saved update result and +10 is outerRA101748. Live s0 already
holds70002000, not the result. Require quit617D84zero and outer caller before
interpreting this as the next iteration. Later1000us branch is101750/101758;
CreateSema40/RA5AED68 has s1=1000; Wait44/RA5AEDC0 names a semaphore in a0,
and its s1 no longer supplies the requested microseconds.
Their stack+40/+50 contain578B20/101760. Numeric IDs are local per engine.

Independent read-only native ELF/PT_LOAD and gt4disasm audit qualifies these
addresses. CORE/nativeELF still match manifests85d26aa8.../10f82e22... .
Pinned R5900.h maps s0/s3 to GPR16/19 low64; source hash2f149a4d... .
MemoryTypes.h89847b1f... places Main128MiB before Scratch16KiB: observer
eeMem+08000000+2050 reads guest70002050. Current/synced are u64 at2050/2058,
headu32 at2090.72bytes include the head and one uninterpreted adjacent word.
Waiter is{nextu32,threadu32}, eight bytes at a main-memory stack pointer;
range/alignment are checked before host reads. A nonzero invalid head stops.

Object6DE790 is published at617D80; routine+38 is1016E8, arg+3C.64bytes is
only an observed prefix; wrapper accesses additional fields beyond it. Global
617D80eight bytes also covers quit617D84. Controls6186DCsixteen bytes covers
E4; other words have no newly assigned meaning. Captures remain private.

## Experiment and acceptance

Use inherited103/105 PE/PDB/layout/dynarec endpoint qualification with fresh
isolated profiles. New selector records the known receive, priority boost,
message wake and restore, then the first bracket and eventual frame Sleep
RA4A2248. Preserve actual restored-main continuation and its next block.
Stop at the first qualified next-update service after the result. Capturing
next raise or a timer wait is a fallback endpoint requiring explicit analysis.
No release Step Into, interpreter substitution or instruction-count claim.

Repeat with fewer observation points and compare all common state fields;
exclude only sequence, host thread and host address. Observe the same semantic
bracket in fresh model checkpoints with original service charges/preemption.
Align control branch, frame/synced/queue/waiter, caller/stack and main work,
not numeric thread/semaphore IDs across engines. If entry already differs,
report that the causal frontier precedes this bracket and trace earlier.
No guessed quantum, forced wake or policy change can follow from that mismatch.

Inputs/preservation checked before cloning: three owner files/six original
BIOS, reference PE/PDB/ISO/CORE/native ELF. Two profiles each have six copies.
New reference observer compiled MSVCW4 without compiler warnings. Live records,
repeats, closure and preservation results are accepted below.

## Confirmed live result and original-policy model

Thirty-one complete reference windows contain217states; reduced observation
contains93states, all93common states exactly equal. Comparison excludes only
sequence, host thread and host address; cycles, registers, RAM, stack, controls,
frame/waiter remain. Both terminate at the preferred GetThreadId endpoint,
not a fallback or timeout, and close normally. Wakea0=1 is checked in the trace.
Thread identifiers differ locally: update5reference/3model; do not align by ID.

| Semantic point | Reference | Original-policy model |
|---|---|---|
| First post-message priority raiseRA1078F0 | current3/synced3 | current4/synced3 |
| Controls6186DC/E4, quit617D84 | 0/1, quit0 | 0/1, quit0 |
| Frame waitSleepRA4A2248 | observed, actual main restored | absent in this bracket |
| Priority restoreRA10799C | s5=0, current4/synced4 | s5=1, current5/synced4 |
| NextGetThreadRA4A06B0 | stack+0=0, outer101748 | stack+0=1, outer101748 |

Reference Q98raise -> Q105frame Sleep -> Q203restore -> Q210next iteration.
At Q105 its head equals SP and node thread is5; Q111materialized endpoint
restores ordinary main PC5ADBC8/RA57689C. Main reaches actual work: GetThreadId
callers57644C/576674, RPC_BIND sem creation/SIF DMA and WaitRA5B18B4 selecting
idle81FC0 (Q146). This precedes the update's result restore. A RUN status by
itself was not used as evidence of main execution.

Fresh model records42pre-service boundaries1299..1340. RaiseN1316 sees4/3;
restoreN1324 sees s5=1; nextN1325 saves1 at stack+0 with outer101748. Timer
WaitN1328 has RA5AEDC0 and stack+40/+50=578B20/101760. The1000us interpretation
comes from the independently decoded caller/consumer; this diagnostic does
not log s1/a2 at CreateSema. Waita0=103 is a semaphore ID, not microseconds.

**Confirmed:** the boolean split reaches the actual next update iteration.
Entry current-synced is already0in reference and1in model. For the qualified
negative-interval branch, these inputs explain waiting/return0 versus already
late/return1. **Unknown:** the first causal source of those input differences.
The experiments align semantic object/caller/control branch, not absolute
elapsed frames or boot time. No defect in the sync instruction semantics,
replacement clock, IOP lifecycle change or matched1000us reference interval
is established. Next107 must inspect the prefix before this entry.

## Qualification of the read-only model observer

Ignored helper clones production tools/gt4boot/main.cpp. Only two changes:
diagnostic provenance prefix and on_service register/RAM getters/logging.
Driver callback precedes handler, accepted-service accounting and time advance
(src/ee/driver.cpp145..155). No setters, extra service, clock write or guest
work accounting is added. Original whole-module/runtime library hashes are
pinned in source-inputs.json. Diagnostic provenance explicitly says
slice106-read-only-observer; it is not presented as the production executable.

Its fresh10k checkpoint is compared to actual105production: compatibility,
service count, context_memory33580532bytes, kernel8212bytes and banks496bytes
are byte-identical; only provenance intentionally differs. Independent reviewer
parsed framing separately and confirmed that equality. Work8,946,341 and full
state agree with the interpreter too. This qualifies this observation interval,
not every possible observer or execution horizon. No diagnostic checkpoint is
resumed for production boot.

Diagnostic whole-module compilation added/W4 beyond production flags and
reported15409warnings in the unchanged generated header: C4702=15104,
C4102=303, C4189=2. Logs retained; no claim of warning-free diagnostic build.
No warning originates in the new logger. Production flags/executable/module
and source are unchanged, retaining54/54CTest and Python97(91/6) from105.
No new full-suite run is claimed for this evidence-only slice.

Fourteen helpers, captures, checkpoint, input/library/module/producer hashes
are recorded in docs/inputs/slice106-frame-sync-result.json, metadata SHA256
d0912b5e68a82b42af732e9378b8cd0eb0ceb47a767407d7a695d11bdedad848.
Production remains32687d1173d5cb31d1b9b5003437ceb38b2bd20a98f1a005045c4e425233b3fc.
Owner3files/originalBIOS6/copies12 remain unchanged; both owned references
closed normally. Payloads and translated/helper outputs remain ignored.

Commands from repository root, serialized reference runs:

```powershell
private/tooling-venv/Scripts/python.exe private/tooling/slice106-prepare-reference.py
private/tooling-venv/Scripts/python.exe private/tooling/slice106-model-census.py
private/tooling/pcsx2-slice106-observer/host_observer.exe private/pcsx2/pcsx2-v2.9.94/pcsx2-qt.exe private/pcsx2/sstates/slice106-frame-sync-result/qualified1 private/pcsx2/sstates/slice106-frame-sync-result/qualified1/trace.jsonl
private/tooling-venv/Scripts/python.exe private/tooling/slice106-prepare-model-observer.py
cmd /c private\tooling\slice106-model-observer\build.cmd
private/tooling/pcsx2-slice106-observer/host_observer.exe private/pcsx2/pcsx2-v2.9.94/pcsx2-qt.exe private/pcsx2/sstates/slice106-frame-sync-result/reduced1 private/pcsx2/sstates/slice106-frame-sync-result/reduced1/trace.jsonl reduced
cmd /c private\tooling\slice106-build-state-comparator.cmd
private/tooling/slice106-model-observer/model_observer.exe private/fingerprint-check/CORE.GT4 --disc "Gran Turismo 4 (USA) (v2.00).iso" --services 10000 --strict-rpc --quiet --count-work --compare-interpreter --checkpoint-at 10000 private/pcsx2/sstates/slice106-frame-sync-result/model/observer-10000.bin
private/tooling/slice106-compare-observer-state.exe private/pcsx2/sstates/slice105-cdvd-init-response/verified-model/strict-10000.bin private/pcsx2/sstates/slice106-frame-sync-result/model/observer-10000.bin
private/tooling-venv/Scripts/python.exe private/tooling/slice106-publish-evidence.py
```

Build reference observer from its folder using build-observer.cmd. Different
working directories for that build and root-relative preparation are deliberate.
