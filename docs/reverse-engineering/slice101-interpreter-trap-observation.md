# Slice 101 — separately qualified interpreter trap observation

Date: 2026-10-11. Baseline: `4c11f6b`.
Branch: `slice101-interpreter-trap-observation`. Status: closed and verified.

## Scope and initial state

Owner authorized the next slice after discussing tooling priorities. Aim:
observe first syscall-slot exception and BIOS-selected return in a separately
qualified PCSX2 interpreter, including the model's handled likely-slot gap.
Do not infer a word trace from release Step Into or a return from repeated
identical stopped states. No guessed clock conversion or preemption reversal.
EE Reloaded installation remains outside this slice.

**Confirmed initial repository state:** clean main at `4c11f6b`, then new
short-lived branch. Reference release is v2.9.114 at pinned source
`aa7ab4306e269075784c7ac3eb4b45e6e6c53445`; its interpreter address probes
are conditioned on EXTRA_DEBUG/PCSX2_DEVBUILD. The slice100 captures and
helpers remain under their original ignored experiment folder.

**Unknown at entry:** first interpreter slot vector EPC/BD, BIOS-selected
return and exact failure chain. The qualified route and observations below
resolve these questions for the bounded synthetic controls only.

The observation route is the existing pinned release plus independently
matched official host symbols, rather than a newly compiled Devel reference.
The old save timing reader is not used or assumed compatible with this route.

## Pinned host-symbol observation route

Environment: Windows11 Home Single Language10.0.26200/build26200,
Python3.14.5, VS2022 Build Tools17.14.34, MSVC dumpbin14.44.35228.0.
Original observer built x64 with `/std:c++20 /EHsc /W4`, no warnings;
the Win32 debugger and DbgHelp are host tools, not guest execution engines.

**Confirmed:** source tag v2.9.114 cloned privately and HEAD matches
`aa7ab4306e269075784c7ac3eb4b45e6e6c53445`. Official release has a Windows
symbol asset, but no Devel asset. The separately downloaded symbol archive is
21,279,011 bytes, SHA256
`23745618476f12a90493aca73029c88a33fe64f10ec3b86b8d85409259aacdfe`.
Extracted PCSX2 PDB: 100,093,952 bytes, SHA256
`99b483451a79da4237585aa9da2ffc9a844dcd128e8628921a0f12832336de6f`.
An independent PE CodeView/PDB MSF reader verifies identical GUID
`7833e0ae-b1fc-c437-4c4c-44205044422e`, age1. DbgHelp also loads that PDB with
exact-symbol matching. All source/PDB/tool payloads remain ignored.

Public symbols expose interpreter SYSCALL RVA0x18B270, COP0::ERET RVA0x1FE60,
register pack RVA0x2EA2880, Cpu RVA0x2EA2578 and intCpu RVA0xE45F48.
`cpuException` and `execI` are inlined, not separate breakpoint symbols.
MSVC dumpbin confirms inlined SYSCALL exception entry and vector store, and
references to register PC/status/cause/EPC at offsets680/592/596/600.
Original observer layout also uses cycle1088/branch1100 and size1160;
PDB reports cpuRegisters size1160, but OS DbgHelp type-child query fails.
Do not claim successful PDB member introspection or a per-word guest trace.

Original private host observer uses two restored host INT3 locations and
hardware write watches on PC/EPC/Cause on the observed CPU thread. It does
not change guest words, registers or the emulator's instruction decisions.
Host stepping only rearms an INT3; state writes are not completed guest
instructions. Cpu pointer must equal intCpu in accepted records. Qualify
before any production inference; host pauses can affect elapsed host time.

Preparation verified six original BIOS files against slice100 preservation,
copied them into13 separate original-fixture profiles and recorded owner
INI/slot9/backup hashes anew. Same inherited software/no-card/no-patch/no-input
settings; only ConfirmShutdown=false added to isolated profiles for normal
cleanup. Old captures/configs unchanged. Private root:
`private/pcsx2/sstates/slice101-interpreter-trap-observation/`.

