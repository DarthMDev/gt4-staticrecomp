# Slice102 — qualified reference time and message-wake phase

Capture date: 2026-10-11; closure: 2026-10-12. Baseline75d139a.
Branch slice102-matched-update-interval. Status: closed bounded observation;
the requested1000us phase was **not observed**. Owner authorized small slices toward real
boot, with progress updates/questions as needed; later native Vulkan, audio,
XInput and game saves follow causal boot progress.

## Initial evidence and scope

Confirmed model frontier: delay1000us from update0x1016E8/object0x6DE790,
caller0x578B20/outer0x101760; WaitSema0x5ADCE4 restores root0x5ADBC8.
Its accepted-service charge147456BUSCLK consumes the full pending deadline.
TIM2 then interrupts that restored PC before any root instruction; correct
final interrupt-return preemption selects update again. Slice96 showed270/270.

Reference97 measured retry2000us and frame SleepThread, both selecting idle
0x81FC0, not root. Historical modes were not qualified per segment. They do
not answer this experiment. Slice101 now qualifies host SYSCALL/ERET symbols
and CPU global layout; dynamic-engine caller/clock require fresh qualification.

Pinned source audit: recSYSCALL calls interpreter SYSCALL runtime function,
and recERET calls interpreter ERET. Register flush does not necessarily debit
pending block cycles before these host entries. iBranchTest applies the debit
afterwards. Thus raw cpuRegs.cycle at these points is not an exact per-word
clock. Observe a materialized branch/event boundary and preserve the distinction.

No production time, scheduling, checkpoint or acceptance expression changed.
Next: isolate/pin reference boot, identify engine pointer, real1000us request
and selected context, read timer's own epoch, repeat with less observation.
Do not infer a conversion from one sample or special-case WaitSema.

## Observation preparation and pilot limits

Confirmed input verification: ISO67b6c007..., CORE85d26aa8..., native
analysis ELF10f82e22..., reference executable0506a196... and matching
PDB99b48345... unchanged. Owner INI/slot9/backup and six original BIOS
verified against slice101 preservation. A new ignored profile copies the BIOS,
routes all folders privately and disables only shutdown confirmation. Inherited
renderer13 is **Software**, with a D3D12 presentation device in the log;
it is not Null. Cards/cheats/compatibility patches disabled; no injected input.

DbgHelp confirms recCpu RVA0xE4EFF0, Cpu pointer0x2EA2578,
cpuRegisters0x2EA2880, EE counters0xE79E60, EE memory pointer0x169F000,
and EE recEventTest0x47AD00 (not IOP homonym0x4506B0). Each recorded
segment requires Cpu==image_base+recCpu. Native ELF independently gives
0x005ADCE0 words24030044/0000000C, the WaitSema wrapper load/syscall.

First pilot instrumented SYSCALL/ERET from BIOS startup:180s bounded census,
265198/265241 hits, **zero selected guest samples**, normal shutdown.
It was too intrusive to reach the requested interval; no timing conclusion.
Source59e8bc1a... and executablee07b7533... archived separately before changes.

Second pilot lets startup run until those exact CORE words appear in RAM,
then uses DebugBreakProcess on only the isolated process to install the sparse
observer safely. ERET is instrumented only inside a selected service window.
One write watch of cpuRegs.cycle (8bytes, offset1088) after each host return
captures the next clock write, immediately disarms, and preserves native bytes
for auditing the emitted block debit. This is not per-word stepping. The initial
27 selected calls were file/RPC semaphore paths, not the requested delay;
the183-record budget ended normally. Their26 complete windows are controls
for the observation machinery, not evidence for update1000us.

Independent review confirmed list head0x659308/node+0x30 descriptor/node+0x20
interval. Head alone does not identify the WaitSema owner: qualification needs
interval147456, flags3, callback0x5AEF58, matching semaphore argument and
update caller/stack. Review also required a rigid deadline, refusal on overlapping
windows, explicit handling of extra debug-register reasons, and byte auditing
of generated callers. The next pilot narrows calls to timer WaitSema, SleepThread
and the timer callback's iSignalSema. That third pilot reached37 retry2000us
entries and36 timer callback entries, still before the desired phase;
its505-state budget ended normally. It is not the accepted final census.

## Confirmed qualified real-disc census

A fresh profile records **430 selected entries, 429 complete service windows,
3004 states**, then stops at its state budget. The last entry is incomplete;
do not count it as an executed service. In each complete window:

1. SYSCALL entry identifies guest PC/code/service/arguments/SP/RA.
2. Actual host SYSCALL return shows selected vector0x80000180.
3. One write watch captures its block-cycle debit before dispatch.
4. Host ERET entry preserves the BIOS-selected EPC/context.
5. Actual ERET return confirms PC and EXL clear.
6. One write watch captures the ERET block debit before dispatch.
7. EE recEventTest entry confirms that materialized cycle/context.

All429 windows pass those ordering/context/clock checks. Generated callers
are executable outside the main module; that alone is a heuristic. Captured
caller/store bytes and pinned source establish the relevant instruction path.
Independent MSVC dumpbin14.44.35228.0 decodes captured, inert COFF wrappers:
load [rbx+0x3B0], add2, store [rbx+0x3B0], compare next event and dispatch.
Source R5900_TEXTPTR=&cpuRegs.GPR.r[9] gives RBX offset0x90;
0x90+0x3B0=0x440=cycle offset1088. The hardware watch identifies that address.
Independent final review confirms all858 final endpoint stores use that+2
sequence; timer-callback pilot paths include+7. This is not a uniform
per-instruction cost.

