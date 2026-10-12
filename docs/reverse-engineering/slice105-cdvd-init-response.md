# Slice105 — real CDVD init reply and completion

Date:2026-10-12; branch `slice105-cdvd-init-response`, baselinead70361.
Runtime implementation and gates accepted. Stable functional boot remains open.

**Confirmed:** two fresh real-disc dynarec CDVD_INIT requests use mode0 and
complete with four words `{1, 0x0226, 0x0226, 0}`. Qualified capture has four
complete syscall windows/28 states; reduced capture has12 states. All12 common
states match exactly, excluding sequence, host thread id and host address.
Registers, cycles, RAM fields, stack and frame remain in the comparison.

Client874FA8, request875000, reply6570C0 are qualified against the pinned
native ELF10f82e22... and the accepted103 observer. Wait44/RA5B1B74 is paired
with completed DeleteSema41/RA5B1B7C. a0 equals client+8 and the same semaphore
within each pair:17 then21. Both requests remain mode0 at both boundaries.
The initial ERET selects idle81FC0; its buffer is not the completed reply.
SID80000592/function0/caller580E40 are qualified from the pinned ELF and SDK,
then associated with the specific live client/recv/send endpoints. The trace
does not directly capture the RPC packet's SID/function. Its128-byte stack
sample is shorter than the SifCallRpc0xC0 frame and omits that outer caller RA.
Before the second request the reused receive buffer contains unrelated old
fields. Only the matched completion establishes the second response. Seven
states at/after each selected DeleteSema boundary retain the same sixteen bytes.

**Confirmed offline:** the full pinned ISO matches its manifest; root entry
IOPRP300.IMG is atLBA1551/278465bytes. Its ROMDIR selects CDVDMAN(offset1E5E0,
85165bytes) and CDVDFSV(offset33290,33429bytes). Independently parsed .iopmod
and EXTINFO both declare0226 for both modules. Seventeen checked native words/
halfwords trace the real server callback694 to `{1,CDVDFSV version,CDVDMAN
sceCdSC(-9) version,diagnostic}`. CDVDFSV5534 and CDVDMANDA84 both hold0226.
The static diagnostic global is initially zero; live completion independently
establishes its observed zero reply. Module VAs are relative, before relocation.
Hashes and metadata are published in docs/inputs/slice105-cdvd-init-response.json;
all payloads/captures/helpers remain ignored in private.

**High confidence:** the game's consumer580CD8 reads +0C at580E98, +04 at
580EA4 and +08 in the branch slot580EB4, never word0. DiagnosticFE/FF bypasses
the ordinary version tests; otherwise it divides signed versions by256 toward
zero and reports1 when both major versions are at least2. The observed reply
therefore selects1. This is independently decoded behavior, not a live capture
of the routine's v0 at580F90. DeleteSema is not that return endpoint.

Commands run from repository root:

```powershell
private/tooling-venv/Scripts/python.exe private/tooling/slice105-audit-disc-modules.py
private/tooling-venv/Scripts/python.exe private/tooling/slice105-validate-live.py
private/tooling/pcsx2-slice105-observer/host_observer.exe private/pcsx2/pcsx2-v2.9.94/pcsx2-qt.exe private/pcsx2/sstates/slice105-cdvd-init-response/qualified1 private/pcsx2/sstates/slice105-cdvd-init-response/qualified1/trace.jsonl
private/tooling/pcsx2-slice105-observer/host_observer.exe private/pcsx2/pcsx2-v2.9.94/pcsx2-qt.exe private/pcsx2/sstates/slice105-cdvd-init-response/reduced1 private/pcsx2/sstates/slice105-cdvd-init-response/reduced1/trace.jsonl reduced
```

Reference PE/PDB/layout/source identities and dynarec flush/cycle-store
qualification are inherited from102/103. The new named GPRs/RAM ranges are
checked against the native caller. These are materialized boundary states,
not a guest instruction trace or a host-time measurement. No cycle conversion,
clock policy, forced wake or lifecycle change follows from the reply capture.

Both references closed normally. Three owner INI/save files, six original
BIOS files and twelve isolated BIOS copies retain the expected hashes. A
preparation command initially used the observer folder for a root-relative
Python path; it failed, then compiled the still-unconfigured clone. It was
corrected and rebuilt before either reference launch; no rejected build was
used for a capture. No owner files were altered.

**Unknown:** other request modes (including shutdown5), initialization failure,
diagnostic changes, live consumer return, complete IOP lifecycle fidelity and
the first matched1000us update interval. A bounded mode0 implementation must
reject unqualified variants, reuse the actual SIF endpacket/DMAC path, bump
RPC semantic identity and prove fresh boot behavior without weakening gates.

## Rejected incremental build and repair

The initial fullCTest53/53 and Python91(85/6) run is **not an accepted105
integration gate**. Actual old104 RPC1 checkpoint restoration unexpectedly
succeeded. The CDVD reply was new but gt4boot still announcedRPC1, despite
the header declaringRPC2: its main object was older than the changed header.
Ninja reported zero dependencies for that object. Direct byte inspection
proved generated rules used an OEM-encoded localized /showIncludes prefix,
while the compiler emitted UTF-8. Those different bytes failed to match;
header changes were silently ignored. Inline definitions could consequently
differ between translation units. The rejected model captures remain in
private105/model, with the initial gate logs and90k excerpt retained.

Requesting English diagnostics did not repair dependencies: this installation
only has Portuguese compiler resources. That clean binary announcedRPC2 and
refused RPC1, but Ninja still recorded zero dependencies. Its observations in
private105/accepted-model are also rejected as integration evidence; the folder
name does not establish acceptance. Initial AUTO decoding and missing root C
prefix attempts failed explicitly and were corrected before the final build.

