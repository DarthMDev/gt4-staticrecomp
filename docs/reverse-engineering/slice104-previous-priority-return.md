# Slice104 — fix the previous-priority return, preserve the real frontier

Date:2026-10-12. Baseline1eb31a8, branch slice104-priority-return.
Root sole writer; independent contract/code/evidence review read-only.

## Confirmed change

Slice103's pinned BIOS reference24->1 returned24;1->24 returned1.
The game saved that return at576710 and used it for restoration at576760.
Production now captures the target's previous priority before mutation and
writes v0 before dispatch. This fixes the observed zero-restoration error
without changing scheduling, interrupt preemption or time advancement.

Kernel semantic identity is now3; time3/interrupt4/RPC1/translation2 and
GT4CPT3/GT4KERN2 layouts are unchanged. Actual103 stop1303 checkpoint with
kernel2 is refused with `kernel (file 2, this binary 3)` before restore.
Provenance is diagnostic; compatibility gates behavior, not a commit string.

## Qualification boundaries

The same handler is registered for normal29 and interruptive-2A. Independent
original reverse-engineering [PS2Tek pinned source](https://github.com/psi-rockin/ps2tek/blob/1c9166066c3ab9ad089a4d12088acaa9476df4b6/index.html#L4873-L4879)
corroborates the old-target-priority return of iChangeThreadPriority.
Commit1c9166066c3ab9ad089a4d12088acaa9476df4b6, SHA
2227bab31bcfecf8f25a963b8cd74ebcc26a95415925c868260b69af26f81532,
278696bytes; independently checked against the published site and stored
privately. This is **high-confidence documentary** evidence for additional
targets/variants, not a new live BIOS qualification. Normal current-thread
cases remain **confirmed** by103. Scheduling/TH_SELF/dormant differences
remain explicit debt; this return-only change does not validate them.

## Tests and commands

New tests first failed on the original return0 implementation: seven checks
exposed nonzero boost/restore, associated unchanged-error-state expectations,
saved return across preemption, kernel serialization and resumed caller.
The error checks were consequential failures after restoring the wrong zero,
not independent evidence that the original error branch was incorrect.

Green tests cover24->1->24, oldzero, invalid priority/unknown thread without
mutation, interruptive registration returning a different READY target's77
instead of the caller's24, preempted caller PC+4/v0=24, incoming registers,
kernel save/load and resumed return. A checkpoint test explicitly rejects
kernel2. No existing acceptance expression is weakened.

VS2022 Developer environment, Ninja Debug/MSVC1944:

```text
cmake --build build
ctest --test-dir build --output-on-failure
private/tooling-venv/Scripts/python.exe -m unittest discover -s tests/python
```

**53/53 CTest,91.81s** (including whole-program compile43.52s);
**91 Python collected /85 executed /6 skipped,61.568s**, existing socket
ResourceWarnings only. An earlier full CTest was mistakenly run without
VsDevCmd; whole-program compile failed C1083cstdint and dependent tests did
not run. That invocation is preserved, rejected as gate evidence, and fixed
by the explicit Developer-environment repeat. No compiler warnings found.

The unchanged90k gate matches full registers/RAM/kernel/device banks and
**22,566,319 completed instructions /90,000 accepted services**. Fresh10k
also matches **8,946,331 completed /10,000 services**. Shared-model agreement
does not establish independent PS2 environment fidelity.

## Fresh boot result — error fixed, boot frontier persists

ISO67b6c007... and CORE85d26aa8... are freshly verified against public
manifests. Production SHA
8e44209c69238a632cfa67684703e5e3082d7ee83c13bec333146e296555f1c7.
Its embedded commit6fe1db2... is the older CMake-configured provenance;
source baseline1eb31a8 plus104 diff/tested binary hash identify this change.
Do not mistake the stale diagnostic string for the runtime's semantic ID.

Nine original-policy stops1302/1303/1304/1305/1327/1328/1330/10000/100000:

| Stop | Confirmed observation |
|---|---|
|1302|Boost a1=1, old update priority24, s2=-1|
|1303|Wake a0=1; s5=24|
|1304|Restore caller576760, s2=s5=a1=24|
|1305|Update RUN24, root READY64|
|1327|WaitSema103 caller5AEDC0; root saved receive5ADBC8|
|1328|Root RUN **in timer callback** PC5ADCD4/RA5AEF68; update WAIT24|
|1330|Update selected24; root READY64, saved receive5ADBC8 unchanged|
|10000/100000|Still three threads, root READY64/update RUN24, two RPC pairs, zero GIF payload|

Frame/synced counters are526/524 at10k and5926/5925 at100k. Compared with
103, the priority-return error is causally closed; the earlier timing frontier
persists. These snapshots do not re-measure96's270/270 before-instruction
exclusion, and RUN inside a callback is not ordinary root work. The corrected
IRQ preemption is preserved. No functional boot/menu/image is claimed.

## Strict boot exposes an independent unresolved RPC

A fresh `--strict-rpc` run refuses the preexisting unmodeled pair
SID80000592/function0 at5AE064, thread1, send4/recv16, process exit1 with no
checkpoint. Original-policy diagnostic runs above retain existing default
unknown-RPC behavior: they are not a strict functional boot. Inventory shows
two such empty-result calls. This is explicitly unresolved, not new accepted
service success and not hidden by the green differential.

Next experiment: identify this protocol from pinned original source and guest
call path, observe real reference response/completion and consumer, then
implement only independently justified behavior. In parallel, the update's
first frame-sync result remains a causal timing observation target. Do not
invent success bytes, force wake or choose a guessed quantum.

Read-only next-slice source/ELF audit identifies the SDK CD_SERVER_INIT:
guest routine580CD8, client874FA8, send mode875000, reply6570C0.
Sync SifCallRpc caller580E40 waits at5B1B74; DeleteSema caller5B1B7C is
a candidate completion endpoint **after** the waiting thread resumes.
An initial ERET that only selects idle cannot be counted as init completion.
The guest consumes reply+0C/+04/+08 at580E98/580EA4/580EB4; this routine
does not consume word0. Live response/consumer return remain unknown.
Do not copy open-source SDK server versions into the pinned Sony service.

## Artifacts

`private/pcsx2/sstates/slice104-priority-return/`: strict failure, nine private
checkpoints/contexts, original-policy outputs, actual old-checkpoint refusal,
PS2Tek source and build/test logs; eight archived helpers. No new reference
process or owner INI/save/BIOS mutation. Public metadata only:
`docs/inputs/slice104-priority-return.json` (8a7a141a...); full hashes and
commands retained without payload bytes. Decision0045 accepts this contract
after gates and review; stable functional boot objective remains active.