Preparation failures preserved: initial compiler invocation needed Unicode
Win32 APIs; first ERET original-byte assumption failed before instrumentation.
First aborted startup also rejected a mixed-separator ELF path; canonical
native Windows paths fixed the launch. Second pilot timed out before a
qualified target capture; it is not vector/return evidence. Its isolated
process later closed normally (cleanup deadline reported no before exit).
Observer instrumentation is restored before detach and normal shutdown;
no force termination used. A third plain pilot captured the target/vector/
BIOS EPC write/ERET return/continuation and closed normally. Initial BEQ/BGEZ
taken pilots captured a repeated syscall after BIOS selected the slot; BEQ
false returned normally. BGEZ pilot's initial close deadline failed; the
exceptional cleanup then closed it normally. These are pilot evidence only.

Independent review found observer defects before qualification: unknown host
INT3/single-step events were consumed, the deadline was checked only on a
wait timeout, ERET caller/thread were not constrained and exceptional cleanup
did not report restoration results. Corrected observer refuses unknown host
breakpoints/steps, permits only the initial loader breakpoint outside the
pinned image before any syscall/ERET, checks the deadline on every loop,
requires the watched CPU thread and the audited ERET caller RVA0x140272,
and reports exceptional restoration/detach results. Normal restoration API
failures refuse the capture. The complete fresh13-case matrix and two reduced
repeats succeed with the corrected observer. Pilot artifacts
remain separate and are not relabelled as qualified captures.

**Confirmed, positive control:** at SYSCALL entry guest PC is preincremented
0x100068 and code is0x0C; actual instruction is0x100064. Vector selection is
0x80000180/EPC0x100064/BD0/EXL1. BIOS selects EPC0x100068; ERET instruction
is0x80000328, even though its host entry sees PC0x8000032C. The host function
returns to caller RVA0x140272 with PC0x100068/EXL0/v0=1. A later completed
marker s1=0x11 proves guest continuation. A PC write inside ERET alone would
not prove its status update or subsequent guest execution.

## Qualified syscall controls

B=0x100060, S=B+4=0x100064, fallthrough S+4=0x100068,
target0x100074, latch jump0x100078. Observer's latch event stops at the
preincrement to0x10007C before that jump has completed. It confirms the
earlier marker effect, not execution of the latch jump itself.

All seven syscall cases completed with the corrected observer, Cpu=intCpu,
one watched thread per case, no unexpected host breakpoint/single-step,
restored instrumentation and normal isolated shutdown. First external
breakpoint identity is operational (location/order), not independent proof
of ntdll!DbgBreakPoint. No new guest breakpoint mechanism was enabled.

| Original fixture | First vector EPC / BD | Actual ERET return | Subsequent observation |
|---|---|---|---|
| plain syscall | S /0 | S+4, EXL0/v0=1 | marker s1=0x11, latch fetch |
| BEQ taken syscall | B /1 | S, EXL0/v0=1 | repeated syscall, bounded TLB chain |
| BEQ false syscall | S /0 | S+4, EXL0/v0=1 | marker s1=0x11, latch fetch |
| BGEZ taken syscall | B /1 | S, EXL0/v0=1 | repeated syscall, bounded TLB chain |
| BGEZ false syscall | S /0 | S+4, EXL0/v0=1 | marker s1=0x11, latch fetch |
| BEQL taken syscall | B /1 | S, EXL0/v0=1 | repeated syscall, bounded TLB chain |
| BEQL false syscall | no target trap | no target ERET | slot annulled; v0=-1/s1=0x11 |

**Confirmed, taken chain** (common sequence numbers in the three qualified
captures; every row is a host state observation):

| Sequence | Observation |
|---:|---|
|18| target SYSCALL host entry, PC0x100068/code0x0C/v1=0x2F/branch1 |
|23| vector0x80000180, EPC=B/Cause0x80000020/EXL1 |
|60| MTC0 EPC (code0x409A7000 at guest0x800002E4) selects S |
|63| v1 now0xBC (=0x2F shifted left2) |
|85| ERET host entry, instruction0x80000328, preincremented PC0x8000032C |
|87| ERET host return at RVA0x140272: PC=S/EXL0/v0=1/v1=0xBC |
|89–93| SYSCALL repeats at S, console confirms v1=0xBC/branch0; second vector EPC=S/BD0 |
|130–133| second BIOS entry selects EPC=S+4 and scales v1 to0x2F0 |
|137–138| JALR k0 at guest0x800002FC, slot fetched, transfer selects PC0 |
|141–144| Cause0x08/EPC0, TLB read/fetch exception vector0x80000000 |
|299–302| later LW0x8CA20000 at0x800148E0, new Cause0x08/EPC0x800148E0 |
|2000| bounded_failure_window; no fixture marker continuation |

