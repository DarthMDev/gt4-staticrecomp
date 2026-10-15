# 0065 — Compile all feasible guest code ahead of time; VU1 first

Date: 2026-10-14. Status: Accepted planning direction (slice 151).

The owner asks for a light, fast Windows port, later Android/iOS, with no JIT
and no dedicated renderer for now. GT4 USA v2.00 is a fixed disc, so EE, VU1,
VU0-micro and IOP code are finite sets that can be compiled offline per target.
Interpreters remain only as explicit, counted fallbacks for unseen variants.

Order: VU1 AOT (62.5% of the slice 150 interval runs in the interpreter because
no compiled VU1 programs are registered), then asynchronous GS submission
(21.7% is a synchronous fence wait), then VU0-micro/IOP AOT and EE LTO/PGO.
Graphics features (scaling, PCSX2-class GS) follow the speed work; importing
PCSX2 GS code requires an owner decision on GPL-3.0 distribution.

This decision keeps decision 0064's no-JIT definition and safety gates and
reorders its H1/H2 work. See [Optimization Plan](../OPTIMIZATION-PLAN.md).
