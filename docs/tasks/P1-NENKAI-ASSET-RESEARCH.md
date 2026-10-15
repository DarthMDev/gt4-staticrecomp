# P1-119 — Nenkai References and Bounded GT4 Asset Research

## Authority and scope

The owner explicitly delegated this task to GPT-6 Luna on 2026-10-13, replacing
the proposed Gemini/Antigravity assignment. Parent is the only tracked repository
writer and integration owner. Workers inspect sources and perform bounded offline
experiments in their own private directories. No external provider setup or
unattended recurring work is authorized by this task.

Baseline: `dc46c0b`, after slice 118 candidate preparation and renderer scope
clarification. Read `docs/STATUS.md`, `CLAUDE.md`, `docs/CAR-ASSET-RESEARCH.md`,
`docs/RENDER-CONTRACTS.md` and `docs/inputs/slice118-volume-catalogue.json` first.
P1 remains in progress. This task does not implement the renderer or its live hooks.

## Environment

- Integration checkout:
  `C:/Users/Alano/.codex/worktrees/native-renderer-roadmap/gt4-staticrecomp`.
- Original assets/OpenCode checkout: `C:/Antigravity/gt4-staticrecomp`.
  Its tracked files and active branch are outside worker write authority.
- Python: `C:/Antigravity/gt4-staticrecomp/private/tooling-venv/Scripts/python.exe`.
- Fresh capsule: `private/slice119-nenkai-asset-research/` in the integration checkout.
- Sources: capsule `references/`; worker outputs: `luna/` and `model-inventory/`.
- Existing source pins and qualifications remain frozen. Do not repin inputs to
  accommodate an unexpected observation. Public outputs contain metadata only.

## Work packages

1. **RoFS researcher — Luna, high effort:** resolve page codec/order, packed
   version, node layout and file extents using pinned primary references. Derive
   a bounded private catalogue experiment and controls. Recover only the catalogue
   paths relevant to a menu car and the GT4-specific identity database. A coded
   filename is a candidate, not proof of a marketed car or live instance.
2. **Model/reference inventory — Luna, medium effort:** map model, geometry,
   texture, callback/VM and SpecDB source files to the upcoming car contract.
   Record per-file and repository licenses plus relevant dependency limitations.
   No game-data reads are needed for this package.
3. **Parent integration:** clone relevant repositories into ignored paths, pin
   revisions/provenance, independently check important findings and publish the
   reviewed evidence ledger. Reference source trees remain unmodified.

## Input contract and bounds

Verify the owner's ISO against existing public input pins before catalogue reads.
Pinned SHA-256: `67b6c0075837f3ae1132d608acf2858bf13b2dd62d6eae83dff76df02e4e824f`;
size: 5,314,478,080 bytes. Hash streaming is recorded separately from catalogue
read budgets. Do not modify the ISO, game executable or any asset.

Known slice 118 metadata: VOL begins at ISO byte 216,840,192, length
2,459,502,592; real table at VOL byte 17,483,776. Header is 64 bytes;
offset table contains 408 u32 entries; logical pages number 407 and page size
is 2,048. Verify these fields; do not treat prior metadata as a codec oracle.

Additional unique catalogue/window reads are capped at **2 MiB**, decoded
catalogue output at **2 MiB**. Validate limits before reads/allocation. No asset
payload extraction or unrestricted extractor invocation in this package.
Do not brute-force codec guesses after the reference-derived contract fails;
report the first discrepancy and the smallest evidence needed to resolve it.

## Preconditions, effects and controls

Read-only ISO access; explicit extents, endian order and integer wrapping.
Require bounded decompression with exact page size and consumed input. Slice 119
observed 406 non-final raw-DEFLATE segments ending in an empty stored-block flush
suffix and one final stream; do not require `eof` on those qualified segments or
silently accept arbitrary truncation. Record terminal/flush behavior and limits.
Validate
page/record ranges, page links, node types, directory identity, root selection,
parent connectivity/cycles and file extents before interpreting candidate paths.
Synthetic controls must cover valid codec order and malformed/truncated streams,
output caps and relevant structural bounds. Independent expected values derive
from primary format facts and manually specified fixtures.

Worker reports include exact references/revisions/line ranges, hashes, byte
counts, methods, test results and limits. Keep complete decoded catalogues,
experiments and game bytes ignored. No unsupported path may silently disappear.
Catalogue decoding does not qualify geometry, car names, live callbacks or PS2
fidelity. Use **Confirmed / High confidence / Hypothesis / Unknown** labels.

## Stop conditions and review

Stop on hash mismatch, exhausted bounds, contradictory source/data semantics,
missing provenance or a required write outside the allowed private directories.
Return an explicit unknown rather than relaxing acceptance or guessing identity.
Do not copy unlicensed/GPL implementations into the active MIT-derived runtime;
reference clones are not linked dependencies. Building a reference CLI or adopting
its code requires a separate license/dependency and extraction-contract review.

No Computer Use, GUI automation, simulated input, scripted navigation, runtime
hooks, performance probes or input fixes. Preserve owner cards, backups, locks,
active sessions, frozen producers and OpenCode work. No game launch/closure is
needed. Owner gameplay checkpoints follow future live integration, not this task.

English reports/comments only. Workers cannot commit or push. Parent reviews and
publishes distributable metadata/documentation together; implementation proposals
remain private until separately reviewed and tested for integration.
