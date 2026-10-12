# 0048 — Qualify the message prefix before attributing the extra frame

Date2026-10-12; slice107. Status: accepted observation only.

Observe the first update lock and message queue through native caller/stack
identities and the same main waiter migrating from receiver to pending.
Filter generic queue getters by the qualified queue pointer; RA alone also
selects other queues. Earlier update getters cannot trigger the post-result
stop. Pointed diagnostic reads require explicit main-RAM bounds before
contains/read, since contains also recognizes MMIO.

Reference40windows/280states and reduced120/120 exact common states qualify
the new fields. Model121pre-handler boundaries and independent10k serialized
comparison against actual production qualify the read-only observation
interval. Keep diagnostic provenance and producer/input identities explicit.
Raw cycle entries remain separate from debited/materialized endpoints;
independently decoded common store prefix qualifies80new debit observations.

Both first update getters and notify getters have3/3; only the model has4/3
at the subsequent release, with private return100 observed while the same
main node is still receiver. The reference executes the corresponding queue
transfer and wake with3/3. This establishes a smaller local divergence before
sync, but not the IRQ cause/writer or equal elapsed boot time. Earlier receive
phases already differ and are retained, not discarded to align frame numbers.

Changing queue/sync behavior, adopting a service quantum or reversing correct
IRQ preemption is not justified. Next108 identifies counter writer and
interrupt/time ownership in this bounded interval, preserving production and
accounting non-service work. The1000us reference match remains open.

Trade-off: another bounded observation leaves the production frontier intact,
but avoids changing a demonstrated queue operation to compensate for an
unattributed frame event. Rejected broad-selector/unguarded diagnostics and
generated-profile hash correction are preserved in107 evidence/metadata.
