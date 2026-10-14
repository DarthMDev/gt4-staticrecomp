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

This was the slice143 launcher entry point. The current wrapper selects the
qualified145 pair below; the143 executable/manifest remain frozen and valid.

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

## Owner report verification (slice144)

**Confirmed:** owner session `session-20261014-175413-544001` contains one complete
manual F8 block at lines601–608 in the observed prefix. Two adjacent F7 starts
occur at599/600; the second restarts the interval. No automatic report, invalid
accounting or guest-counter reset appears between that start and the F8 report.
All970 samples fit the4096 cap. Live session remains open; result.json is produced
on normal exit, while F8 has already flushed the full report to probe.log.

| Measurement | Observed value |
|---|---:|
| Duration / VBlank samples / pace |40.323s /970 /24.06 VBlanks/s|
| Interval mean / median |41.570ms /41.341ms|
| p95 / p99 / maximum |47.108ms /49.685ms /60.476ms|
| Measured owner-thread CPU / occupancy |31.620ms per sample /76.1%|
| Exclusive VU interpreter wall / interval share |25.646ms /61.69%|
| Exclusive GS fence completion wait |9.392ms|

The14 exclusive categories sum to41.572ms per sample, within their0.001ms printed
rounding of40.323s/970. Independent Luna read-only log validation agrees on block
completeness, boundaries and arithmetic. Runtime hash matches the143 pin. Frozen
prefix and extracted report have separate private hashes; mutable source log and
owner card are untouched. See [metadata](inputs/slice144-owner-performance-report.json).

VU interpretation is the leading observed wall category in this instrumented
interval. **Unknown:** diagnostic overhead, exact car/track/camera and scene
composition, separate subsystem CPU execution and GPU timestamp time. The24.06
figure measures VBlank cadence; it does not establish distinct rendered scene FPS.
No optimization, rebuild, process restart, scripted input or new gameplay probe
was performed by the agent for this verification.

## Matched overhead pair (slice145)

Owner identified the144 interval as Toyota Prius, external camera, Nordschleife.
Vehicle year/trim, start/end positions, warm-up and video mode remain unspecified.
The VU scope measures **VU0 micro and VU1 together**; VU0 macro operations execute
in EE code. A VU1 call count does not separate per-unit elapsed time. After VU,
the next largest exclusive category was GS fence completion wait9.392ms, followed
by EE dispatch2.261ms and VIF/GIF1.120ms. None is subsystem CPU/GPU timestamp time.

Two fresh Release candidates differ by `GT4_PERF_FRAME_ONLY=OFF/ON`. Both use the
same pinned production EE library, immutable143 three-source capsule,143 port,
nine114 shaders, compiler/flags, guest settings and current native host code.
The frame-only definition makes direct donor Scope objects empty/trivially
destructible, replaces native scope macros with no-ops, and excludes mapped-byte,
dispatch, presentation and audio operation hooks. It retains common F7/F8/F9
boundaries, wall/thread-CPU clocks and cumulative guest progress/workload samples.
The report labels mode and makes missing subsystem/host-traffic data explicit.
**It is not a zero-instrumentation baseline.** This measures incremental detailed
scope/counter cost relative to the common boundary telemetry, not total overhead.

Public [detailed pins](inputs/slice145-performance-detailed.json) and
[frame-only pins](inputs/slice145-performance-frame-only.json) bind mode, exe and
dependencies. Launcher preserves card/backups/byte locks, rejects crossed modes/
paths and keeps the existing143 profile accepted. It defaults to the145 detailed
candidate; use tooling Python from this checkout, with the owner's assets root:

```powershell
C:/Antigravity/gt4-staticrecomp/private/tooling-venv/Scripts/python.exe scripts/play_performance.py --assets-root C:/Antigravity/gt4-staticrecomp --mode detailed
C:/Antigravity/gt4-staticrecomp/private/tooling-venv/Scripts/python.exe scripts/play_performance.py --assets-root C:/Antigravity/gt4-staticrecomp --mode frame-only
```

Run one session at a time; owner closes the previous game normally. Owner retains
all navigation. Prefer detailed/control/detailed order, same car/camera/video
setting and a repeatable scene. A stationary car at the same location provides
a simpler first comparison; moving race segments must also match scene/workload.
Warm up each run, then F7,30–60s,F8, and close normally to finalize result.json.
Record scene, power/display/overlay state and any loading/mode changes. Obtain
three comparable intervals per mode before treating a small difference as real;
thermal/scheduler variation may dominate. Do not mix143 and145 as the matched pair.

Compare median/p95/p99 and thread CPU alongside common workload counters and guest
clock-model speed. A larger frame-only pace alone does not prove correct gameplay
speed, an optimization or PS2 fidelity. CPU per sample and frame timing changes
only estimate instrumentation impact when scene and machine conditions match.
GPU timestamps/distinct scene counts and absolute common-boundary cost are Unknown.

**Confirmed host controls:** detailed native7/7 in5.26s, frame-only native7/7
in5.03s, separate macro-off observation9/9 in14.22s, Python173/173 zero skips,
warning-free builds. New fixture verifies mode, shared guest/CPU data and absence
of invented subsystem/host-traffic data. LLVM COFF symbol control finds zero
undefined Probe references in frame-only system/vif/vu_micro/native_vulkan/host_io
objects, versus expected references in detailed. Independent source review finds
no pair blocker. These controls do not replace owner gameplay/overhead intervals.
No current session restart or new gameplay probe was performed by the agent.

VU0/VU1 separation is a following pinned derivation; it is not silently added to
this single-variable overhead pair. See [offline AOT feasibility](VU-AOT-FEASIBILITY.md).

## Detailed owner run and logical-CPU screenshot (slice146)

