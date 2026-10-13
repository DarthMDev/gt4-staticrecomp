# Nenkai Reference Environment — Slice 119

2026-10-13. The owner delegated this research to GPT-6 Luna instead of using
Gemini/Antigravity. Two bounded workers handled catalogue decoding and model/
database source inventory. The parent prepared the environment, independently
replayed catalogue pages and reviewed the reports. This is P1 research, not a
renderer implementation or performance experiment.

## Local environment and provenance

Six repositories were cloned into the integration checkout's ignored
`private/slice119-nenkai-asset-research/references/`. All checkouts are pinned,
unmodified research references. No submodules/packages were downloaded, no
reference executable was built, and no library was linked into the game.
The asset checkout and existing owner-controlled game session were preserved.

| Repository | Pinned revision | Use and limits |
|---|---|---|
| [PDTools](https://github.com/Nenkai/PDTools/tree/ffe0bb26377ac9ff62f40951c1c0b16c5d9a380f) | `ffe0bb26377ac9ff62f40951c1c0b16c5d9a380f` | MIT model, texture, VIF and SpecDB research. Its readers still need bounds and selected-payload qualification. Dependencies have separate licenses. |
| [File specifications](https://github.com/Nenkai/GT-File-Specifications-Documentation/tree/05e52347890541758222d173dbb62d2b40581b19) | `05e52347890541758222d173dbb62d2b40581b19` | MIT templates for MDLS, CAR4, textures and model commands. Addresses from other revisions are not hooks for this executable. |
| [Modding Hub](https://github.com/Nenkai/gt-modding-hub/tree/b312010b54c89e774a6c23f5d37a9f6de8606812) | `b312010b54c89e774a6c23f5d37a9f6de8606812` | Primary published format research and GT4 directories. No root license found; documentation reference only. |
| [GT4FS](https://github.com/Razer2015/GT4FS/tree/b938f71fd939e0ea44c38839a50ffb66cfd2067a) | `b938f71fd939e0ea44c38839a50ffb66cfd2067a` | Nenkai's profile links this repository under Razer2015. RoFS format/order reference; no root license found. No implementation adopted or extractor run. |
| [GTPS2ModelTool](https://github.com/Nenkai/GTPS2ModelTool/tree/ef9aadbaa946b68f4ea7ba7cbb3437156f937714) | `ef9aadbaa946b68f4ea7ba7cbb3437156f937714` | MIT root and separate BSD-3-Clause SharpTriStrip. Primarily a GT3 custom-model builder; README says GT4 support is unfinished. Not a qualified GT4 extraction/conversion solution. |
| [GTSpecDB](https://github.com/Nenkai/GTSpecDB/tree/691e49fa6b85773bd4e8bc47e361c0a1cd6716a0) | `691e49fa6b85773bd4e8bc47e361c0a1cd6716a0` | MIT database editor/SQLite bridge. Source supplies schemas, not this ISO's vehicle-name/model-code mapping. GUI was not launched. |

Root license hashes, repository pins and reviewed metadata are recorded in
[slice 119 evidence](inputs/slice119-nenkai-assets.json). GTPS2ModelTool's root
`LICENSE.txt` retains placeholder copyright text; future redistribution needs
qualified attribution. PDTools submodule pins differ across these tools:
GTPS2ModelTool records `23afb2cea05ab83b097feaeadd2ff63460a41c6e`, GTSpecDB
records `4eb68cde09c4f694e1d4f5dc86dd7fad6f646208`. Neither submodule was populated
or substituted with the separately cloned PDTools revision. A bounded source
inventory is not a complete dependency/package-license audit.

## Source map for the next car contract

All paths below are relative to their pinned repository, not the active runtime.

| Question | Primary source locations | Required additional evidence |
|---|---|---|
| Volume pages and file extents | GT4FS `GT4FS.Core/Volume.cs`, `TocHeader.cs`, `Node.cs`, `Entries/`; Hub `docs/formats/volume/ps2_gt4_volume.md` | Production bounds, index-search semantics and capped payload extraction contract. |
| GT4 model set | PDTools `PDTools.Files/Models/PS2/ModelSet/ModelSet2.cs`, `ModelSet2Model.cs`; templates `Formats/PS2/GT4/GT4_MDLS_ModelSet.bt` | One selected MDLS payload, relocated pointers, resources, instances and live effects. |
| Vehicle wrapper/components | Templates `Formats/PS2/GT4/GT4_CAR4_CarInfo.bt` | Confirm the chosen resource family before assuming a CAR4 wrapper. |
| Geometry | PDTools `PDTools.Files/Models/PS2/PGLUshape.cs`, `VIFDescriptor.cs`, `VIFPacket.cs`, `VIFCommand.cs` | Packed data, strip breaks, bounds and coordinate conventions with independent controls. |
| Texture/material/colour state | PDTools `PDTools.Files/Textures/PS2/TextureSet1.cs`, `GSMemory.cs`, `GSPixelFormats/`; model-set material/variation tables | Palette, swizzling, colour/LOD and actual resource selection. Offline format decoding does not require executing the GS drawing path. |
| Model commands and callbacks | PDTools `PDTools.Files/Models/PS2/RenderCommands/`, `Models/VM/`; templates `Formats/Shared/ModelSetupCommands.bt`, `ModelVM.bt` | Preserve qualified callback/VM cadence, branch/pass selection and side effects before replacing draws. |
| Named-car crosswalk | PDTools `PDTools.SpecDB/Core/SpecDB.cs`, `Core/Mapping/Tables/CarName.cs`, `GenericCar.cs`, `CarVariation.cs`, `ModelInfo.cs`; GTSpecDB SQLite source | Exact USA v2.00 database rows and a demonstrated join to volume model labels; no GT6-derived substitute. |

The ModelSet2 reader declares `HeaderSize = 0x80`; its `HEADER - 0xE4` comment
conflicts with that constant and field reads. Templates and earlier executable
inspection support the 0x80 layout. Comments are research hints, not acceptance
values. No actual car payload was parsed in slice 119.

## Result and next boundary

**Confirmed:** bounded catalogue-page decoding now succeeds, with independently
matching decoded hash/counts and real car/SpecDB directory leads. See
[the asset ledger](CAR-ASSET-RESEARCH.md) for methods, controls and limits.

**Unknown:** a named car's model code, database identity, extracted resources,
geometry/texture correctness and live instance/camera contracts. A catalogue
filename or offline viewer cannot qualify native scene substitution.

The next package is a reviewed read-only catalogue/extraction tool with public
synthetic controls, followed by only the USA v2.00 database entries needed to
identify one car, then that model's bounded resources. The experimental private
reader has not been adopted as production tooling. No P2/P3 renderer, live hook,
timing change, deferred rendering or graphical remaster is implemented here.

The reusable worker scope is [P1-119](tasks/P1-NENKAI-ASSET-RESEARCH.md). Keep
frozen capsule/source pins; a new extraction attempt uses a new output identity.

## Slice 120 — Reusable reader and file-payload framing

[Bounded volume tooling](VOLUME-READER.md) now reproduces the catalogue and
extracts explicit selections with public controls. Slice 119 remains frozen.

**High confidence from primary sources; Confirmed on five selected SpecDB
files:** PS2ZIP starts with bytes `C5 EE F7 FF` (LE `0xFFF7EEC5`), a LE 32-bit
negative decoded-size complement, then raw DEFLATE at byte 8. File decoding
does not use the catalogue XOR step. Sources:
[PDTools PS2ZIP.cs:131-159](https://github.com/Nenkai/PDTools/blob/ffe0bb26377ac9ff62f40951c1c0b16c5d9a380f/PDTools.Compression/PS2ZIP.cs#L131)
(MIT), [GT4FS PS2Zip.cs:19-55](https://github.com/Razer2015/GT4FS/blob/b938f71fd939e0ea44c38839a50ffb66cfd2067a/GT.Shared/Polyphony/PS2Zip.cs#L19)
and [BTree.cs:227-254](https://github.com/Razer2015/GT4FS/blob/b938f71fd939e0ea44c38839a50ffb66cfd2067a/GT4FS.Core/BTree.cs#L227).
GT4FS probes the marker independently of record kind; the new reader documents
a stricter supported subset and rejects unqualified combinations. GT4FS has
no inspected root license: only format facts were used; no implementation was
copied or linked. PDTools was not built or linked.

## Slice 121 — GT4-specific variation map and actual headers

The pinned Hub GT4 model guidance and PDTools CarVariation/Variation schema
identify the correct ModelCode route. The bounded offline join resolves the
Ford Ka to `fdeu0006`, independently reproduced on pinned selected records.
[Model Asset Probe](MODEL-ASSET-PROBE.md) records sources, exact fields and the
source-to-payload disagreements: signed -1 VM metadata and zero-size Tex1 slots.
Do not inherit unsigned-pointer or nonzero-size assumptions from generic header
descriptions. No reference implementation was imported or linked; geometry,
pixels and live use remain unqualified.
