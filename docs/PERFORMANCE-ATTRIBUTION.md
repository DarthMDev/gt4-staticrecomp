# OpenCode evidence reconciliation and timing accounting

Slice 142, 2026-10-14. Source integration track:
`codex/hybrid-no-jit-port-plan-141`, implementation branch
`codex/perf-accounting-142`, parent `432252a`.
This is H0 evidence review and a host-only accounting core, not a speedup or
replacement instrumented player build. The game still uses its existing code.

## Reconciled OpenCode experiment

**Confirmed:** clean source checkout `codex/perf-investigation-115` at
`dd666464b4bb40e111b8d43adb8099a634a0d9e1`; supplied executable matches
54,469,632 bytes / SHA256
`69eb3cc98249eb125a9e6ae8f1942442c8f4696ff8944b1da9c128d3f923a726`.
The retained test receipt reports CTest 4/4 and Python 34/34. Its manifest's 3/3
and 32/32 fields are stale. The original checkout/artifacts are preserved.

**Confirmed source/controlled-clock defects:**

| Observation | Consequence |
|---|---|
| EE dispatch calls preempt, which services IOP/events/VBlank; VIF can call VU | Timers overlap. EE + IOP + residual is not an exclusive partition |
| Reporting/restarting happens inside scopes that close later | A later destructor charges pre-cut time to the new interval |
| F9 only flips enabled state | Disabled wall time/guest counter gaps can appear after re-enable |
| Scopes read QPC even when disabled | `GT4_PERF=0` is not a no-instrumentation-cost reference |
| Presentation source size is reported as upload bytes | Counter does not measure actual upload traffic |

Luna independently traced generated `preempt` through donor `system.cpp`
and VIF/VU paths. A separate fake-QPC characterization compiled against the
hash-matched old header reproduces all six expected cases: two clock calls for
a disabled scope, a disabled-started scope charged after enable, a 1,000,020 us
interval containing the disabled gap, pre-cut time leaking after report, nested
EE/IOP double-count potential and the byte-label mismatch. These are synthetic
host controls; no game navigation or live performance sample was taken.

**Evidence limit:** the retained long session log hashes to
`e6da7243906fc5771ee02ded577a7b6be2f5a559e9f773ff4188c8476411899c`
(4,620,165 bytes). It contains 6,081 periodic reports and no manual reports. The
reported 494-interval selection/filter and scene labels were not located; its
median/min/max and scene-specific distributions remain an OpenCode-reported
summary, not independently reproduced statistics. The old log also has the
documented cumulative guest-counter defect. Do not infer real game speed from it.

**Hypothesis retained:** guest-side/driver CPU work deserves investigation before
assuming presentation dominates. **Unknown:** the dominant subsystem. Wall time
outside measured waits is not measured CPU execution time; it includes scheduler
delays and unmeasured work/waits. A GS fence wait includes queue/driver scheduling
and completion latency; it is not a GPU timestamp or exact GPU execution duration.
The source also retains the old keyboard analog duplication; any new integration
must preserve slice 136's correction rather than launch that older behavior.

## New controlled-clock core

`native/perf_accounting.hpp` adds fixed-size, single-host-thread accounting.
Callers provide monotonic host timestamps; the core reads no clock, environment,
game state, file or GPU. It is linked only into its new CTest fixture, not into
`gt4vulkan` or donor overlays in this slice.

- Every elapsed segment belongs to one **exclusive** top category, or unattributed
  time. Exclusive totals sum exactly to the measured interval.
- **Inclusive** totals measure the union of time with a category active. Recursive
  same-category scopes do not double count; inclusive totals are not additive.
- Checkpoints expose still-active work. Restart clips at the supplied timestamp
  while preserving scope ownership; subsequent returns charge only the new portion.
- Disabled scopes retain stack ownership without accumulating time. Re-enable
  starts a fresh interval and excludes the disabled gap.
- Entry counts mean calls begun while enabled in this interval. A carried scope
  can have positive time with zero new entries; do not divide by zero for averages.
- 64 scopes, strict LIFO tokens, monotonic timestamps, explicit invalid-category/
  capacity/order/clock/overflow faults. Faults remain sticky and invalidate the
  measurement; restart cannot conceal them. No host undefined behavior for wrapping.

Authored timelines exercise EE→VIF→VU, nested IOP/host/waits, unattributed time,
active cuts, disable/re-enable, direct/interleaved recursion, LIFO/clock/category/
capacity failures and near-uint64-limit timestamps. Independent Luna review found
no blocker; its fixture-sum/recursion/entry-documentation suggestions were applied.
Counter-overflow guards exist, but the monotonic uint64 span/token invariants
prevent practical direct overflow; no production mutation seam was added to force it.

Fresh Release build/control CTest 8/8 in 14.58 s, Python 164/164, zero skips or build
warnings. After fixture-only hardening, targeted accounting CTest 1/1 passes;
the initial complete receipt is preserved separately. The old-probe fake-clock
characterization also passes with a warning-free final compile. No benchmark,
gameplay smoke, game launch or owner-card operation was performed by this slice.

## Next candidate gate

Use this core in a separately pinned measurement adapter and frozen-donor
derivation, with strict fresh-output/source validation. Clip **all** active scopes
and synchronize frame/counter baselines at report/toggle boundaries. Record waits
as nested categories, correct labels/zero-denominator handling and use a safe
monotonic clock conversion. Preserve keyboard correction, disable shape capture
and quantify diagnostic overhead in the actual high-VU scenario.

Qualify callback/thread ownership before instrumentation: the core is not thread
safe, and VU/GS worker configurations cannot share it implicitly. Preserve the
old producer and baseline/reference target. Only then provide a new owner-controlled
F7/F8 test build and rank exclusive wall costs alongside available CPU/GPU evidence.
Do not select VU AOT, rewrite GS or remove fences from the current summary alone.
See [Hybrid Port Plan](HYBRID-PORT-PLAN.md) and
[slice142 evidence](inputs/slice142-performance-accounting.json).
