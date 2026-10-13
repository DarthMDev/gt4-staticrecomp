# 0058 — Isolated EE Producer and Static Render Contracts

Date: 2026-10-13. Status: accepted for the authorized P0/P1 work; no renderer swap.

## Decision

Reconstruct the current qualified ELF directly from pinned CORE records and the
existing reviewed ELF/BSS policy. Build the MIT donor translator with Clang and
regenerate/compile EE code in an independent private producer. Compare normalized
source identities to the frozen producer; retain production library/launcher gates.
Do not restore the original archived engine or reuse its validation claims.

Use a bounded static scanner plus independent semantic review to map camera/model
candidates before observing or replacing their work. Prefix hashes and literal
call-site counts are explicitly narrower than function/reachability proof. Getters,
model scripts and callbacks keep their authoritative invocation count.

## Evidence and limits

[Slice 116 metadata](../inputs/slice116-ee-producer-contracts.json) records verified
CORE/ELF identity, rebuilt translator, equal 328-file inventory, fresh library and
independent ISO preparation. Static research identifies a recovered callback body
missing from the current AOT registry; dispatcher behavior is a stop, not EE
interpretation. Reachability is Unknown; source equality does not close P0 coverage.
No native-renderer, high-refresh or performance claim follows from these results.

The owner selected cheap Luna workers for bounded inventories and source review.
The integrator reviewed their evidence and a stronger semantic review, applied all
changes in an isolated checkout, and preserved OpenCode's optimization branch.

## Consequences

The public recipe no longer requires a prepared generation cache to produce EE
source/library. Runtime integration still needs deliberate candidate selection,
native checks and owner gameplay qualification. Complete asset/lifetime/matrix
contracts are prerequisites for the first live native car. Deliberate extra seeds
must use another documented producer and controls, never an unexplained baseline
repin or a permissive missing-code fallback.
