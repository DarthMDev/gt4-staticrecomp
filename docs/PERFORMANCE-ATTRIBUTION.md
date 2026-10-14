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

## Playable diagnostic candidate (slice143)

**Confirmed:** `native/perf_probe.*` connects the controlled accounting core to
four derived donor scope sites (EE dispatch, IOP run, VIF transfer, VU interpreter),
native GS host copies/dispatch/presentation/waits, audio push/waits and host VBlank.
The clean pinned donor is unchanged. Production AOT game library and nine SPIR-V
shaders match slice114. Internal scale remains1; no game clock, graphics semantics,
fences, generated game code or input merge behavior is changed.

Read-only Luna traced current native single-thread ownership: donor browser/VU
worker hosts are not compiled or started here. Probe records wrong-thread use as
an invalid measurement rather than sharing mutable accounting. Its only platform
CPU measurement is owner-thread GetThreadTimes kernel+user, sampled at boundaries;
wall clock uses steady_clock with safe microsecond conversion. Exclusive wall
categories partition the interval; inclusive active-category unions overlap.
Neither table allocates actual CPU execution time among subsystems. Fence waits
remain completion latency, not GPU timestamp duration.

Use tooling Python from the assets checkout, from this candidate checkout:

```powershell
C:/Antigravity/gt4-staticrecomp/private/tooling-venv/Scripts/python.exe scripts/play_performance.py --assets-root C:/Antigravity/gt4-staticrecomp
```

The launcher selects only the pinned143 manifest/executable, preserves the existing
manual card and byte locks, and creates the usual per-session card backup. No
shape-capture mode is linked into this player. Inherited PS2_ and GT4_PERF_ tuning
settings are removed; GT4_PERF=1, PS2_SCALE=1 and PS2_GS=gpu are explicit. Vulkan
loader overrides are preserved and recorded in result.json after the session;
other driver/overlay state is not controlled. No scripted presses/navigation.

1. Owner opens the race and lets its initial loading settle.
2. Press F7 once, drive the same section for30–60s, then press F8 once.
3. Tell Codex the car/track/camera and approximate start/end points; the report is
   in `private/manual-play/session-.../probe.log` in the assets checkout.
4. Optional F9 reports before pausing and resumes with a fresh interval. F7 also
   resumes if paused. Close normally after F8; shutdown omits the partial interval.

Keys are latched until the next VBlank, before host message pumping. Repeated
keydown is ignored. F7 suppresses automatic5s reports/reset until F8. Same-boundary
F8 is handled before F9/F7 so a pause/restart cannot erase the requested interval.
All still-active scopes are clipped at each cut; disabled gaps and cumulative
guest-counter baselines cannot leak into the next interval. Disabled scopes keep
ownership but do not read clocks; this is **not** a zero-instrumentation reference.

Reports label VBlanks/s separately from Unknown fresh scene frames, and guest
cycle-model speed separately from independently proven game speed. Percentiles
use nearest-rank p95/p99 and even-sample average median; a4096-tail sample cap is
disclosed. Mapped write/read bytes and presented RGBA bytes are separate counters.
Audio empty refills identify an empty queue at refill, not proven audible underruns.

**Controls:** warning-free Release candidate CTest7/7 in4.36s; separate macro-off
observation control CTest9/9 in14.23s, Python171/171 zero skips. Final launcher
Vulkan-environment metadata change passes the Python CTest again in8.64s. Fixtures
cover nested cuts, long manual intervals, disabled/no-clock spans, resume, counter
reset, wrong-thread invalidation, percentiles/tail limits and F8/F9 ordering.
Preparation controls prove exact inverse derivation, source pins and fresh-output
rejection. Launcher controls cover profile/schema rejection, inherited tuning
settings and card/session locks. Strict CMake gates recheck clean donor, game
library, source receipt, adapters/shaders and exact capsule inventories.

**Unknown / next evidence:** overhead under high-VU gameplay, GPU timestamp time,
distinct rendered scene counts and the dominant subsystem. This slice adds
diagnostics, not a performance optimization. The separate macro-off build is a
source/fixture control with an observation EE profile, not a matched live overhead
benchmark. Owner should collect the corrected interval before selecting any VU/GS
rewrite or claiming a bottleneck. Public metadata: [candidate](inputs/slice143-performance-candidate.json),
[derivation](inputs/slice143-performance-runtime.json); raw artifacts stay private.
