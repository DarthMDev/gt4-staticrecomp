# 0043 — Observe reference time at materialized block boundaries

Date: 2026-10-12, slice102. Status: accepted for bounded observations only.

SYSCALL and ERET host entries in the pinned dynarec expose guest state before
the current translated block's cycle debit. A raw entry/return delta alone
cannot establish the complete dispatch interval.

Use matched executable/PDB/source, explicit Cpu==recCpu checks, isolated real
disc profiles, actual host returns and one temporary cycle write watch per
endpoint. Audit generated caller/store bytes against source and an independent
disassembler; confirm the later EE event-test boundary. Separate block costs
from idle fast-forward. Preserve rejected pilots and restore instrumentation
before normal shutdown. Host elapsed time is outside the measurement.

Activate the observer only after exact hash-verified CORE wrapper words are
loaded; instrument ERET only during a bounded selected window. Refuse overlap,
unexpected debug events and expired deadlines with pending instrumentation.
Repeat the relevant prefix with less observation. This avoids treating a slow
BIOS-startup debugger census as the requested game interval.

Slice102 establishes two2000us retry windows and a message-wake/frame prefix;
it does not obtain the requested1000us update phase. The reduced repeat matches
54 recorded states. No production clock, slot-resume semantics or checkpoint
identity changes. Decisions0040/0041/0042 still apply; a work/time policy needs
the missing phase match and must include non-service work consistently.

Evidence: [slice102](../reverse-engineering/slice102-matched-update-interval.md).
