# 0063 — Keep VIF framing conditional on explicit cycle state

Date: 2026-10-13. Status: Accepted for offline P1 framing only.

Do not inherit a default CL/WL from a model filename or a successful byte scan.
Require a caller-supplied nonzero cycle, update it on STCYCL, and count source
rows during fill mode. Public hand-authored controls distinguish destination
count from source count, skip mode, NUM0 expansion and payload alignment.

The source-only errata recommended deferring all cycle-dependent framing. The
parent accepts a narrower, controlled conditional implementation for explicit
CL/WL in 1–255. This is a byte-span decision, not VU execution or live-state
qualification. The pinned donor's formula and an independently written replay
are comparisons; neither establishes hardware fidelity. Zero-field cycle
semantics, packed/reserved UNPACK, channels and geometry remain unqualified.

All selected car packets close under (1,1) and contain no STCYCL. Therefore
the incoming live cycle is still Unknown and must be traced before claiming
native geometry correctness. No GIF special case overrides ordinary payload
length merely because the immediate resembles Nenkai's marker convention.

See [VIF Framing](../VIF-FRAMING.md) and
[slice 123 metadata](../inputs/slice123-vif-framing.json).
