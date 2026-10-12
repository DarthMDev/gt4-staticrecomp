# 0046 — Qualify and implement the pinned CDVD_INIT mode-zero reply

Date:2026-10-12; slice105. Status: accepted.

Replace the empty response for80000592/function0 with the observed four-word
reply only for request mode0, send4/recv16 and mapped request/receive buffers.
Unsupported modes or layouts stop with useful context in both strict and
default policies. Other functions remain unknown. Classification identifies
the pair; the response handler separately qualifies its request variant.

Two real-disc dynarec completions show `{1,0x226,0x226,0}`; reduced observation
matches12/12 common states. Independent .iopmod/EXTINFO and the native server
callback agree on both versions and result. The diagnostic value is confirmed
live. The guest's version consumer is independently decoded; its final v0 has
not been captured. Identification of SID/function uses the pinned ELF/SDK
combined with the specific live client and buffers, not a live packet dump.

Reuse the existing SIF result transfer, RPC_END correlation fields and DMAC
completion. Do not wake a guest thread directly, execute IOP instructions,
claim complete lifecycle fidelity or introduce clock assumptions. A narrowly
evidenced observable contract is sufficient here; guessed minimum versions
or open-source SDK server constants would conceal the original error.

RPC semantic identity1->2 rejects old checkpoints before restoration. No
serialized state or format changes; time3/interrupt4/kernel3/translation2
remain. Tests cover full response/canaries, request DMA, correlation/DMAC,
strict/default equality, repeats/restored bind, malformed sizes/unmapped
buffers/unobserved modes, no response or completion on rejection, adjacent
unknown function and explicit RPC1 checkpoint refusal. Fresh boot and full
gates determine causal progress; this decision is not a boot claim.

Acceptance:54/54CTest and Python97(91 executed/6 skipped),90k/10k work and
full-state differentials identical. Fresh strict10k/100k have0unknown RPCs,
complete observed reply and0GIF bytes. Strict/default10k checkpoints are
byte-identical. Actual104RPC1 checkpoint is refused by the final executable.
Root remains READY64 at5ADBC8; update RUN24. No ordinary-root or functional
boot improvement is inferred from closing this unknown response.

The acceptance work uncovered stale main-object headers caused by mismatched
localized include-prefix bytes. English-only clean build did not fix it.
A narrowly scoped UTF-8 C/CXX include-metadata launcher, six fixtures, recorded
main dependencies16 and a header-change/dry-run control qualify the final
binary. Earlier mixed/zero-dependency builds are rejected. This prerequisite
is included in105 because checkpoint identity could otherwise differ across
objects. RC and other encoding environments remain audit debt. Exact input,
tool, binary, capture and17private-helper hashes are in105response metadata.

Evidence: [slice105 response](../reverse-engineering/slice105-cdvd-init-response.md),
[source protocol](../reverse-engineering/slice105-cdvd-init-contract.md).