The owner repeated the Prius/external-camera/Nordschleife test in145 detailed.
Session `session-20261014-181701-302246` records F7 at160 and a complete8-line
F8 block161–168:962/962 samples,40.732s,23.62VBlanks/s. Median42.065ms,
p9548.162ms,p9950.736ms,max58.382ms. VU26.246ms is61.99% of interval wall;
GS fence9.392ms is22.18%, followed by EE2.269ms and VIF/GIF1.143ms. Separate
owner-thread CPU is32.533ms/sample,76.8% occupancy; guest clock-model speed39.36%.
The14 exclusive categories sum42.341ms versus42.340956ms from duration/count,
within printed rounding. No invalid/reset marker or automatic reset in interval.

A second F8 at169 reports60 samples/2.704s: a short follow-up after the first
report reset, not another30–60s trial. Both are preserved and classified separately.
Result records detailed mode, pinned runtime, normal window closure, exit0/no
timeout and identical card before/after. Independent Luna verification agrees.
Full closed log/result, extracted blocks and owner screenshot are frozen privately;
[public metadata](inputs/slice146-owner-detailed-results.json) carries hashes/limits.

The screenshot shows29FPS/35.3ms, overallCPU14%, highest visible logicalCPU71%
(CPU3),CPU95C and GPU31%. These are point-in-time overlay readings, not interval
statistics or identities of game threads. Our GetThreadTimes sample follows the
owner thread independently of which logical processor runs it. Serial work plus
waits can limit the frame without a fixed processor reading100%; migration can
also distribute activity across logical processors. Actual migration, CPU-bound
instruction cost and thermal/power limiting are not established by this screenshot.
Its35.3ms is not the40.732s interval median or a distinct-scene-FPS measurement.

After the owner confirmed closure and absence of a running game was checked,
the protected frame-only launcher opened `session-20261014-182103-412804`,
PID43684. Executable selection, Vulkan1x/16:9/48kHz startup and8650752B backup
confirmed; all gameplay remains manual. No live probe, build, input injection,
runtime change or performance optimization by the agent. Matched control data
is still needed before quantifying detailed instrumentation overhead.

## Frame-only owner run and detailed repeat (slice147)

Session `session-20261014-182103-412804` records F7 at134, complete five-line F8
at135–139:1175/1175 samples/56.199s,20.91VBlanks/s. Mean47.828936ms,
median46.311,p9560.676,p9983.614,max112.004ms. Owner-thread CPU37.274ms/sample,
77.9% occupancy; guest clock-model speed34.85%. No invalid/counter reset/periodic
reset within interval. Mode/runtime match145 frame-only pins; exit0/windowclosed,
no timeout, card before/after equal. Closed log/result/report frozen privately;
extraction assertions and independent Luna review agree.

| Measurement | Detailed146 | Frame-only147 |
|---|---:|---:|
| Duration |40.732s|56.199s|
| Median wall/sample |42.065ms|46.311ms|
| p99 wall/sample |50.736ms|83.614ms|
| Measured thread CPU/sample |32.533ms|37.274ms|
| Primitive mean/sample |18125.4|18061.8|
| Interpreted VU1 calls/sample |790.3|788.6|

Control median is10.09% higher and CPU/sample14.57% higher **in these observations**.
That does not establish negative instrumentation cost, an optimization, thermal
limiting or any other cause. Workload means are close, not proof of identical
scene content. Durations differ, exact position and heat/power/clock/scheduler/
overlay/driver state were not controlled. One interval per mode is insufficient
to quantify the detailed-probe cost. Both retain common boundary instrumentation.

After confirmed owner closure and no existing process, protected145 detailed
repeat opened `session-20261014-182403-457646`,PID37664. Startup/backup verified;
owner repeats the same scene, targeting40s to better match the first detailed
interval. This completes the planned detailed/control/detailed order once F8
is collected, but may still leave overhead statistically unresolved. No builds,
runtime/code/clock changes or navigation by agent. [Evidence](inputs/slice147-owner-control-results.json).

Following scope change: replace the existing aggregate `run_fast(unit,pc)` timing
with mutually exclusive VU0-micro and VU1-interpreter-fallback categories in a fresh
pinned derivation; prove their sum preserves the old exclusive partition. That
site excludes generated VU1 AOT work; keep the label narrow and preserve143/145.

## Initial A/B/A sequence complete (slice148)

Repeated145 detailed session `session-20261014-182403-457646` closed normally,
card hashes unchanged. F7line234/F8lines235–242 contains1191/1191 samples in
53.161s,22.40VBlanks/s. Mean44.635600ms, median43.779,p9551.349,p9956.683,
max65.955ms. Measured threadCPU35.094ms/78.6%; exclusive sum44.636ms reconciles
within rounding, VU28.155ms remains largest category, GS fence9.364ms second.
No invalid/reset marker; independent Luna review agrees. Artifacts are frozen
privately; [public sequence](inputs/slice148-owner-aba-results.json) records pins.

| Run | Median | p99 | Thread CPU/sample |
|---|---:|---:|---:|
| Detailed A1 |42.065ms|50.736ms|32.533ms|
| Frame-only B |46.311ms|83.614ms|37.274ms|
| Detailed A2 |43.779ms|56.683ms|35.094ms|

Removing subsystem hooks did not improve this observed sequence. That is not a
negative instrumentation-cost measurement: exact scene/position, interval lengths
and machine conditions vary, and there is only one B run. Overhead magnitude/sign,
thermal cause and any game optimization remain Unknown. Do not keep requesting
repeats of this initial sequence before making the bounded per-unit attribution
available. Next fresh candidate splits mutually exclusive interpreter timings,
with synthetic partition/routing controls; frozen145 pair remains available for
later matched experiments. No runtime or build changes in this evidence slice.
