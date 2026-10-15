# Native race renderer study (GT4Recomp)

2026-10-15. Read-only feasibility study. No checkout, worktree, build or game was touched. Labels: **Confirmed** (read in repo/decomp files or primary sources), **High confidence**, **Hypothesis**, **Unknown**. Decomp paths are relative to `private/slice151-research/gt4-decomp/` (read only). Repo paths are relative to `C:\Antigravity\gt4-staticrecomp`.

## Summary

- A hybrid "native races, GS for the rest" renderer is technically plausible but is still far from a first visible result. The repo's own gates put the first native car behind unresolved contracts: the live model instance, the callback/VM cadence, and the matrix/clip conventions are all **Unknown** (docs/RENDER-CONTRACTS.md, docs/CAR-ASSET-RESEARCH.md slice 121 end).
- The decomp gives good entry points (RacePS2Base render family, ModelSet2 render, CameraBase getters), but their names are community match names with medium confidence, and several "getters" run VU0 microprograms and can update state (RENDER-CONTRACTS.md, "Camera getters").
- **Recommendation:** get visible quality soonest from the **hardware GS path** (TASK-026 spec, TASK-027 implementation, plus a paraLLEl-GS spike, which is already on the roadmap), and take **ultrawide** as a separate, cheap guest-side projection/HUD project that does not need a native renderer. Start the native race renderer only as a **separate fork** with its own decision record, and only after the owner accepts that it departs from the current "original game, as faithful as possible" mission in CLAUDE.md. It should not block TASK-012/026/027.

## 1. How GT4 renders a race today

### 1.1 Control flow (Confirmed names, medium-confidence roles)

- Race session: `RacePS2Base` (vtable with 148 slots) sits under `RaceBasic` / `RaceBasicWithRaceDisplay` (`knowledge/classes.md` section 11-13, decomp).
- Render entry points from `config/symbol_addrs.txt` (gthd-tagged, not verified against code bytes here):

| Function | Address | Role (inferred from name) |
|---|---|---|
| `RacePS2Base::render` | 0x0033D970 | per-frame race render |
| `RacePS2Base::render_world` | 0x0033DEA8 | world pass |
| `RacePS2Base::render_course` | 0x003403B8 | track; calls ModelSet2 render with null instance (RENDER-CONTRACTS.md, 0x0034044C) |
| `RacePS2Base::render_cars` | 0x00340950 | cars |
| `RacePS2Base::render_reflection` / `_reflection_cars` / `_reflection_car_lights` | 0x003411D0 / 0x00341710 / 0x00341A30 | reflection passes |
| `RacePS2Base::render_back_mirror` | 0x00341E50 | rear mirror |
| `RacePS2Base::render_after` | 0x003422B0 | post-world pass |
| `RacePS2Base::render_post_effect` | 0x0033F890 | post effects |
| `RacePS2Base::isUseProjectionShadow` | 0x003411A8 | projected shadows |
| `CourseData::render_course` / `render_model` | 0x00396E10 / 0x00397188 | track model draw |
| `RaceDisplay::setProjectionMatrix` | 0x0039BB38 | HUD projection |
| `ModelSet2::begin` / `end` / `render` / `___render` | 0x00455370 / 0x004554D0 / 0x00455528 / 0x00455708 | model-set wrapper and command dispatch (RENDER-CONTRACTS.md table, High confidence structural role) |
| `CameraBase::getProjectionMatrix` / modelview | 0x0044F210 / 0x0044F280 | camera matrices (High confidence external symbol) |

- **Confirmed:** `ModelSet2` render reads the record table at model+0x38 (stride 0x28), a command stream at record+0x18 and per-instance fields; it calls begin once, internal-render per model, and end (RENDER-CONTRACTS.md "Confirmed from generated code").
- **Confirmed:** the camera getters are not pure. They call adjusted-this virtuals and VU0 helpers at decimal 2304/2352 and touch scratchpad 0x70002000 (RENDER-CONTRACTS.md, "Camera observation contract proposal").
- **Confirmed:** `ModelSet2` callbacks and the model VM can change animation/state during render (side-effect ledger, RENDER-CONTRACTS.md).

### 1.2 Draw lists, matrices, lighting

- The decomp does not name the GS/VU1 packet builder. `knowledge/architecture.md` section 8 says "drawing is spread over `mRenderContextPS2`, model/image objects, `PGLXshapeBuilder` (0x494500) and unnamed code (unit_0046A050 and GS/VU-side helpers). The low-level graphics path is unknown." `knowledge/architecture.md` section 13 lists "GS/VU1 packet building and the display-list code are not named."
- `knowledge/hwmap.md`: 622 of 31,164 functions touch hardware directly. The biggest cluster is `unit_00494578` (196 scratchpad, 99 register, 77 VU0, 64 sync functions), described there as "the rendering back end". Note the decomp places this unit in its `network` folder, which suggests the label is a heuristic. **Unknown** whether it is really render code.
- Matrix and light state flow through VU0 (`docs/VU0-MICROCODE-MAP.md`, `recomp` VU0 macro helpers in `docs/NATIVE-PORT-PLAN.md` section 2). **Unknown:** where light parameters are set and the matrix convention (row/column, handedness, clip range).

