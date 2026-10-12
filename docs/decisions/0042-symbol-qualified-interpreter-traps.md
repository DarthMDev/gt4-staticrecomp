# 0042 — Qualify interpreter traps through matched host symbols

Date: 2026-10-11, slice101. Status: accepted for this bounded observation route.
Production behavior, clock and serialized state unchanged.

## Context

The pinned release ignores its development-only per-word breakpoint probe.
Its Step Into implementation does not establish a guest word trace. Slice100
therefore left the first interpreter slot exception and BIOS return unknown.
Official symbols for that exact release expose interpreter SYSCALL, ERET and
CPU globals; CodeView/PDB GUID/age and published archive hash match.

## Decision

Use a separately pinned original host observer for the bounded synthetic
matrix. Qualify it with the plain syscall positive, branch/effect/annulment
controls, matching engine pointer and thread, audited host instruction bytes
and ERET caller. Restore all host instrumentation before detach and normal
shutdown. Refuse unexpected host breakpoints/single steps; do not silently
consume emulator assertions. Preserve refused pilots separately.

PC/EPC/Cause write watches report host state writes, not completed guest
instructions. PC may already be incremented, code may belong to the previous
instruction, and a PC write inside ERET precedes its status update. Capture
the host ERET return and later guest effects separately. Host pauses disqualify
wall-clock timing in this experiment.

## Consequences and limits

The route avoids changing the reference build or bypassing the old save
reader's identity guards. It is specific to this binary/PDB/source/layout,
not a generic observer for future releases. The initial external breakpoint
is identified operationally by order/location, not by a proved ntdll symbol.

Even a complete interpreter vector/return chain does not establish PS2
hardware correctness. Keep decision0041: do not guess a production slot
return, copy the reference failure, weaken the differential gate or derive
a time conversion. The known fake-service likely-slot gap remains a separate
resumability/serialization contract. Match a real GT4 update1000-us/root
interval independently before proposing a work-to-time policy.

Evidence: [slice101 interpreter traps](../reverse-engineering/slice101-interpreter-trap-observation.md).