No copied BIOS implementation is committed. The two MTC0 words independently
decode as opcode0x10/rs4/rt26/rd14: MTC0 k0,EPC. Source explains interpreter
PC preincrement, branch clearing on exception and ERET's use of selected EPC.
Offline caller disassembly confirms call[rax+0x20] at RVA0x14026F returns to
0x140272 before the next PC increment/fetch. These checks are independent of
the GT4Recomp implementation, but all live results qualify this emulator/BIOS.

The PC watch at sequence137 occurs before the slot fetch updates code:
PC0x80000304/code0x0340F809 therefore combines the slot's preincrement with
the preceding JALR. Do not subtract4 uniformly from PC to assign code's
address. Link register0x80000304 agrees with JALR at0x800002FC plus8.
An additional offline check of hash-pinned slice100 plain-continuation RAM
(SHA25674d52a86..., full hash in its evidence) independently confirms LW at
0x800002F8, JALR at0x800002FC, NOP slot0x80000300, MTC0 at0x800002E4/310
and ERET at0x80000328. This checks word placement in the same BIOS; it is not
a new live event or an assumption about hidden save-structure layout.

**High confidence:** the second table dispatch yields an effective zero
target. The trace shows the load/calculation/JALR->0, but k0 and bytes of the
table entry were not captured. Do not call that entry a confirmed memory read.
The 2,000-record bound counts observations, not guest instructions or cycles;
it does not prove permanent failure. Later BIOS s0/s1 values are not fixture
markers. `target_syscalls=1` counts only service0x2F, not the repeated0xBC.

The three false/taken families characterize the pinned engine, not a universal
architectural BD rule. Slice100 dynarec and slice101 interpreter disagree on
vector delivery; neither is adopted as a hardware oracle.

## Qualified effect and annulment controls

All six original effect cases completed with normal shutdown and qualified
engine/thread. These run ADDIU s0,s0,1 instead of the target syscall. At the
terminal latch-fetch observation, v0 remains-1 and EXL is clear; no target
service0x2F was entered. Read the marker after the effect, not at PC=S's
preincrement before slot fetch.

| Effect fixture | Completed s0 effect | Later s1 marker |
|---|---:|---:|
| BEQ taken |1|0x22|
| BEQ false |1|0x11|
| BGEZ taken |1|0x22|
| BGEZ false |1|0x11|
| BEQL taken |1|0x22|
| BEQL false |0|0x11|

The separate BEQL-false syscall control also has no target trap, v0=-1 and
s1=0x11. This distinguishes annulment from a failed handler. Original ELF
hashes are checked before every run; the offline ledger reader additionally
checks trace hashes, sequence continuity, engine/thread, vector/return,
failure-chain landmarks and terminal effects for all13 cases.

## Production implications and remaining questions

No runtime, recompiler, clock, checkpoint or semantic identity changed.
The fake0x42 likely-slot characterization still reports native5/1 and
interpreter1/0: the native shortcut accepts/resumes while the interpreter
stops before acceptance with its transfer pending. Neither executes this
BIOS. The new reference result disproves an assumed normal
S+4/branch-target return for these taken controls; it does not select a
replacement production shortcut. Decision0041 still requires explicit
pending context, comparator/serialization coverage and semantic identity
review for a future resumable-slot change.

**Unknown:** physical PS2 correctness, other BIOS/services, whether GT4's
matched update path contains such a slot syscall, and its interrupt/event
phase alignment. No matched real update1000-us/root-work interval was sampled.
Do not use these host pauses or synthetic service0x2F as clock evidence.
Next bounded experiment: match that real interval in the pinned reference,
qualifying the engine per segment; separately scope any slot-resume contract.

## Reduced repeats, preservation and acceptance

