# Original Engine Archive

Prior to cleanup, branch
[`codex/archive-original-recomp-20261013`](https://github.com/moesuito/gt4-staticrecomp/tree/codex/archive-original-recomp-20261013)
was pushed to origin at commit `95f615519b992c65505d77da35af825fc6a13a53`.
It preserves the complete tracked state: original recompiler engine, tools, tests,
plans, handoffs, and all preceding evidence, including the native slice 113 base.
Prior Git history remains intact; no commits were rewritten.

## Removed from main

411 legacy files: `src/` and `include/` engine sources, `tools/gt4*` tooling and
dedicated helpers, C++/Python tests specific to that engine, Ghidra/PCSX2
observation scripts, and superseded documentation, plans, and handoffs.
This work remains accessible on the archive branch, but is not a second
active product target and does not appear in `main` build/test commands.

Vulkan, Win32, audio, and XInput code was moved from `experimental/vulkan/` to
`native/`. Scripts, tests, and manifests used by the active path were retained,
along with reports, decisions, and lessons 110–113 documenting the migration.

Legacy links, paths, and gates in those historical reports and manifests serve as evidence
from that point in time; removed source files now reside on the archive branch
or within private capsules. Do not edit historical pins to reflect a cleanup.

## Ignored Material

The archive branch preserves tracked files only. The ISO, BIOS, CORE/ELF files, compiled libraries,
generated code, local tooling, captures, and memory cards were never tracked in Git.
All were preserved in their local filesystem paths, including original engine
outputs required for historical evidence. Legacy `build/` and `generated/` directories are inactive
local paths, not active build targets; they were neither deleted nor re-linked to `main`.
Save files and the owner's manual play window were untouched by this cleanup.

## Inspection and Restoration

Inspect the branch on GitHub without switching the active local checkout. To restore a
specific file, locate its path on the archive branch and submit a documented change;
do not revert the entire repository to the legacy state without rationale. A separate
checkout can rebuild the archived engine should an investigation require it.
Private inputs continue to require their original verification checks and compatibility constraints.
