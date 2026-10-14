# H0/H1: freeze and attribute the Windows no-JIT baseline

Status: ready to scope the next implementation slice; not executed by slice 141.
Slice142 reviews OpenCode evidence and qualifies the standalone time-accounting
core; [integration/counter/clock gates remain](../PERFORMANCE-ATTRIBUTION.md).
Parent: [Hybrid Port Plan](../HYBRID-PORT-PLAN.md). Windows first. Owner controls
all gameplay; no GUI automation, synthetic inputs or scene navigation.

## Deliverable

One separately pinned Release candidate, comparable with the existing owner
build, with bounded host telemetry and a reproducible owner-run measurement
protocol. Find the largest demonstrated cost before selecting an optimization.

## Inputs and isolation

- Begin from the dedicated hybrid-plan branch and re-read STATUS/BUILD/AGENTS.
- Read OpenCode's current branch/status/report and owner results without modifying
  its checkout or session. Record its exact commit; request missing evidence
  rather than treating an unfinished experiment as the new baseline.
- Preserve the keyboard correction, input/audio behavior, source notices and
  existing saves/backups/locks. Shape capture is disabled in performance runs.
- Verify private ISO/CORE/ELF/library/shader pins. Choose the baseline library
  explicitly; observation libraries and recovered callbacks have distinct gates.
- Use fresh immediate private output directories and manifests. Do not invoke a
  recipe that writes frozen114/118/136 outputs without isolating every producer.

## Bounded work

1. Inventory current CPU/GPU waits, diagnostic switches and clock authorities
   from source. This identifies measurement sites, not the bottleneck itself.
2. Record no-JIT settings and reachable paths; no PS2 instruction machine-code
   generation is introduced. Audit executable-memory APIs separately from normal
   driver shader compilation. Unsupported code/services stay explicit.
3. Add aggregate timings/counters at reviewed subsystem/host boundaries. No
   per-instruction or packet-body logging, no captures masquerading as profiling.
   Account for nesting and perturbation; label missing GPU timestamps Unknown.
4. Add controlled-clock/counter fixtures and existing CTest/Python checks for new
   logic. Build/test serially, then compare telemetry enabled/disabled overhead.
5. Provide manual interval controls and a concise en-US owner protocol for menu,
   showroom and the chosen race segment. Launch only a verified candidate through
   the protected launcher when it will not interfere with an existing session.
6. Record owner observations and rank competing costs. Propose one measured
   H2 experiment with correctness controls, rather than changing all subsystems.

## Acceptance

Public report contains exact source/input/tool/build hashes, scenario context,
interval boundaries, median/p95/p99, guest speed, CPU work/waits, supported GPU
timings, readback/submission counts and audio queue health. Unavailable metrics
are explicit. No speedup conclusion before comparable owner-run intervals.
Existing functionality checks remain intact and private game data stays ignored.
H1 screening is not stable-60 acceptance; that claim requires the later sustained
fresh-frame/presentation and critical-path budget gate in the parent plan.
Finish with STATUS/journal updates, commit and push; no rewrite of either track.

Do not implement upscaling, import PCSX2 GS, add a mobile frontend, raise the
guest clock or remove synchronization as part of H0/H1. Those have separate
interfaces/tests and require measured attribution or renderer feasibility work.