Source nuance: recBranchCall first sets nextEventCycle=current cycle;
ERET's cpuSetNextEventDelta(4) takes the **minimum**, so it preserves that
earlier deadline. The observed positive ERET debit therefore reaches event
testing immediately in these paths. Do not generalize SYSCALL/ERET entry
to a completed block clock or to the separate interpreter engine.

The final census keeps two initial timer retries, then skips240 further
2000us retry windows to avoid redundant instrumentation. Other completed
windows include89 actual frame SleepThread calls (ra0x4A2248),75 message
receive SleepThread calls (ra0x57689C),71 message WakeupThread calls
(ra0x576730) and associated priority changes. They are distinct callers;
not every message receive belongs to main.

**Zero recorded timer intervals147456:** no matched update1000us/root
interval was obtained. This finite census does not prove that the reference
never issues one. The reference regularly waits for VBlank while the model
is already late and issues the conditional1ms retry (slice96). This phase
difference must be resolved before selecting a production time policy.

## Confirmed timer measurements at materialized endpoints

Both retained timer calls have flags3, callback0x5AEF58, matching descriptor
semaphore0x15, interval294912, accumulated0, outer return0x577FB8.
They are the **root retry2000us**, not update1000us/outer0x101760.

| Window | Vector boundary cycle | Selected-context boundary cycle | Difference | Selected PC | Timer remaining, BUSCLK |
|---|---:|---:|---:|---|---|
| sequence28 |1646380288|1646381398|1110|0x81FC0|294912 ->294400|
| sequence35 |1646975695|1646976805|1110|0x81FC0|294656 ->294144|

These are reference block-boundary intervals, not physical-console syscall
costs. First timer base17165568, MODE0x382, CLKS2/rate512, no pending OVFF;
the independently audited lazy-counter expression gives17165568 ->17166080.
The second starts256 ticks after its base: do not round it back to a full2ms.
Raw host entries have pending debits; their timestamps remain separately
recorded. No elapsed host seconds or guessed guest conversion enters this result.

First actual frame SleepThread (frame1/synced0, stack0x6DE660) has vector
boundary1801445065 -> selected-context boundary1801445911:846 cycles.
It selects message continuation0x5767C0/SP0x1FFFB80/RA0x5790F8,
**not idle**. Later frames choose other contexts, including idle. Slice97's
different frame/phase cannot be substituted for this sample.

## Confirmed message-wake prefix and repeat

At frame1, receive SleepThread RA0x57689C/SP0x1FFFB90/a0=0x6D6774
selects idle. At frame3/synced3, update-stack0x6DE630 changes thread5
priority to1 (ra0x576710), calls WakeupThread with **a0=1**
(ra0x576730), then restores thread5 priority24 (ra0x576760).
The subsequent frame wait (sequence98) actually restores the previously
observed receive context: PC0x5ADBC8/SP0x1FFFB90/RA0x57689C/a0=0x6D6774.
This is direct context continuity; identifying it as the initial main thread
is high confidence, not a complete captured BIOS TCB lifecycle.

This independently supports the earlier model message-wake candidate; it
does not prove a WakeupThread bug. The reference reaches the frame wait
instead of the model's already-late1000us path. Ordinary main work after
that restore and its next blocking reason remain the next comparison.

A second fresh profile removes host-return breakpoints and all clock watches,
keeping entry/ERET/event endpoints and stopping at frame6: **18 complete
windows,54 states**. All54 match the qualified prefix exactly in every
recorded field except observer sequence/thread/host_rva; cycles, registers,
timer fields, stack bytes and frame counters included. This is equality of
recorded observations, not all RAM or completed-instruction counts. It
supports the captured initial phase; skipped retry totals240 versus235
show why an asynchronous aggregate census is not a universal exact trace.

## Preservation, acceptance and next slice

All five instances closed normally; no reference remains. Owner INI/slot9/
backup, six original BIOS and18 isolated BIOS copies retain their initial
hashes. Helpers compiled warning-free with MSVC;22 archived helpers and all
captures stay ignored. Metadata only:
`docs/inputs/slice102-reference-intervals.json`, SHA256
`c9876d5501a130a5da4ccce8026ec0791e45b82a9fb8b54cb5c3ab4c7a0bf0a8`.

Production unchanged: gt4boot SHA2564c1dd903...; semantic identities and
acceptance gates unchanged. Existing slice101 gates53/53 CTest and Python
91 (85 executed/6 skipped) apply to the identical production; no new full
suite run is claimed for this documentation/metadata slice.

Fresh independent read-only review approved the429-window census, both timer
computations,54/54 repeat, native-store decoding and hashes/sizes of22 helpers,
six capture/analysis artifacts, nine source files and both initial profiles.

Next **slice103**: follow the first message-wake -> receive continuation ->
ordinary work -> next block, aligning caller/stack/queue/frame between model
and reference. Capture actual arguments before diagnosing priority/lifecycle;
do not equate numeric IDs across engines. The1000us match remains open.
Do not force wake, alter priorities, copy dynarec slot defects or fit a quantum.
