# Slice105 preparation — CDVD init identity and next live observation

Date2026-10-12, baseline7f859fe. Source audit only; live observation and
implementation not started. Stable functional boot goal remains active.

Confirmed protocol identity: PS2SDK pin2c670453980fcc3fe46ead6399b730b12c8556eb
defines CD_SERVER_INIT=80000592; sceCdInit uses function0, sends4 mode bytes
and receives16 bytes. Source fields describe result/CDVDFSV version/CDVDMAN
version/diagnostic mode. Four primary source files downloaded to ignored
`private/tooling/slice105-source-pins`, hashes verified independently against
the read-only audit. Full source/input/tooling hashes and addresses:
`docs/inputs/slice105-cdvd-source-contract.json`.

[Client](https://github.com/ps2dev/ps2sdk/blob/2c670453980fcc3fe46ead6399b730b12c8556eb/ee/rpc/cdvd/src/libcdvd.c),
[packet layout](https://github.com/ps2dev/ps2sdk/blob/2c670453980fcc3fe46ead6399b730b12c8556eb/common/include/libcdvd-rpc.h),
[common definitions](https://github.com/ps2dev/ps2sdk/blob/2c670453980fcc3fe46ead6399b730b12c8556eb/common/include/libcdvd-common.h),
[open-source server](https://github.com/ps2dev/ps2sdk/blob/2c670453980fcc3fe46ead6399b730b12c8556eb/iop/cdvd/cdvdfsv/src/cdvdfsv.c).
The server derives from SDK3.1.0; its version/result constants do not prove
the Sony/GT4 module response. Do not copy them as a success stub.

Native ELF10f82e22... verified against public native manifest; disassembler
audit independently checked via direct little-endian words/JAL calculation.
Routine580CD8: client874FA8, send875000, recv6570C0. Bind caller580DAC;
call caller580E40. Sync wait caller5B1B74, s1=client/s4=recv/s5=send.
DeleteSema caller5B1B7C occurs after that waiting caller resumes. An initial
ERET that dispatches idle is not evidence that init completed.

The guest reads uncached reply+0C at580E98, +04 at580EA4, +08 at580EB4;
these govern diagnostics/version tests and the routine's return1/2. It does
not read word0 in this consumer. Actual four-word response, both request
modes, consumer return and completion timing are **unknown**. Captures102/103
omit this buffer and cannot supply the missing values.

Next bounded experiment: inherit accepted103 PE/PDB/dynarec/profile/CORE gate,
qualify new GPR and RAM buffer fields, capture request/client/reply at the
matching call/wait and completed DeleteSema. Stop after two qualified init
completions; repeat with less observation. Match modes/client/caller per
segment. If claiming sceCdInit return, capture a separate endpoint after the
consumer loads; DeleteSema alone only proves wait completion. No new clock
policy or guest wake. Retain owner INI/saves/BIOS hashes and normal shutdown.