Two serial repeats use service-only mode: hardware watches arm at target
SYSCALL, excluding the SetupThread-return observation window. Original ELF,
reference, observer and all six isolated BIOS hashes checked before launch.
Plain's entire76-record target-forward window and BEQL-taken's first500
records match their qualified matrix capture exactly after removing only
thread ID/sequence index. Cycle/register/host-RVA/watch-mask fields are also
identical. Reduced BEQL stops at the same2,000-observation bound, not the
same terminal guest state: omitting the first18 records extends the later
observed BIOS window. This is a bounded instrumentation control, not a
host-time measurement or hardware proof.

Independent reviewer re-read all13 captures without importing the ledger
writer: hashes agree with manifests and the published slice100 original-ELF
table; engine/thread/sequence/return/effect/failure landmarks all agree.
All13 effective INIs retain EE interpreter, renderer13, cards disabled,
patches/gamefixes disabled and private case directories. No unexpected host
breakpoint/single-step was consumed in accepted runs.

Owner INI/slot9/backup and six original BIOS hashes remain identical to
preparation; all78 isolated BIOS copies also match. Reference/PDB hashes
rechecked. No PCSX2 process remains. No reference game saves were overwritten;
the isolated traces/metadata/logs and refused pilots remain ignored locally.

Seventeen original helper source/script/executable files archived under
`private/pcsx2/sstates/slice101-interpreter-trap-observation/helpers/` and
checked against their tooling originals. Full distributable metadata:
[`docs/inputs/slice101-interpreter-traps.json`](../inputs/slice101-interpreter-traps.json),
SHA256`113a0a8f55d85ab1fefffdcfd9254755fbf81c1a259f9b6a5ae7ba994ada3d19`.
It pins15 accepted traces,13 original ELFs,17 helpers and five source files.
Observer source SHA256
`26ad0d82149b8af5d9a2b9dbfe297327ef5cc846bafe04a1da8cceb1abbeba40`;
observer executable SHA256
`73332aae5efbe59afcb930f8695b9a4f741b176a2d644162cf71524e0ec8610f`.
No BIOS/game/tool payload or raw trace is in git.

Reproduction on this preserved workspace: use archived `host_observer.exe`
with pinned reference executable, absolute isolated case directory and a
new absolute output path; optional fourth argument `service-only` narrows
the window. Ordinary invocation is:

```text
host_observer.exe <pinned-pcsx2.exe> <absolute-isolated-case> <new-trace.jsonl> [service-only]
```

The helper rejects output overwrite and unqualified original bytes/engine/
thread/caller. The preparation/runner/ledger/check scripts and build command
are preserved with hashes. Re-running a runner with its existing output name
is refused; use a new explicitly isolated experiment and requalify identities.
Do not copy this observation method to a changed binary by guessing offsets.

Input verification before project suites: CORE SHA256`85d26aa8...` against
disc manifest, native analysis ELF SHA256`10f82e22...` against native manifest,
ISO SHA256`67b6c007...` against disc manifest (full hashes in existing manifests).
The optional M3 reference ELF remains absent, explaining six existing skips.
Build: warning-free/no work. CTest **53/53 (50.37s)**; Python **91 collected,
85 executed/6 skipped (69.789s)**, existing socket ResourceWarnings. Suites
ran serially on their own binaries; some ran while unrelated isolated PCSX2
controls were active, so their wall times are not throughput comparisons.
All acceptance expressions are unchanged. Production gt4boot SHA256 remains
`4c1dd903ee1bdc82d516e714fc5107ef448b34269ffbf83de61fa573d17230ba`.

**BUILD:** original private observer compiled warning-free; production unchanged.
**VERIFY:**15 accepted bounded controls, independent review and existing full gates.
**EXPLAIN:** [worked lesson](../lessons/slice101-bios-return-is-an-observation.md),
[decision0042](../decisions/0042-symbol-qualified-interpreter-traps.md).
Slice102 has not started. It should match a real update1000-us/root interval,
including phase/engine qualifications, before considering a time policy.

Official references:
- [Pinned release and symbols](https://github.com/PCSX2/pcsx2/releases/tag/v2.9.114)
- [Interpreter source](https://github.com/PCSX2/pcsx2/blob/aa7ab4306e269075784c7ac3eb4b45e6e6c53445/pcsx2/Interpreter.cpp)
- [Windows debugger event contract](https://learn.microsoft.com/en-us/windows/win32/debug/writing-the-debugger-s-main-loop)