The launcher now decodes UTF-8 compiler output and normalizes the detected
include prefix only at line starts to ASCII. Ordinary diagnostics are retained;
compiler failure stays failure, and unknown drive/UNC include metadata stops.
PDB-directory trailing backslashes become forward slashes before CMake-list
expansion. Semicolon or other trailing-backslash arguments stop explicitly.
Six input-independent fixtures cover these cases, including a path with spaces.
Root C/CXX detection supplies both prefixes; zlib C targets also use the
launcher. CMAKE_CONFIGURE_DEPENDS and its SHA256 in the launcher command ensure
changing this build tool invalidates compiler commands. Read-only review found
no remaining material issue in the observed C/CXX scope. RC cmcldeps locale
handling remains unaudited; no universal argument/encoding portability claimed.

New full gates, recorded main-object dependencies, a bounded header timestamp/
dry-run check and actual RPC1 checkpoint refusal are required before accepting
the final binary. No discarded checkpoint will be resumed for boot progress.

## Accepted implementation and final executable

SID80000592/function0 is classified implemented-verified, with the variant
checked separately: mapped request/receive, mode0, send4/recv16. Other modes,
sizes and unmapped buffers stop in both policies before reply or completion.
Four little-endian words use the existing result transfer/RPC_END/DMAC5 path;
no direct thread wake, timing change or IOP instruction execution was added.
RPC semantic identity2 refuses1; time3/interrupt4/kernel3/translation2 and
GT4CPT3/GT4KERN2 layouts remain. Synthetic fixtures cover real request DMA,
all16reply bytes/canaries, correlation fields, completion drain, restored bind,
both policies, malformed variants and unchanged16-byte/64-byte sentinels on
rejection. Adjacent function1 remains unknown. No gate was weakened.

Final Ninja main-object dependencies contain16project headers including
ee_checkpoint.hpp. Changing only that header's timestamp schedules main
recompilation; restoring its exact timestamp returns the dry-run to no work.
The final model-identity CTest independently requires RPC2 from gt4boot.
Actual104 RPC1 checkpoint restoration returns1 naming the RPC mismatch.
These checks accept the normalized build; earlier similarly named captures
remain rejected. Exact evidence is archived privately and hash-pinned publicly.

Full Developer-environment build/CTest54/54 passes197.75s. Python97tests,
91executed/6skip, passes63.473s; its existing socket ResourceWarning is retained
in the log, not a test failure. C/CXX build has no compiler warning/error.
Fresh final-binary runs:

| Run | Completed work | RPC pairs/unknown | GIF bytes | Frame/synced |
|---|---:|---:|---:|---:|
| strict10k | 8,946,341 | 2/0 | 0 | 526/524 |
| default10k | 8,946,341 | 2/0 | 0 | 526/524 |
| strict100k | 24,268,729 | 2/0 | 0 | 5926/5925 |
| CTest90k differential | 22,566,329 | not newly inspected | not newly inspected | not newly inspected |

Strict10k AOT/interpreter complete state and work agree;90k gate agrees too.
Strict/default10k entire checkpoint files are byte-identical. All three parsed
snapshots contain reply{1,550,550,0}, root READY64 with ordinary savedPC5ADBC8,
and update RUN24. No ordinary main continuation or functional boot is claimed.
The strict preexisting CDVD stop is removed; the frame-sync frontier persists.
Compared with104, work rises10instructions with the revised reply present;
independent instruction attribution was not captured. This is an engine-
consistency observation, not an independent word trace.

Final gt4boot SHA25632687d1173d5cb31d1b9b5003437ceb38b2bd20a98f1a005045c4e425233b3fc,
110391296bytes. CMake4.3.3/MSVC19.44.35228 x64/Python3.14.5; embedded
source commitad70361 plus tracked105diff and binary hash establish provenance.
Metadata SHA256efaa9ca6fa607b024eebdfde8205cd1bbf49eaaf341387b7754d5a837c1c1328.
Seventeen private helpers are archived with hashes, including the observer and
checkpoint reader. Original owner3files, original BIOS6 and isolated copies12
are rechecked unchanged; both references closed normally, none left running.

Final commands, after the isolated observations above:

```powershell
cmd /c private\tooling\slice105-verify.cmd
private/tooling-venv/Scripts/python.exe private/tooling/slice105-check-build-dependencies.py
private/tooling-venv/Scripts/python.exe private/tooling/slice105-pin-gate.py
private/tooling-venv/Scripts/python.exe -m unittest discover -s tests/python
cmd /c private\tooling\slice105-build-inspector.cmd
private/tooling-venv/Scripts/python.exe private/tooling/slice105-fresh-boot.py
private/tooling/slice105-inspect-stops.exe private/pcsx2/sstates/slice105-cdvd-init-response/verified-model/strict-10000.bin private/pcsx2/sstates/slice105-cdvd-init-response/verified-model/strict-100000.bin private/pcsx2/sstates/slice105-cdvd-init-response/verified-model/default-10000.bin
private/tooling-venv/Scripts/python.exe private/tooling/slice105-publish-metadata.py
```

Next106: bracket the first update frame-sync result after the qualified message
wake. Align controls6186DC/E4, input frame/synced/queue, caller/stack and actual
restored-main work. Priority restoreRA10799C exposes s5, the sync result later
used to choose the1000us delay; record the input branch before interpreting it.
An entry mismatch precedes the bracket and requires tracing earlier, not a
guessed clock quantum. The matched1000us interval remains open.