### 1.3 VU1 microprograms

- **Confirmed** (`docs/VU1-MICROCODE-SOURCE.md`): the whole VU1 microcode is one 16 KiB image, XOR-stream-encrypted in the ELF at file offset 0x525DD0. It is already AOT-compiled (`tools/vu_recomp`, 102 entries, interpreter as counted fallback; `docs/ARCHITECTURE.md` update 2026-10-15). All 86 recorded snapshots were identical.
- **Hypothesis:** the program roles (transform, lighting, clipping, environment/effects) follow the usual PS2 GT pattern, but no role mapping exists in the repo. Mapping them is a prerequisite for any mid-level option.

### 1.4 Models and textures

- **Confirmed** (`docs/MODEL-RECORDS.md`, `docs/VIF-FRAMING.md`, `docs/CAR-ASSET-RESEARCH.md` slices 120-125): Ford Ka '01 (ID 59) is resolved to model code `fdeu0006` through CAR_VARIATION -> VARIATION -> ModelCode. The garage/racing MDLS pair is extracted into a private capsule. Counts: garage 22 shapes / 154 descriptors / 14 materials / 49 textures; racing 113 shapes / 312 descriptors / 18 materials / 123 textures. Packet QWC and start alignment are checked. Vertex decoding, pixel decoding and the live link are **not** done.
- Format facts (Nenkai templates, High confidence for layout; `docs/RENDER-CONTRACTS.md`): MDLS header 0x80, model count at 0x16, model table at 0x38, 0x28-byte records with commands at 0x18; shape templates are VIF-packed with DMA quadword lengths and packed material/texture indices. Relocation is not handled.
- Tools: PDTools (MIT; `PGLUshape.cs`, `VIFPacket.cs`, `TextureSet1.cs`, `GSMemory.cs`) is the best source. The repo's Nenkai notes say its readers still need bounds and payload qualification (`docs/NENKAI-REFERENCES.md`). GTPS2ModelTool says GT4 support is unfinished. No tool is a qualified GT4 extractor.
- Textures are GS-format (swizzled, CLUT). A native renderer must decode them itself, so the GS texture-format census is reusable here.

## 2. Interception options for a hybrid

| Option | Where | What is captured | Effort | Fidelity | Mobile cost | Main risk |
|---|---|---|---|---|---|---|
| (a) High level: scene capture at `RacePS2Base::render_*` / `ModelSet2::render` | guest, via generated entries | camera, object list, transforms, LOD, material indices, instance pointers | medium-high | good for placement, weak for effects | low per frame if snapshots are small | getters/callbacks have side effects; each must run exactly once (SCENE-OBSERVATION.md gives a probe contract) |
| (b) Mid level: replace VU1 program output with native vertex processing | VU1 entry registry (`vu_recomp`) | transformed vertices, lighting, clip flags | high | high if faithful | medium | must reproduce VU1 numerics exactly for visual parity; the VU1 image is closed and enumerable, which helps |
| (c) Low level: reconstruct geometry from the GS/GIF stream | donor GS front end | primitives after VU | very high | high | high (still a GS model) | this is a HW GS renderer, not a native one; no gain beyond TASK-027 |

**Assessment (High confidence):** (a) is the only option that avoids executing the original draw path for covered objects, and it matches the repo's P3/P4 "replace the draw, keep the state" rule (NATIVE-PORT-PLAN.md section 4). Its weak point is **effects**: lens flare, sparks, sky and reflections are often GS tricks (readbacks, channel shuffles, feedback). Those need the GS path or separate native reimplementation. The repo already found readbacks in the night race and lens-flare readback stalls (TASK-012), so they are real.

## 3. What becomes possible natively

- **Resolution independence:** yes for opaque geometry, the largest cost. The GS path must still scale its own output (TASK-027, internal 1x-6x).
- **Ultrawide (21:9, 19.5:9, 20:9):**
  - The game computes its own projection (`CameraBase::getProjectionMatrix`, `RaceDisplay::setProjectionMatrix`). Widening FOV is a guest-side change; it does not need a native renderer. **Hypothesis:** this is also how PCSX2 widescreen patches work. The search this session found no GT4-specific widescreen pnach or ultrawide patch; the results were other games. The owner should check the PCSX2 patch repository directly (`github.com/PCSX2/pcsx2_patches`).
  - What breaks: missing geometry at the new edges (original culling and LOD tuned for 4:3 or 16:9), projected shadows and reflections sized to the old viewport, mirror and HUD anchoring, and any 2D overlay positioned by pixel. NATIVE-PORT-PLAN.md P5 already lists these risks.
  - HUD: `RaceDisplay` and its 32 display elements (`knowledge/classes.md`) need an explicit anchor policy. Stretching the HUD is the known failure mode.
