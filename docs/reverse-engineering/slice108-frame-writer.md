# Slice 108 — exact frame writer and interrupt ownership

2026-10-12; baseline `55dc5c7`, branch `slice108-frame-writer`.
Decision 0049 accepts observation only. Production source, clock, IRQ return,
semantic identities and executable are unchanged.

## Result and claim boundaries

**Confirmed:** the model's first message notify GetThreadId, service N1300,
completes the shared BUSCLK accumulator. Its existing 147456-tick charge
queues INTC cause 2. At the next clean instruction boundary the kernel
installs handler `004AB430` over update thread 3, saving PC `005ADB98` and
RA `005768F0`. The bridge executes `sd v1,0(s1)` at `004AB4AC`, changing
scratch counter `70002050` from 3 to 4 while synced remains 3 and the same
main receiver node remains `{next=0,thread=1}`. Private service `100` is
the interrupt return, restoring that update continuation; it is not idle.

**Confirmed:** the corresponding independently qualified reference interval
remains guest frame/synced 3/3 in GS blank, after its local VBlank start.
The next delayed GS event is still in the future. That event is not the
next INTC2/VBlank start. All new phase fields repeat exactly.

**Unknown:** the first earlier source of the phase difference and a complete
reference-backed replacement for the model's time policy. Matching the
local caller/queue interval does not align elapsed boot time. Earlier receive
states were already different in 107 and remain relevant. No functional
boot or clock fix is claimed.

## Inputs, tools and identities

All payload and capture bytes remain private. Metadata:
`docs/inputs/slice108-frame-writer.json`, SHA256
`6420d600444ac476894ae66ac533cc1023bd583b5d712562a1c9c16a0386d30f`.

| Input/tool | SHA256 or pinned identity |
|---|---|
| USA v2.00 ISO | `67b6c0075837f3ae1132d608acf2858bf13b2dd62d6eae83dff76df02e4e824f` |
| CORE.GT4 | `85d26aa8430154967b2633eede929286694ac39e99762527edcec365fd642ff9` |
| Analysis ELF | `10f82e2231a51404b95682ed3ea81171100a1af2fefdeed3391943016c7c935c` |
| Production gt4boot | `839adaa22f48ba674fc8f5f6c498299a9329598dedf23ca3ee507f1f1c76dfab` |
| Native disassembler | `474a743a98282c31ffbe7aa56a2ace7975661dd45f0890678202659ad73d998d` |
| Final private model observer | `aea2b359202b286c16670529ffe3e9e8ac4d31edcbf71b6b9873f28938f27fcc` |
| Final link map | `5c70f92e293e4a167005a383fd9e8e6784911d4848bc4154815cc95fbbcddc81` |
| PCSX2 executable | v2.9.114, `0506a196f5bc76d47f618e68d40519c766bbf5959b8aa88d5b91cee1dcb48037` |
| Official matched PDB | `99b483451a79da4237585aa9da2ffc9a844dcd128e8628921a0f12832336de6f` |
| PDB CodeView | GUID `7833e0ae-b1fc-c437-4c4c-44205044422e`, age 1 |
| Reference source | `aa7ab4306e269075784c7ac3eb4b45e6e6c53445` |

VS 17.14.34/MSVC 19.44.35228.0, C++20; private reference/probe compile
with W4, model uses production debug flags. VS amd64 plus `chcp 65001`
was used. Accepted builds have no warnings/errors; model relink adds
`/DEBUG /INCREMENTAL:NO /MAP` for object attribution. Model identities stay
time 3, interrupt 4, kernel 3, RPC 2, translation 2.

## Independent native writer qualification

The ELF program-header mapping has LOAD offset `1000`, guest base `100000`;
there is no section table. An independent manual word decode and the CORE
disassembler agree:

| Guest PC | ELF offset | Instruction/effect |
|---|---|---|
| `4AB434/438/440` | `3AC434/438/440` | Form `s1=70002050` |
| `4AB4A0` | `3AC4A0` | `ld v1,0(s1)` |
| `4AB4A4` | `3AC4A4` | `daddiu v1,v1,1` |
| `4AB4A8` | `3AC4A8` | `beq a2,zero,4AB4C0` |
| `4AB4AC` | `3AC4AC` | `sd v1,0(s1)`, word `FE230000`, ordinary delay slot |

