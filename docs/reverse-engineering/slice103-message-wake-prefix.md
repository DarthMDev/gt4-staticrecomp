# Slice103 — first message wake and next blocking reason

Date: 2026-10-12. Baseline b29eb99. Branch slice103-message-wake-prefix.
Status: closed observations; production unchanged. Root sole writer;
independent subagent review is read-only.

Slice102 qualifies a receive SleepThread (caller57689C, stack1FFFB90,
object6D6774), message WakeupThread a0=1 and later frame SleepThread
that restores the same receive context at5ADBC8. Its next ordinary work
and block were unknown at this baseline. Model96's analogous message wake precedes the
already-late frame/1000us path that excludes root before any instruction.

Fresh model checkpoint stops captured actual next-call arguments/context.
A bounded reference pass followed the restored receive context until its
next blocking service. Semantic routine/caller/stack/message/frame are
compared; IDs are local evidence.

No production clock, kernel, priority, checkpoint or acceptance change.

## Fresh model observations

Confirmed: nine fresh stops1283/1284/1302/1303/1304/1305/1326/1327/1328
using unchanged production4c1dd903... and verified ISO/CORE. Stops1303/1328
also compare successfully against interpreter. Complete checkpoints private.
The inspector uses production checkpoint/snapshot/kernel deserializers.

At1302 message ChangeThreadPriority29 has a0=3,a1=1,caller576710,
update priority24 and s2=-1. At1303 WakeupThread33 has a0=1,s5=0;
at1304 restore ChangeThreadPriority29 has a1=0,s2=0,s5=0,caller576760.
At1305 update priority0/root READY64. Object6D6774+1C remainsFFFFFFFF.
At1327 timer WaitSema103 follows;1328 root selected only inside the timer
callback, with its saved ordinary receive continuation5ADBC8 unchanged.

Confirmed by ELF words and the new reference below: message-release576640 saves ChangeThreadPriority's
v0 into s5 at576710 and selects it for s2 when old s2=-1 at576748.
Production kernel.cpp writes v0=0 on successful priority change. This explains
the model's restore argument. Runtime correction is deferred to104.
Reference102 restore a1=24 is confirmed. Exact first-prefix re-extraction
77..83 shows v0=18 at all ERET/event endpoints of the first priority change,
and91..97 shows restore a1=18 / return1. The earlier A4 suspicion confused
v1 (BIOS dispatch index) with v0 (service return); it is withdrawn.
The new reference includes s2/s5/message bytes and main's next block.

## Independent reference result

Confirmed: fresh pinned real-disc dynarec capture has **20 complete windows /
140 states**, followed by an independently started reduced observation with
**20 windows / 60 states**. All60 repeated states match every recorded guest
field exactly, excluding only sequence/thread/host_rva. Both processes close
normally after the main's next blocking dispatch; no input is injected.
The observer inherits102's matched PE/PDB, engine/layout, flush and materialized
cycle endpoints. New fields are low64 GPR s2/s4/s5 and64 message bytes.
No word stepping, host elapsed timing or instruction counts are inferred.

First selected message prefix, decimal priorities:

| Point | Reference | Model fresh stop |
|---|---|---|
| Raise current update to1, caller576710 | Q77 s2=-1,s4=0, object+1C=-1 | N1302 s2=-1, object+1C=-1, old priority24 |
| Return from priority change | Q80..83 v0=24 | Kernel writes v0=0 |
| Wake main, caller576730 | Q84 a0=1, s2=-1, s5=24 | N1303 a0=1, s2=-1, s5=0 |
| Restore, caller576760 | Q91 s2=s5=a1=24; Q94..97 return1 | N1304 s2=s5=a1=0; N1305 priority0 |
| Frame SleepThread return | Q104 PC5ADBC8,RA57689C,SP1FFFB90 | Corrected model excludes saved receive continuation at5ADBC8 |

The causal **return -> s5 -> MOVZ s2 -> restore a1** is confirmed on both
paths. Reference s4=0 skips the object-priority load; therefore24 in this
prefix comes from the syscall return. Model zero is not a guessed frame
effect or wake-target mismatch. Reference updateID5/modelID3 and stack/frame
values differ; we match semantic callers and message path, not absolute phase.
This is an earlier service-return divergence, not yet proof that correcting
it alone resolves the entire boot frontier.

The native model's N1328 RUN root is executing the timer callback at5ADCD4,
RA5AEF68. Its saved ordinary receive PC remains5ADBC8. RUN status alone must
not be described as ordinary main progress; slice96 supplies the quantified
before-instruction exclusion. No new zero-instruction census is claimed.

## Main's next block in the reference

After Q104 the restored main reaches GetThreadId at callers57644C/576674,
CreateSema at5B1854 (returns25), SifSetDma77 at5B0D8C and WaitSema25
at5B18B4. Q136..139 selects idle81FC0. This is the synchronous RPC_BIND
completion path: native ELF routine5B17D0, modea2 bit0 clear, creates a binary
semaphore, sends SIF command80000009 and waits client+8. It is not the
update's1000us timer wait or a frame wait. Payload and stack bytes stay private.
No new RPC reply or boot completion is established by stopping at this wait.

## Qualification, checks and limits

Inputs are the unchanged manifests ISO67b6c007..., CORE85d26aa8... and
native ELF10f82e22...; reference0506a196... (actualv2.9.114), source
aa7ab4306e269075784c7ac3eb4b45e6e6c53445, PDB99b48345.../GUID
7833e0ae-b1fc-c437-4c4c-44205044422e age1. MSVC14.44.35228.0,
VS17.14.34, Python3.14.5. Exact hashes/size/commands and helper identities:
`docs/inputs/slice103-message-priority.json` and private model-runs.json.

The official PS2SDK source pin2c670453980fcc3fe46ead6399b730b12c8556eb
[header](https://github.com/ps2dev/ps2sdk/blob/2c670453980fcc3fe46ead6399b730b12c8556eb/ee/kernel/include/kernel.h)
and [assembly](https://github.com/ps2dev/ps2sdk/blob/2c670453980fcc3fe46ead6399b730b12c8556eb/ee/kernel/src/kernel.S)
declare an s32 return/direct syscall, but do not document its meaning.
They cannot replace the observed return evidence. Normal current-thread
successful24->1->24 calls are confirmed; error cases, dormant/other threads
and the interruptive variant remain independently unqualified.

Nine fresh model stops and two differential checks are green. Production
gt4boot SHA4c1dd903... unchanged; no src/include/scripts/tests/CMake diff.
Full existing gates53/53 CTest/Python91(85 executed,6 skip) are inherited
from unchanged production, not re-run or weakened. Three original owner
INI/save files, six original BIOS and12 isolated copies match preservation
hashes. Ten helpers archived privately; both references closed normally.
Only distributable hashes/derived observations are tracked.

Independent read-only review confirms both causal chains, next RPC_BIND wait,
and the distinction between the root callback and ordinary continuation.
Next slice104: correct the evidenced priority-return contract with meaningful
nonzero-return/preemption tests and checkpoint kernel semantic identity;
qualify scope before extending unobserved service cases. Run a fresh boot
to assess actual causal progress. Keep clock/preemption unchanged.