- **Modern lighting, shadows, LOD:** possible only in a native path, and each one changes the look. The game's own light parameters and projection shadows are the starting points. This is a creative change, not fidelity, and it conflicts with the repo's "as faithful as possible" mission unless the owner opts in.
- **Frame rate:** physics stays on the original cadence. **Unknown:** the race update cadence. NATIVE-PORT-PLAN.md P6 requires separating simulation from presentation first, and the repo has no measurement of it. The current VBlank-driven callback is the source of pacing (`docs/ROADMAP.md`, `runtime/src/system.cpp:85` cited there).

## 4. Effort and staging

Estimates are **Hypothesis** for one experienced engineer with the existing tooling. They are not repo measurements.

| Phase | Work | Visible output | Effort |
|---|---|---|---|
| A | Scene capture: at `render_course`/`render_cars`/`ModelSet2::render`, snapshot camera, object list, transforms and material indices once per frame. Render them in a separate viewer beside the GS image. No draw replacement. | a native side-by-side view of one car | 2-4 months |
| B | Cars only: decode one MDLS car's geometry, textures and materials; draw it natively in place of the original car draw (flag, counters proving no VU1/GS draw for that object) | owner-visible one-car replacement | 3-6 months after A |
| C | Track: same pipeline for course models; visibility/LOD and reflection passes | full race scene natively, GS still for effects and HUD | 4-8 months |
| D | Sky, effects, particles, transparency, shadows, mirrors | full 3D native; effects remain the hard part | 6-12+ months |
| E | HUD and menus stay on the GS path permanently, unless a separate UI project is started | — | — |

Team: one lead plus two engineers for A-C is a rough **Hypothesis**; D needs graphics specialists. Realistic calendar to a credible full race: over a year.

## 5. Risks

1. **Decomp completeness:** 5,676 functions have no translator entry; `render_*` functions may be among them via function pointers (`docs/DECOMP-INTEGRATION.md`, `docs/EE-CALLBACK-COVERAGE.md`). A hook on a missing entry halts the game (`runtime/src/dispatch.cpp`).
2. **Effects as GS tricks:** lens flare, channel shuffles, feedback and readbacks cannot be reproduced by object-level capture.
3. **Side effects:** callbacks, VM calls and state-restoring getters must not run twice (RENDER-CONTRACTS.md). This is the main correctness risk.
4. **Replays, photo mode, multiple cameras, split-screen:** `RacePhotoMode`, `RecordedSceneCamera`, `RaceSplitBattle` each have separate render paths (decomp `classes.md`). Each needs its own contract.
5. **Asset completeness:** only one car is decoded. Relocation, texture-slot indirection and zero-size slots are unresolved (MODEL-ASSET-PROBE, slice 121).
6. **Licensing and provenance:** PDTools and GT4FS have unclear root licenses in places (`docs/NENKAI-REFERENCES.md`). Format facts are usable; code is not without review.
7. **Mobile:** a native path does not remove GS work. The HUD and any effects still run on the GS path, and the ARMSX2 GS pipeline is that path on mobile. Native geometry mainly saves GS cost on opaque geometry. **Unknown** how much.

## 6. Interaction with other work

- **TASK-012 paraLLEl-GS and TASK-026/027 own HW renderer:** these cover the whole frame and produce visible quality at 1x-6x sooner. They are the prerequisite for any hybrid, because the GS path still draws HUD, menus and effects. The native race renderer needs the GS path to keep working.
- **TASK-011 MTVU:** unaffected at the contract level; it changes when VU1 results are needed. Option (b) would change that sync point.
- **TASK-020 decomp names:** the names this study depends on come from gthd and will be formalized there. Do the naming work before Phase A.
- **Scene observation work (`docs/SCENE-OBSERVATION.md`, slices 127-129):** already the right first step for Phase A, but it is scoped to the shape-call boundary and not yet live.

## 7. Recommendation

1. **Now:** keep TASK-012 (paraLLEl-GS spike) and TASK-026/027 (own HW renderer) as the visible quality path. They fix resolution and the broad picture for the whole game.
2. **Now, parallel and cheap:** a guest-side ultrawide/projection study (`CameraBase`, `RaceDisplay::setProjectionMatrix`, HUD anchors). It does not need a native renderer and gives visible value sooner. Check the PCSX2 patch repository for GT4 entries first.
3. **Only after the owner decides:** the native race renderer, starting with Phase A as a read-only capture-and-view tool. It should live in a **separate fork** (for example `gt4-native-race`) that consumes captured scene data from this repo. Reasons: CLAUDE.md scopes it out for this repository, the owner's own mission text says "original game ... as faithful as possible", and the native path adds a second graphics stack to the Phase 1 release. Changing that scope is an owner decision, and it should be recorded as a decision file before any code moves.
4. Do not start Phase B or later until Phase A has a passing contract on one car.

## 8. Limits of this study

- No code was run, no hook was installed, no ELF bytes were re-verified for the gthd addresses above.
- Web search this session found no GT4-specific PCSX2 widescreen patch or ultrawide fix. The results were for other titles. Treat the ultrawide mechanics above as **Hypothesis** until checked against the PCSX2 patch repository.
- Effort figures are Hypotheses, not measurements.
