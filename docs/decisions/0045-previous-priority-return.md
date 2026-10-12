# 0045 — Return the target's previous priority before dispatch

Date:2026-10-12, slice104. Status: accepted return-only correction.

Slice103 independently observes successful normal current-thread24->1
returning24 and1->24 returning1; the game consumes that value in message
priority restoration. Return the target's previous priority rather than
unconditional zero. Capture it before mutation and write v0 before dispatch,
so a switched caller's saved context retains the return.

The shared0x29/-0x2A handler adopts this return contract only. Independent
PS2Tek original reverse-engineering documentation, commit
1c9166066c3ab9ad089a4d12088acaa9476df4b6, index.html4873..4879,
SHA2227bab31bcfecf8f25a963b8cd74ebcc26a95415925c868260b69af26f81532,
describes old-target-priority return for the interruptive variant. This is
high-confidence documentary corroboration, not live pinned-BIOS proof of
every variant. Normal current-thread cases remain confirmed live.
Source: [PS2Tek](https://psi-rockin.github.io/ps2tek/index.html),
[pinned source](https://github.com/psi-rockin/ps2tek/blob/1c9166066c3ab9ad089a4d12088acaa9476df4b6/index.html#L4873-L4879).

Keep scheduling, validation and IRQ behavior unchanged in this return fix.
The documented TH_SELF0/dormant/i* reschedule differences remain explicit
audit debt; this decision does not assert complete service fidelity.
Invalid-priority/unknown-thread tests preserve existing error behavior.

Kernel semantic identity2->3 rejects older checkpoints before restore;
GT4CPT3/GT4KERN2 layout, time3, interrupt4, RPC1 and translation2 remain.
Tests require nonzero return, game-like restoration, target/caller distinction,
preempted saved return, incoming register isolation, serialization/resume
and explicit kernel-model2 refusal. Full gates and fresh boot must establish
the actual effect; do not infer functional boot from contract tests alone.

Gates passed53/53 CTest and Python91(85/6); fresh snapshots confirm
restoration24 and saved-return compatibility. The boot frontier persists,
and strict mode refuses the preexisting unknown80000592/function0 pair.
Evidence: [slice104](../reverse-engineering/slice104-previous-priority-return.md).
