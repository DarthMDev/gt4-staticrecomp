# 0044 — Treat the message priority-return divergence before clock changes

Date:2026-10-12, slice103. Status: accepted observation and next-fix scope.

Independent reference103 demonstrates successful current-thread priority
changes24->1 returning24 and1->24 returning1, with60/60 states repeated
under reduced observation. The game saves that return and selects it for
the restoration argument when s2 is the -1 sentinel. Fresh model snapshots
show the same path consuming an unconditional zero from our kernel and
restoring priority0. This is a confirmed earlier service-return divergence.

Prioritize this contract before selecting a new clock quantum or forcing
thread scheduling. Reference resumes main receive and then synchronously
blocks in RPC_BIND; the model's RUN root inside a timer callback is not
ordinary main work. Different IDs/stacks/frame phases remain explicit.

Production remains unchanged in103. A subsequent correction must test
nonzero previous priority and preservation across preemption, update the
kernel checkpoint semantic identity, reject older model checkpoints and
run the full existing gates plus fresh boot/differential evidence. Clock,
correct interrupt preemption and acceptance expressions stay unchanged.
Do not present other-thread/error/interruptive semantics as independently
verified from these two successful current-thread calls.

Evidence: [slice103](../reverse-engineering/slice103-message-wake-prefix.md),
[metadata](../inputs/slice103-message-priority.json).
