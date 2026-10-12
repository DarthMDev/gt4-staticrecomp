# 0049 — Qualify the extra frame's writer and local video phase

Date: 2026-10-12; slice 108. Status: accepted observation only.

The model's N1300 notify GetThreadId charge completes the shared BUSCLK
accumulator, queues INTC2 and installs guest handler 4AB430 over thread 3,
saved PC 5ADB98/RA 5768F0. Bridge delay-slot store 4AB4AC writes frame 3 -> 4
while the same main receiver remains queued. This identifies the local
counter writer and interrupt ownership before message release.

The corresponding reference interval stays 3/3 in GS blank after its local
VBlank start. Matched official PDB layouts and source distinguish delayed GS
deadline from the following INTC2. Forty windows/280 states and reduced
120/120 exact common states qualify the new phase observations.

Use private logging copies with explicit diagnostic provenance, safe reads,
pending-slot observation and a link map. Compile the unchanged comparator
under the same private kernel declaration. Accept only after independent
full checkpoint framing, fresh production and repeat controls: all guest
state/models/services exact; 47/47 records and repeat checkpoint exact.

Trade-off: additional private observation code needs these controls, but
attributes the actual store/owner without modifying production interfaces,
whole-game translation or time policy. Rejected first ODR build and superseded
no-map capture remain pinned separately. Production gates are inherited from
107; no runtime change requires or receives a new semantic model identity.

The observed service crossing explains this local extra frame; it does not
align elapsed boot time or justify a universal replacement quantum. CP0
Cause/EPC alone are not ownership evidence in the model's non-BIOS handler
frame. Earlier phase differences and the matched 1000us reference wait remain
open. Next 109 qualifies earlier registration/receive/handler anchors and
non-service work before a time-policy decision. Correct IRQ return stays.