Registration `4A050C..4A051C`, call `4A0518`, supplies cause 2 and handler
`4AB430` to wrapper `5AD980`, service `10`/AddIntcHandler. Static registration
and generated registration agree. The handler body is absent from the
whole translated module; its live store therefore executes in the bridge.
The captured pending-transfer state independently qualifies the slot.

Historical `4AB6D8` registrations at `4A0E20..4A0E5C` use wrapper `5AD9B0`,
service `12`/AddDmacHandler, channels 0/1/2. They must not be described as a
second INTC cause-2 handler in the current model with separated domains.

Commands used; their disassembly output is retained privately:

```powershell
build/gt4disasm.exe private/fingerprint-check/CORE.GT4 0x004AB430 70
build/gt4disasm.exe private/fingerprint-check/CORE.GT4 0x004A04F0 32
build/gt4disasm.exe private/fingerprint-check/CORE.GT4 0x005AD980 24
build/gt4disasm.exe private/fingerprint-check/CORE.GT4 0x004A0E20 34
```

## Model causal chain

The private observer logs services 1298..1303, before/after the handler,
before/after charge, continuation, INTC/DMAC selection and frame installation.
An address-specific hook records the wide RAM store before and after its
original effect. Bridge records observe pending transfer around A8/AC/C0.

| Point | PC / frame-synced | BUSCLK accumulator / ownership |
|---|---|---|
| N1300 before GetThreadId | `5ADB94`, 3/3, RA `5768F0` | 2310144, accepted 1299, queue empty |
| After handler, before accepted-service accounting | `5ADB94`, 3/3, return 3 | accepted 1299; still 2310144 |
| Before charge, after accepted-service accounting | `5ADB94`, 3/3 | accepted 1300; still 2310144 |
| After 147456-tick charge | `5ADB94`, 3/3 | residue 0; queue `[INTC2]` |
| Continuation / dispatch | `5ADB98`, 3/3 | thread 3, eligible CP0/mask |
| Handler installed | `4AB430`, 3/3 | cause 2, argument 4, saved PC `5ADB98` |
| Slot store before / after | `4AB4AC`, 3/3 -> 4/3 | same deferred owner and receiver; residue 0 |
| Slot completed | `4AB4C0`, 4/3 | pending false; completed work 7466064 |
| N1301 private return | `1604`, 4/3 | restores saved update PC/thread; its charge leaves 147456 |
| N1302 release getter | `5ADB94`, 4/3, RA `576674` | receiver moved to pending; residue 147456 |

Independent arithmetic predicts the observed crossing: service charge is
3/50 of the model frame; after 1299 charges the residue is 47/50 frame,
or 2310144. Idle contributes exactly a complete frame, preserving this
residue. N1300 completes it. This is a deterministic model-policy relation,
not a physical-duration or reference-cycle conversion.

Both store hooks record work 7466063 and PC `4AB4AC`; the bridge increments
work only after the successful slot and clears pending. No second write,
queue ownership change or scheduler wake is needed to explain this increment.
The captured handler chain contains exactly one pair `{4AB430,4}`.

CP0 Status changes `70030C11 -> 70020C11` on installation and is restored on
return. Cause/EPC stay zero. The model copies CP0 and clears EIE; it does not
execute a BIOS vector that writes Cause/EPC/EXL. The deferred frame is the
authoritative interrupted owner/PC here. INTC STAT remains `804` even before
this new occurrence; the empty -> INTC2 software queue proves the occurrence,
not a new rising edge inferred from that already latched status bit.

## Independent reference phase

The new offline DbgHelp probe reads the matched official PDB, resolves
typedefs with `TI_GET_TYPEID`, and obtains sizes/field offsets independently
of the observer. The source explains their meaning:

| Field | RVA / bytes | Qualified layout |
|---|---|---|
| vsync / hsync | `E79E18 / E79E00`, 24 each | Mode 0, startCycle 8, deltaCycles 16 |
| vSyncInfo | `E79E30`, 40 | rate 0, mode 8, Render C, Blank 10, GSBlank 14, h fields 18..24 |
| Counter scheduler | `E79EE0`, 16 | signed delta 0, padding 4, start 8 |
| Host frame count | `E79EF0`, 4 | VBlank-end host count |
| INTC / DMAC | `eeHw RVA168F000 + F000 / E010` | INTC STAT/MASK 0/10; DMAC STAT |

`Counters.cpp` SHA `eea781811f3e3a20b0caa875248b2f15555e674c571fd742fb2bbb94331d5a32`,
`Counters.h` SHA `d59f8d6a73b1e67e05d36375544b2ce574efed874dc9375f579e0d4152caa609`.
Source 489/516 signals VBlank start/INTC2, 569/579/582 signals end/host count/
INTC3; 598..630 selects render 0 -> GS blank 2 -> VBlank 1.

Fresh qualified reference: 40 complete windows, 280 states; reduced repeat
120/120 common states exact, including all new fields. Only sequence,
host thread and host RVA are excluded. All 80 block debits retain the
independently decoded 18-byte load/add2/store prefix and RIP return+18;
later generated branches are not inherited from earlier COFF samples.

Q126 notify, Q132 materialized event, Q133 release and Q139 materialized
event all have mode 2, start 1810604814, GS delta 65601, guest frame/synced
3/3, host frame 368, INTC STAT 8/MASK 6183, DMAC STAT `00A70040`.

| State | Observed cycle | Endpoint distance to delayed GS event |
|---|---:|---:|
| Q126 notify raw entry | 1810613441 | 56974 |
| Q132 materialized event | 1810613508 | 56907 |
| Q133 release raw entry | 1810613592 | 56823 |
| Q139 materialized event | 1810613659 | 56756 |

Delayed GS deadline is 1810670415, **not a new INTC2**. With recorded Blank
421724 and Render 4498396 unchanged, the subsequent VBlank start derives as
1815524934. These are local reference phase/deadline quantities. Raw entries
are not complete costs; no ms conversion, universal branch behavior, or
absolute model/reference time alignment is adopted. Host frame 368 is not
guest counter 3. Source meaning plus observed phase does not constitute a
capture of the earlier reference writer itself.

## Qualification, preservation and rejected attempts

The observer adds only logging insertions to private main/driver/kernel/state
copies and a private member declaration without fields/virtuality. The
unchanged comparator is also compiled with that declaration to avoid an
ODR mismatch. Final link map attributes the four implementation objects to
the private copies and excludes the original archive versions. No whole
module regeneration, runtime setter, extra service, clock change or new
guest instruction is introduced. All fixed reads are RAM/scratch; receiver
reads are explicitly RAM/alignment bounded; device getters are const member
inspection rather than MMIO reads.

Independent CPT framing confirms model IDs, 10000 services and all state
sections exactly equal to fresh production: RAM/context 33580532 bytes,
kernel 8212, banks 496. Only binary provenance differs, explicitly prefixed
`slice108-read-only-observer `. Final repeat checkpoint is entirely identical,
and 47/47 observation records match without exclusions. Fresh production
checkpoint is entirely identical to actual 107 producer. Work 8946341 and
full state match the interpreter in all three fresh runs; shared-model
consistency is not PS2 environment fidelity. Diagnostic checkpoints are not
resumed as production. Fresh boot/count-work is the qualified selection.

Production 54/54 CTest and Python 97 (91 run/6 skip) gates are inherited
unchanged from 107, not rerun in 108. New private observations have automated
causal, phase, repeat, map and framing checks. Independent review confirmed
native decode, read-only source changes, final map/framing and phase meaning.

First preparation stopped on a wrong generated-header name; its empty directory
is retained. First private build missed comparator recompilation under the
private declaration: rejected for ODR mismatch, never executed, sources/logs/
binary retained. PDB child queries stopped at typedefs; resolving the aliases
qualified the layout. A missing enum include also failed compilation explicitly.
Initial no-map capture passed state control but is retained as superseded by
the final mapped producer/capture, with its own binary identity. No failure
was hidden or production acceptance weakened.

25 helper files and 54 artifacts are hash-pinned. All 3 owner files, 6 original
BIOS files and 12 isolated copies were checked before/after and preserved.
Both reference processes closed normally; no remaining reference process.
No owner INI/save or game manifest was updated.

## Next experiment

**Slice 109:** qualify the earlier receive -> first-update frame phase with
native registration/handler/caller/queue anchors. Capture the reference frame
writer/INTC2 context and corresponding model charge history, including work
outside services. Locate the first preceding phase divergence before choosing
any reference-backed time policy. The matched 1000us update wait remains open.
Keep the correct IRQ return and current clock until a replacement is justified.

Accepted capture commands are retained in the archived build/preparation/
qualification helpers. Model: fresh strict/count-work/compare-interpreter
10000 services. Reference: the same bounded real-disc dynarec route as 107,
fresh isolated `slice108-frame-writer/qualified1` and `reduced1`, with seven
new fixed read-only fields. Existing outputs refuse overwrite; a new experiment
requires fresh directories and separately qualified identities.
