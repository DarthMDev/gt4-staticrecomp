# GT4 native renderer feasibility study (hook the game's draws, celosia style)

Date: 2026-10-17. Read-only study for GT4Recomp maintainers. Nothing was edited in the repository, nothing was
built, and the game was not launched. Two throwaway analysis scripts (ELF vtable reader, VU1 opcode census) ran
from the session scratchpad against the pinned private ELF and the slice155 VU1 image; their outputs are quoted below.

Labels: **Confirmed** (read in the decomp source, the ELF bytes, our repo docs, or Nenkai's MIT sources),
**High confidence**, **Hypothesis**, **Unknown**.

Paths: repo = `C:\Antigravity\gt4-staticrecomp`; decomp = `private/slice151-research/gt4-decomp` at `80b81d10`
(file paths below are relative to its `src/` unless noted); Nenkai = `private/slice151-research/nenkai`
(PDTools `ffe0bb26`, MIT); ELF = `private/reconstructed/SCUS_973.28.elf`.

Decomp names come from gthd matching (medium/high confidence, `config/gthd_names.txt`). Several are wrong; where the
code says otherwise this report uses a role name and keeps the decomp name in brackets (for example
`RaceEntryCar__update` at 0x004A5348 is really `pglMatrixMode`).

## 0. Taint status and sources

- **Untainted.** I did not open PCSX2 sources, the `gt4recomp-gs` fork, `private/tooling/pcsx2-*` or
  `private/slice189-pcsx2-ref`, and I did not open celosia's source code. From celosia I read only our prose study
  (`private/research/celosia-study.md` sections 0 header, 2.3, 2.4, 5).
- Disclosure: `docs/GS-LIVE.md` and `docs/GS-STREAM-PROTOCOL.md` contain prose written by PCSX2-tainted agents. I read
  only GS-LIVE sections 6.1-6.3 and 9.3 (game-side readback counts and timings) and the protocol's scope section. I
  skipped GS-LIVE 8.1 and 10.1, which describe PCSX2 source readings. No PCSX2 code or design detail entered this report.
- Other sources: CLAUDE.md, docs `ARCHITECTURE`, `VU1-MICROCODE-SOURCE`, `NATIVE-PORT-PLAN`, `NATIVE-RENDERER-STUDY`,
  `ULTRAWIDE-STUDY`, `HIGH-FPS-STUDY`, `DECOMP-INTEGRATION`, `CAR-ASSET-RESEARCH`, `NENKAI-REFERENCES`,
  `RENDER-CONTRACTS`, `SCENE-OBSERVATION`, `LICENSE-DEMO-CIPHER`; decomp `knowledge/architecture.md`, `classes.md`,
  `hwmap.md`, `config/symbol_addrs.txt` and about 60 decomp source files; Nenkai PDTools
  `Models/PS2/RenderCommands/ModelSetupOpcode.cs`, `PGLUmaterial.cs`, `PGLUshape.cs` (MIT).

## 1. Summary

1. **GT4 draws through its own GL-like library, "PGL".** Many PGL symbols are named in our sources: `pglPushAttrib`,
   `pglFrontFace`, `pgluTexImage` in the decomp, and about 90 model display-list opcodes in Nenkai's MIT
   `ModelSetupOpcode.cs`. PGL keeps a shadow of the GS registers in scratchpad at `0x70002000` with a dirty mask at
   `+0x1D8`, and a matrix stack in VU0 registers. Draws go through `pgluCallShape` (0x0049D308), which writes DMA REF
   tags that point at the model's own VIF packets and ends each shape with an `MSCAL` of a VU1 program
   (**Confirmed**, section 2.3). This is the natural "draw call" to hook, at the same level as celosia's `modelDisp`.
   Its arguments and the PGL state together describe one draw completely: matrix, material, texture, blend, fog,
   depth state and VU1 program.
2. **The frame has a clean, documented structure.** `RacePS2Base::render` runs four vtable passes:
   `render_world`, `render_post_effect`, `render_panel` (HUD) and `render_other`. This gives an exact 3D/2D split
   point (**Confirmed**, ELF vtable 0x679378 read in this study). Inside `render_world`, the main 3D pass
   (`func_0033EAB8`) calls, in order: sky, course, wet-road reflection, concourse, cars (with a per-frame
   environment map), back mirror, flares, particles and smoke (section 2.1).
3. **GT4 relies on several GS tricks.** A native renderer must rebuild each one:
   - render-to-texture everywhere (`GSBuffer` objects: car environment map, reflection buffer, mirror, flare buffer);
   - framebuffer feedback (motion blur and crossfade that read the previous display buffer);
   - a full-screen brightness multiply done by blending (`Cd*FIX`);
   - destination alpha masks (DATE/DATM opcodes in model commands);
   - a 64x8 flare buffer. **High confidence:** it is the source of the one 2 KiB readback per frame measured in
     night races.
   - a per-frame toggled dither matrix, and field-aware screen filters.

   This is why the effects, not the meshes, are the long pole.
4. **The game already interpolates between ticks.** An unnamed `SceneCameraBase` subclass (vtable 0x006792D8) keeps
   two camera keys and lerps eye, center, up and fov by the render parameter `t` (`func_0033B2A8`/`func_0033B3B0`).
   Every render pass takes `t`; races pass `t = 0` (**Confirmed**). Track culling uses a position-only Voronoi
   visibility list (`VisionList::getVoronoi`, 0x004580F0), so a wider FOV should not lose track pieces
   (**High confidence**). Both facts favour high frame rates and ultrawide.
5. **Effort (Hypothesis):** about **75-130 engineer-weeks** for a native race renderer with all race effects,
   replays, photo mode and showroom on PC, plus 10-20 weeks for the mobile backends. With one lead and agent help
   that is roughly **18-30 months of calendar time**. First owner-visible milestone ("cars in shadow mode, side by
   side with the GS, at 4x resolution"): about 4-6 months after the start.
6. **Hardest pieces:**
   - the world-pass effects (wet-road reflection with blur, flares with the readback, post effects that use
     framebuffer feedback);
   - the hybrid composition while some passes still run on the GS, because GS passes read the framebuffer and Z
     that the native renderer would own;
   - matching VU1 lighting and fog bit for bit enough to pass image diffs;
   - draw identity across ticks, needed for interpolation.

## 2. GT4's render architecture

### 2.1 Frame loop and render passes

**Confirmed (decomp + ELF vtable):**

```
GameObjectPS2::render (0x00101148) -> race object vtable ->
RacePS2Base::render (0x0033D970, decomp RaceBGMPS2_RacePS2Base/RacePS2Base__render.cpp)
  vt+0x348 render_world(buf, t=0.0f)      0x0033DEA8   3D world
  vt+0x350 render_post_effect(buf)        0x0033F890   motion-blur/crossfade feedback
  [sets f_cf5a history bit]
  vt+0x358 render_panel(buf)              0x0033FA40   HUD (RaceBasic 0x0038B5A0 adds RaceDisplay etc.)
  vt+0x360 render_other(buf)              0x0033FA98   other 2D (monitors, messages)
```

The vtable map below was read from the ELF in this study (`RacePS2Base` vtable 0x00679378, entry = {s16 delta,
s16 index, u32 fn}):

| vt offset | function | address |
|---|---|---|
| 0x348 | render_world | 0x0033DEA8 |
| 0x350 | render_post_effect | 0x0033F890 |
| 0x358 | render_panel | 0x0033FA40 |
| 0x360 | render_other | 0x0033FA98 |
| 0x368 | render_world_split | 0x0033FD88 |
| 0x370 | render_course | 0x003403B8 |
| 0x378 | render_reflection | 0x003411D0 |
| 0x380 | render_cars | 0x00340950 |
| 0x388 / 0x390 | render_reflection_cars / _car_lights | 0x00341710 / 0x00341A30 |
| 0x398 | render_exchange_tire (pit) | 0x00340870 |
| 0x3A0 | render_concourse | 0x0033F880 |
| 0x3A8 | render_after | 0x003422B0 |
| 0x3B0 / 0x3B8 / 0x3C0 | render_use_back_mirror / get_back_mirror_rate / render_back_mirror | 0x00341CC8 / 0x00341D78 / 0x00341E50 |
| 0x3D0 | pre-world hook (virtual_121) | 0x0033E860 |
| 0x3D8 / 0x3E0 / 0x3E8 | per-mode hooks around cars (empty in RacePS2Base) | 0x005F3B30 / 38 / 40 |
| 0x3F0 / 0x3F8 | flare-related virtuals (virtual_125/126) | 0x00342768 / 0x003428A8 |
| 0x428 | isCrossFadeEnabled | 0x003429D8 |
| 0x460 | isUseDefocusEffect | 0x003431B8 |
| 0x488 | isUseProjectionShadow | 0x003411A8 |
| 0x490 / 0x4A0 | isRenderCar / is_able_to_render_this_car | 0x00340690 / 0x003426A0 |
| 0x498 | render_dash_panel (stub) | 0x00342EE0 |

**`render_world` (0x0033DEA8)** (Confirmed sequence; roles High confidence unless marked):
1. `func_0033E8E0`: frame counter at `+0xE32C`, pre-world hook, color/depth mask reset.
2. Builds a `GSBuffer` view of the current display buffer (`D_00618588`), and an **N x N environment-map buffer**
   `aux` (`N = self+0xE330`). Its format is 0 or 2 depending on whether the frame is at least 449 lines tall and 577
   wide.
3. `func_0033E960` -> `func_0044E660`: renders the **car environment map** from the target car's position (section 2.4).
4. `func_0033EAB8`: **the main 3D pass**, described below.
5. Either draws directly into the display buffer (when the formats match), or renders offscreen and copies with
   `func_00105AD8` (a textured-sprite blit).
6. `func_0033F1C8` (course flag bit 0): `func_003BADF8`, a reduced-size glare or bloom pass whose strength scales with
   the screen diagonal over 781.2 (= the diagonal of 640x448). Role: Hypothesis.
7. `func_0033F2E8`: back-mirror composite, smoke, flare draw, view overlay, full-screen color filter, optional
   gradient filter (section 2.6).
8. If `isUseDefocusEffect`: `func_0033E6E0` (depth of field with focus distance `cam+0x68 * 1000`). Role: Hypothesis.
9. `func_00330AE8(self+0xE3AC, out, D_006184F0)` under replay or camera conditions. Unknown role (it takes the 16:9 flag).

**`func_0033EAB8`, the main 3D pass** (Confirmed order):
- bind the target, clear (`pglClearColor`, `pglClearDepth`, `pglClear(3)`);
- projection: `pglMatrixMode(1)`, `pglLoadIdentity`, then `camera->applyProjection(t)` (camera vt+0x10). The decomp
  shows one write to `cam+0xA08` before this call; its value comes from `scene+0x20`. Its role is Unknown;
- modelview: `pglMatrixMode(0)`, `pglLoadIdentity`, then `camera->applyModelview(t)` (vt+0x18);
- course environment: `func_00397CE0` sets fog and lights from `scene+0x60`;
- **sky**: `func_00396B80(course, course+0x54, -1, 1.0, 1000.0)`. It draws a ModelSet with its own depth range
  (near 1, far 1000) and no fog;
- `pglClear(2)` (depth only); `func_00342470` (a full-screen textured pass before the course, role Unknown);
- **course**: vt+0x370 `render_course`;
- **wet-road reflection**: vt+0x378 `render_reflection`;
- brightness multiply: `func_00342580`;
- flare setup: global `D_00621D24`, `func_003E50E0` (scene flag 0x800);
- **concourse** (pit or showroom scene): vt+0x3A0;
- **cars**: `func_0033DC88` (pit crew texture), vt+0x3D8, vt+0x380 `render_cars(aux, car, ..., t)`, vt+0x3E0;
- camera overlay (camera vt+0x1C8); dash panel vt+0x498; `render_after` vt+0x3A8 (course after-models);
- **back mirror**: rect 260x58 out of 640x448 at the top center, scale 1.3 (`+0xE2E0`), vt+0x3C0 or `func_00341D88`;
- **flares** (`FlareClass` at `self+0xDA0`, `func_003B7B68`/`func_003B80E8`/`func_003B8848`), flare virtuals
  vt+0x3F0/0x3F8;
- **particles** `func_003C2838(self+0x3238)` (ParticleManager);
- vt+0x3E8.

### 2.2 Who builds the display lists and DMA chains

- **PGL state machine (Confirmed, decomp `network/unit_00494578/`, range about 0x494500-0x4AC000).** The decomp
  places this unit in "network", but it is the render back end (`knowledge/hwmap.md` already calls `unit_00494578`
  "the rendering back end"). Identified entry points (role from the code; names are mine where the decomp has none):

| Address | Role | Evidence |
|---|---|---|
| 0x004A1638 / 0x004AB040 | pglEnable / pglDisable(cap). Caps: 3 = DATE, 4 = alpha test, 5 = depth test, 6, 15 = cull, 17, 21 | switch on cap, writes TEST/ZBUF shadows; Nenkai opcodes 23-30, 45-46 |
| 0x004A2808 | pglBlendFunc(alphaReg, fix): ALPHA byte at +0x198, FIX = x*128 | values 0x44, 0x48, 0x62, 0x64, 0x68, 0xA8, 0xA9 are GS ALPHA A/B/C/D codes |
| 0x004A2980 | pglColorMask: FBMSK = ~arg | +0x174 |
| 0x004A29A8 | pglDepthMask (ZMSK) | +0x188 bit 32 |
| 0x004A4550 | pglTexParameter (CLAMP wrap modes, TEX1 filter/LOD) | +0x1A8, +0x1B8, +0x1C0 |
| 0x004A4810 | pglScissor | SCISSOR bit fields at +0x1A8 |
| 0x004A13C8 | pglDitherMatrix (DIMX, 4x4 x 3 bits) | `func_0033DC48` toggles between two matrices every frame |
| 0x004A3078 / 0x004A3088 / 0x004A3230 | pglClearColor / pglClearDepth (1-x) / pglClear(1 color, 2 depth, 3 both) | call patterns |
| 0x004A40B0 | pglFog(distance, amount): FOG value 255 - 255*amount | decomp `func_00397DA0` calls it with `fov` and `aspect`; those field names are wrong |
| 0x004A5348 [`RaceEntryCar__update`] | pglMatrixMode(0 modelview, 1 projection) | call patterns |
| 0x004A53F8 / 0x004A5400 | pglPushMatrix / pglPopMatrix (`vcallms 0x900` / `0x930`, VU0 micro) | decomp comments |
| 0x004A7454 / 0x004A74B4 / 0x004A7550 | pglLoadIdentity / pglLoadMatrix / pglGetMatrix (copies VU0 vf28-vf31) | |
| 0x004A7890 / 0x004A7988 / 0x004A79B0 / 0x004A79D8 | pglScale / pglRotateX / pglRotate? / pglRotateY | |
| 0x004A59B8 / 0x004A5A90 / 0x0049AC00 | pglFrustum / pglOrtho / pgluPerspective(fovy, aspect, near, far) | |
| 0x004A5418 / 0x004A5688 | pglPushAttrib(mask) / pglPopAttrib | |
| 0x004A6788 / 0x004A6290 / 0x004A6AF8 | textured sprite blit / filled rect / line | GSBuffer copies |
| 0x004A3998 | full-screen framebuffer filter: modes 0-9 = add, subtract, crossfade, multiply, field-offset blends | section 2.6 |
| 0x004A9378 | VRAM-to-VRAM block copy (three 640x224 blocks) | used by crossfade |
| 0x0049D308 -> 0x0049D328 | **pgluCallShape(shape)**: emits DMA REF tags and VIF codes, ends with `MSCAL n` | section 2.3 |
| 0x0049CEA0 | material/texture switch callback (9-bit material and 7-bit texture index; 40-byte and 80-byte records) | registered by ModelSet2::begin (RENDER-CONTRACTS) |
| 0x0049AC98 / 0x0049AC70 | pgluTexImage / pgluTexReset (TEX0/MIPTBP shadows) | decomp |

- **GSBuffer** (render-target object, `startup/unit_00101400/func_00105xxx`): create (0x001052E0, with format;
  `0x31` = PSMZ24, a depth buffer), bind as color target (0x001056A0), bind as depth (0x00105708), bind as texture
  (0x00105728), copy/blit (0x00105AD8), `getTexture` (0x00105930), `getBufferWidth` (0x00105830). Two display buffers
  live in `D_0061850C` (`e[2]`, `cur`). **Confirmed.**
- **Model display lists.** `ModelSet2` command streams are interpreted on the EE by `ModelSet2___render` (0x00455708)
  through a byte opcode table at 0x006AABD0 (RENDER-CONTRACTS). The opcodes are a GL display list, per Nenkai:
  matrix ops, blend/alpha/depth/DATE state, fog color, `LODSelect`, `BBoxRender`, `CallModelCallback` (brake lights),
  `CallVM` with VM-driven transforms, `pgluShapeTweenRatio` (morph), `pglVariableColorScale/Offset`,
  `pglTexGenf` (environment-map texture coordinate generation), `pgluCallShape`. **Confirmed** (Nenkai MIT enum),
  matching the executable's table structure (RENDER-CONTRACTS, High confidence).
- **DMA.** `pgluCallShape` writes REF tags pointing into the model's resident VIF packets (zero copy). The per-object
  `*::DMAsafe` functions (ModelSet2 0x00454D30, CourseData 0x003976D0, BillboardSet 0x004504F8, RacePS2Base
  0x0033CD78) let the game free or modify a resource only after the DMA that reads it has finished. **Confirmed**
  for the tags; the DMAsafe role is High confidence.

### 2.3 The draw call: `pgluCallShape` (0x0049D308 / 0x0049D328)

**Confirmed** (`network/unit_00494578/func_0049D328.cpp`):
- input: a shape command `Cmd` (count, a per-shape float `value`, flags, packet items `{addr, count, event}`) and the
  PGL context `Obj` (scratchpad 0x70002000);
- if the float `value` changed, a constant is written to VU1 memory (`func_0049E548(obj, 0xC1, value)`). Hypothesis:
  the dequantization scale of the compressed vertices;
- per item: a DMA REF tag (`count | 0x30000000`, `addr`), `STMASK`, then an `ITOP` and `MSCAL n`, where `n` (the VU1
  program address) comes from the PGL state (`obj->count`). The VU1 program is therefore picked by PGL state
  (fog/clip/lighting/texgen variants);
- side effects that a replacement must keep: the `item->event` callback (`obj->callback`), the counters
  `nc`, `nd`, `ne`, the conditional state flush (`func_0049E480`, `func_0049D080`) and the dirty-bit flushes
  (`func_0049E5F8(obj, 0xD9)`).
- Callers (ELF `jal` scan in this study): `ModelSet2___render` (0x455A5C), `BillboardSet::render` (0x4507A4), the
  environment-map code (0x44E918, 0x44EE1C), and one HUD tachometer. The repo's slice127-139 "shape-call
  observation" (docs/SCENE-OBSERVATION.md) already observes this boundary live (256 records, Ford Ka menu).

### 2.4 Scene graph and model classes

- **Race session:** `RacePS2Base` (vtable 0x679378, 148 slots) and its subclasses (Arcade, GT mode, License, Photo,
  Split, LAN/Net...). Each overrides some render virtuals (the `render_cars` word appears in 25 vtables).
- **Course** (`self->0x6C->0x80->4`, `CourseData`, unit_00394D48):
  - models at `+0x4`; sky ModelSets at `+0x54` (main) and `+0x60` (mirror); Voronoi lists at `+0xC`/`+0x1C`/`+0x20`;
  - `BillboardSet` at `+0x78` (trees, crowds);
  - scene parameters at `+0x80`: flags at `+0x10` (0x800 flare, 0x10000/0x20000 reflection cars/lights, 0x800000
    projection shadow, 0x8000, 0x200000), fog blocks at `+0x60` (main), `+0xA90` (mirror) and `+0xC40` (reflection),
    the reflection buffer size at `+0xC24/0xC28`, the blur pass count at `+0xC2C`, the brightness factor at `+0x3C`,
    glare parameters at `+0x1750..0x1764`, color filter and gradient parameters at `+0x950..0x994`.

  **Confirmed** offsets from the decomp; roles are High confidence or Hypothesis as marked in section 2.1.
- **Course draw:** `CourseData::render_course` (0x00396E10) asks the camera for x/y/z and calls `VisionList::getVoronoi`
  (0x004580F0). That function maps the position to a grid cell and returns the nearest sample point's list of
  visible model indices. `render_course_2` (0x00396F00) then calls `ModelSet2::begin`, `ModelSet2___render(i)` for
  each listed index, `ModelSet2::end`, and the billboards for the same indices. **Confirmed.**
  `CourseData::render_course_for_renderer` (0x00396AD0) runs the course VM (`evalVMMethods`, animated course
  objects). `render_after`/`render_model` draw after-models (0x00397160/0x00397188).
- **Cars:**
  - entries are in a list at `self->0x6C->0x60`, sorted by distance (`RaceEntryBase::sortEntryCar`);
  - the car render object is at `entry+0x2880`, with a vtable at `car+8`. Slots seen in `render_reflection_cars`:
    `+0x118(t)` pose update, `+0xA8(envTexture)` bind environment map, `+0xF0/+0xF8(rate, 1, 1|2)` (Hypothesis:
    wheels or tires), `+0x40(2)`, `+0xB8(rate, 1)` body draw;
  - model files: `GT4Model::CarModel::getModelFileName`/`Wheel`/`Tire`/`Wing` (0x004541E0..0x00454378);
  - callbacks: `CarModel::Callback` (brake lights), `WheelCallback`, `AfterFireCallback` (classes.md);
  - **GT4 has no cockpit view and no car damage** (High confidence, public knowledge of the game).
    `render_dash_panel` is a 3-line stub.
- **Car environment map** (`func_0044E660`, Confirmed structure, Hypothesis role):
  - from the target car's position, renders the surroundings 3 times with an asymmetric frustum, rotated by
    -120 degrees about Y each time (`pglRotateY(i * -120)`, `pglRotateX(79.97)`);
  - draws a fixed "sphere mapper" shape (`D_006A7D00`) with an orthographic projection into the N x N `aux` buffer;
  - the map is passed to `render_cars(aux, ...)`. **This is the car paint reflection, rebuilt every frame**
    (`UnloadSphereMapperModel`, `Concourse::registerSphereMap` 0x00408470 for the showroom's fixed map).
- **Camera:**
  - `CameraBase` (vtable 0x688450): `applyProjection` vt+0x10, `applyModelview` vt+0x18, `getProjectionMatrix`
    0x0044F210, `getModelviewMatrix` 0x0044F280;
  - the race camera family `CameraSys::*` (follow, onboard, heli...);
  - **the recorded/replay camera (vtable 0x006792D8, Confirmed in this study):** `applyProjection` = 0x0033B2A8,
    `applyModelview` = 0x0033B3B0. They lerp between `keys[1-cur]` and `keys[cur]` by `t` and call `pgluLookAt`
    (0x0049A928) with eye, center and up, and `pgluPerspective(fov, aspect, near, far)` from key fields 9-12. Keys
    are read from a stream (`func_0033ACF0`, 0x24 + 0x10 bytes plus optional extras with defaults 5.0, 1/120, 5500)
    and pushed by `func_0033B0E8`, which swaps `cur`. **The replay camera stores aspect in key field 10.**
  - Live race camera aspect: Unknown (ULTRAWIDE-STUDY's open item). It goes through the camera class's own
    `applyProjection`, then `pgluPerspective` or `pglFrustum`.
- **Viewport:** `GSBuffer` sizes plus `pglScissor`. The 2D/HUD projection is `RaceDisplay::setProjectionMatrix`
  (0x0039BB38, ortho in a 640x448/480 pixel space; ULTRAWIDE-STUDY). Mirror: a sub-rectangle of the display buffer
  (`func_001054D8`), `pglScale(ratio * scale, scale, 1)` on the projection and `pglScale(1, 1, -1)` on the
  modelview, with `pglFrontFace(1)`. **Confirmed.**
- **3D vs 2D split:** `render_world` and `render_post_effect` are 3D or full-screen effects; `render_panel` and
  `render_other` are 2D (`RaceDisplay::render` 0x0039F498 plus the 32 `RaceDisplayObjectBase::render_main`
  elements). **Confirmed.** Menus use the UI framework (`mRenderContextPS2`, faces); `mCarFace`, `mModelFace`,
  `mSceneViewFace` and `mCarModelPS2` draw 3D cars in menus through the same ModelSet2/PGL path.

### 2.5 Draw entry points worth hooking

Frequency is per presented 60 Hz frame (Hypothesis unless counted). "Inputs" are guest structures.

| # | Area | Function (address) | What it draws | Inputs (guest RAM) | Frequency |
|---|---|---|---|---|---|
| H1 | **all meshes** | `pgluCallShape` 0x0049D308 | one shape (VIF packets) with the current PGL state | `Cmd*` (shape), PGL context at 0x70002000 (GS register shadows, material/texture indices), VU0 matrix stack (vf28-vf31 plus the scratchpad copy) | hundreds to thousands per frame (MSCAL counts 724-815 per frame, MTVU-PLAN) |
| H2 | model walk | `ModelSet2::render` 0x00455528, `___render` 0x00455708, alt wrapper 0x00455598, begin/end 0x00455370/0x004554D0 | one model's command stream | MDLS at model ptr (+0x16 count, +0x38 records x 0x28, +0x18 commands), instance (+0x04, +0x14 callback) | per model |
| H3 | frame | `RacePS2Base::render` 0x0033D970 | the four passes | race object; display buffer `D_0061850C` | 1 |
| H4 | 3D pass | `render_world` 0x0033DEA8, `func_0033EAB8` | world | camera via vt+0xD0, course at `+0x6C->0x80->4` | 1 (+1 per split-screen view) |
| H5 | camera | camera vt+0x10/0x18 (`applyProjection`/`applyModelview`), getters 0x0044F210/0x0044F280; replay camera 0x0033B2A8/0x0033B3B0 | projection/modelview | camera object, `t`, keys | 2-6 per frame (main, mirror, reflection, env map) |
| H6 | **cars** | `render_cars` 0x00340950 (unmatched in the decomp); per car: car object vt `+0x118`, `+0xA8`, `+0xB8`, `+0xF0/+0xF8`; model calls through H2/H1 | body, wheels, tires, wing, lights, brake-light callback | entry list `+0x6C->0x60`, car at `entry+0x2880`, env map `aux` | 1 call; 1-6 cars |
| H7 | car env map | `func_0044E660` (called by `func_0033E960`) | 3 x 120-degree views into an N x N sphere map | car position, course Voronoi list, N at `+0xE330` | 1 |
| H8 | tracks | `render_course` 0x003403B8 -> `CourseData::render_course` 0x00396E10 / `_2` 0x00396F00, `render_after` 0x003422B0, course VM 0x00396AD0 | course models plus billboards | Voronoi list (u16 indices), ModelSet2, `BillboardSet` | 1-3 (main, mirror, reflection) |
| H9 | sky | `func_00396B80` 0x00396B80 | sky ModelSet with its own depth range, no fog | `course+0x54` / `+0x60` | 1-2 |
| H10 | wet-road reflection | `render_reflection` 0x003411D0, `render_reflection_cars` 0x00341710, `_car_lights` 0x00341A30 | mirrored course and cars into an offscreen buffer, N blur passes, composite | `scene+0xC10..0xC2C`, flags 0x10000/0x20000 | 0-1 |
| H11 | back mirror | `render_back_mirror` 0x00341E50, `func_00341D88`, composite in `func_0033F2E8` | mirrored view, 260x58 inset | `+0xE2E0` rect, `get_back_mirror_rate` | 0-1 |
| H12 | shadows | `isUseProjectionShadow` 0x003411A8 (scene flag 0x800000); car shadow shapes (Nenkai `PGLUshape.Unk1 == 5`); `func_0033DC88` (pit crew texture) | projected and mesh shadows | Unknown inside `render_cars` | per car |
| H13 | particles | `func_003C2838` (ParticleManager at `self+0x3238`), `SparkParticle::Render` 0x003C1900, `ThickLine2` 0x003C1788 | sparks (additive thick lines) | particle list (`next`, vt+0x18) | 1 + N particles |
| H14 | smoke/dust | `RaceSmoke::update` 0x003E7A58; draw `func_003E7CC8(self+0x31D0, tex, ...)` | tire smoke and dust (Hypothesis); downsamples a framebuffer copy into tiles | RaceSmoke object, a GSBuffer | 1 |
| H15 | tire marks | **Unknown** (no symbol found) | — | — | — |
| H16 | rain/wet | GT4 has no dynamic rain (High confidence); wet look = H10; rally/dirt = H14 | — | — | — |
| H17 | lens flare and sun, night lights | `FlareClass` at `self+0xDA0`: setup `func_003B80E8` (64x8 and 64x64 buffers, `func_00497AF0`), read `func_003B8848` -> `func_00497EF8` -> `func_004A5178(..., 0x40, ...)` (Hypothesis: the readback), draw `func_003B88C8` (up to 2 x 100 sprites, additive 0x48); `FlareClass::updateFlare` 0x003B8D40, `setReflectionFlareParameter` 0x003B8DA0, `CourseEnvPtr::setReflectionFlareParameter` 0x00398918 | sun/flare and light halos | flare entries `{xyz, light, ...}`, scene flag 0x800 | 1 |
| H18 | post effects | `render_post_effect` 0x0033F890 (previous-frame blend, `func_004A3998(6, min(f, 0.8))`); crossfade `func_003429E0`; brightness `func_00342580`; glare/bloom `func_0033F1C8` -> `func_003BADF8`; defocus `func_0033E6E0` (if vt+0x460); color filter and gradient in `func_0033F2E8` (`func_004A7D90`, `func_003BBBC0`) | full-screen effects | camera vt+0x170..0x188 values, scene parameters | 1 each |
| H19 | replays | replay camera (H5), `render(t)` paths; `RecordedSceneCamera`, `RecordedCarParamSponsor` (classes.md) | same passes | key streams | 1 |
| H20 | photo mode | `RacePhotoMode` (vtable 0x681C08), `PhotoPause` (aspect 4/3 at `PauseBase_PhotoPause/func_003C5430.cpp:61`), `mPhotoRenderFace`, `mRenderContextPS2::magnifyScreen` 0x002182E0 | stills, possibly tiled high resolution (Hypothesis) | photo camera manager | rare |
| H21 | showroom/pit | `Concourse::render` 0x00408490, `LicenseConcourse::render` 0x004096C0, `ConcourseLighting`, `render_concourse` vt+0x3A0; menu cars `mCarModelPS2`, `mSceneViewFace` | showroom scene and car | ScenePack | 1 |
| H22 | HUD (stays GS) | `render_panel` 0x0033FA40 / `RaceBasic::render_panel` 0x0038B5A0 / `RaceDisplay::render` 0x0039F498 / element `render_main` (0x003A3B58..0x003AEC70) / `RaceDisplayUtil::wideOffset` 0x003AFDD8 | 2D panels | `RaceDisplay` object (`originX/Y` at +0xB0/+0xB4) | 1 + 32 elements |
| H23 | menus (stay GS) | `mRenderContextPS2` virtuals 49-76, faces | UI | widget tree | 1 |

### 2.6 GS-side tricks the game relies on

| Trick | Where (evidence) | Native equivalent | Difficulty |
|---|---|---|---|
| Render to texture | GSBuffer create/bind (2.2); env map (H7), reflection (H10), mirror (H11), flare buffers (H17), glare (H18) | render targets per pass, at a scaled resolution | M |
| Framebuffer feedback | `render_post_effect` binds the *other* display buffer (`e[1-cur]`) as a texture and blends it with FIX alpha up to 0.8; `func_003429E0` copies three 640x224 blocks of the previous frame (crossfade); RaceSmoke grabs a copy (H14) | keep the previous native frame as a texture; when presenting above 60 Hz, scale the decay per presented frame (`k^(60/Hz)`) | M, plus design choice at high FPS |
| Blend-based full-screen math | brightness: color (0,0,0,y) with ALPHA 0xA9 = `Cd*FIX` (`func_00342580`); filters with 0x62/0x68/0xA8/0xA9 (`func_004A3998`) | post-process shader | S |
| Destination alpha | `func_00342370` writes only alpha (FBMSK 0xFF000000 masked off) with clear values 0 or 0x80000000 around the reflection; model opcodes 29-31 (`pglEnableDestinationAlphaTest`, `DestinationAlphaFunc`, DATE/DATM) | an alpha (or stencil) channel in the native color target with the same write and test rules | M |
| Readback to the EE | 2 KiB per frame in night races (GS-LIVE 6.1, Confirmed count). High confidence: it is FlareClass's 64x8 PSMCT32 buffer (64 x 8 x 4 = 2,048 bytes) | compute the same visibility from native depth (GPU query or compute) and write the bytes to guest RAM at the readback point | L (needs the exact byte semantics) |
| Interlace and fields | 640x224 field buffers (`func_003429E0`); filter modes 8/9 with ±16 sub-pixel offsets per field (`func_004A3998`); the frame format depends on 449/577 thresholds (render_world); races forced to 480p by `force_race_480p_at_vblank` (HIGH-FPS-STUDY) | progressive native output; skip field tricks | S |
| Dither | DIMX toggled every frame (`func_0033DC48`) | none at 32-bit or HDR; optional ordered dither for fidelity mode | S |
| Depth tricks | sky drawn with its own depth range, then `pglClear(depth)` before the course; Z test GEQUAL/GREATER (PS2 larger = nearer, Nenkai opcode 25); `pglDepthBias` (opcode 39); PSMZ24 depth targets; mirror Z scale -1 | reverse-Z native depth, a separate sky pass, depth bias per material | S-M |
| VRAM tricks of the font and HUD | 2D stays on the GS: no work | — | — |
| Texture CLUT/format reinterpretation | Unknown in race passes (no "channel shuffle" seen in the decomp read here) | to verify with a GS capture scan | Unknown |

## 3. Data formats and how they become GPU resources

| Asset | Format (source) | GPU form | Cache key and place |
|---|---|---|---|
| Model set | `MDLS` ModelSet2: 0x80 header, model count at 0x16, model table at 0x38, 0x28-byte records with commands at 0x18, relocation, per-color texture tables, material variations, bind matrices (Nenkai `GT4_MDLS_ModelSet.bt`; RENDER-CONTRACTS, High confidence). Car wrapper `CAR4` (`GT4_CAR4_CarInfo.bt`) | not converted as a whole; the command stream is *executed by the game*, and the native side captures the resulting H1 calls | the model's guest address plus a content hash of its header |
| Shape | `PGLUshape`: triangle strips in VIF packets; GT4 compressed positions at VU address 0x8000, UVs as `short[2]` at 0x8040, normals and vertex colors; flags (Unk1 = 5 marks shadow shapes); per-face material and texture indices (Nenkai `PGLUshape.cs`; repo slices 122-126: 113 shapes / 312 descriptors in the Ford Ka race model) | vertex buffer (position, normal, UV, color) plus a strip-to-list index buffer; the dequantization scale comes from `Cmd.value` (Hypothesis) | **content hash of the referenced VIF bytes** (address plus length from the REF tags); a GPU mesh cache with LRU eviction (celosia's Vita lesson) |
| Material | `PGLUmaterial`, 0x50 bytes: ambient, diffuse, specular, a fourth color (emission?), a float, flags, a float defaulting to 127 (shininess?), a float (Nenkai `PGLUmaterial.cs`, MIT) | uniform block | material record address plus hash |
| Texture set | `Tex1` TextureSet1: `PGLUtexture` records holding GS TEX0/TEX1/CLAMP/MIPTBP images, transfer records (GS uploads), CLUT patches, GS swizzled formats PSMT4/PSMT8/PSMCT16/32 (Nenkai `Tex1_TextureSet1.bt`, `TextureSet1.cs`, `GSMemory.cs`; repo slice 121: zero-size slots exist) | decode (deswizzle plus CLUT) into RGBA8 or BC7 textures; mip levels from MIPTBP | hash of the transfer payload plus CLUT. Car paint colors swap CLUTs or texture tables (`pgluSetTexTable`, external tex sets), so the key must include the CLUT |
| Course | `GT4CourseData.bt`, `GT4Runway.bt`, `GT4_BLBd_BillboardSet.bt` (Nenkai); Voronoi grid in `VisionList` (decomp struct: grid w/h, cells, origin, scale) | static meshes (ModelSet2 as above), billboard instance buffers | per course, loaded once per race |
| LOD | `LODSelect` opcode (distance-based jump in the command stream); `ModelSet2::texLOD`/`texLevel` globals (texture LOD on/off, used for the mirror); `ModelSet2::farClip` global (on for reflections) | the game picks the LOD; the native side captures the chosen shapes | — |
| Animation/morph | `pgluShapeTweenRatio`, model VM opcodes 67-77 (Nenkai) | vertex blend in a shader, or CPU morph | per shape pair |

**Recommended capture level for meshes:** let the game run its own command VM, LOD selection, callbacks and culling,
and capture at H1. This keeps all game-side decisions and side effects (single evaluation; RENDER-CONTRACTS ledger)
and avoids reimplementing the model VM. Offline extraction (PDTools-style readers) is used only for tests and
warm-up preloading.

## 4. VU1 microcode

- **Confirmed** (VU1-MICROCODE-SOURCE): one 16 KiB image (ELF offset 0x525DD0, XOR stream). The slice155 manifest
  holds 426 emitted entries, all AOT.
- Static census of this study (scratchpad script over `private/slice155-vu1-image/vu1_image.bin`, following
  branches until the E bit; counts per entry):

| Family (entry byte addresses) | Size (instructions) | Characteristic ops | Role (Hypothesis) |
|---|---|---|---|
| 7160-7216, 8960-9944 (about 30 entries) | 560-600 | CLIP 31-32, DIV 14-15, MULq 40-50, FCAND 6, XGKICK 5, MINI 8-9, FTOI4 8 | transform + perspective + clip-code culling, fog/color clamp; the main "clipped" mesh programs |
| 7240-7376, 10152-11784 (about 50 entries) | 570-605 | as above plus OPMULA/OPMSUB (cross product), FMAND | same with a face normal: backface test, flat normal, or the reflection vector for `pglTexGenf` environment mapping |
| 9464, 9800, 10136, 12000-12256 | 430-490 | CLIP 11-16, DIV 9 | lighter clipped variants |
| chained 12648 -> 15224 (8-3 CLIP steps) | 380 -> 93 | CLIP n, DIV n+1, one XGKICK | staged polygon clipper (one entry per remaining frustum plane); Hypothesis |
| 6920-7136, 8280-8944 | 60-100 | DIV 4-6, MULq 8-10, FTOI4, no CLIP | unclipped fast path (object fully inside the frustum) |
| 4808 | 60 | ERLENG, DIV | vector length: billboards or sprite sizing |
| small entries (SQI/XTOP/XITOP) | < 20 | buffer setup | double-buffer and constant upload helpers |

- **Lighting:** few EFU or normalize ops are visible. With GL-like materials (ambient, diffuse, specular) per vertex,
  the likely form is a light matrix times the normal, then clamping. That would show up as MADD/MINI runs, not EFU
  ops (Hypothesis). **Skinning:** none seen in VU1 (Hypothesis). Skinned crowds or pit crew use
  `Concourse::getSkinningBuffer` and `GT4_Motion::MotionSetPlayer::getSkinningBufferSize`, which suggests EE-side
  skinning into a buffer. **Fog:** per vertex, from `pglFog` (H-table above) and the MINI clamps; the GS FOG
  register. **Environment mapping:** `pglTexGenf` opcodes 86-87 plus the cross-product family.
- **Difficulty of reproducing them in shaders:**
  - transform, perspective and fog: S once the constants layout is known;
  - clipping: not needed (GPU clipping), but guard-band and near-plane behavior must match for image diffs. That is
    an M risk: celosia still lists near-plane clipping differences (celosia-study 2.4);
  - lighting: M. The exact formula must be derived per program from our AOT output (`vu1_generated.cpp`, readable
    C++) and checked with the existing VU1 differential tests;
  - env-map texgen: M.
- **Method:** for each program family, read the AOT C++ for the matching entry and write a reference function
  (C++, MIT) that maps VU1 input memory to GIF output. Prove it against the AOT code on recorded inputs (the repo
  already has `--vu1-verify`). Only then port it to a shader. The reference function doubles as the CPU oracle for
  shader tests.

## 5. High FPS and ultrawide requirements

### 5.1 State to capture per simulation tick (for interpolation)

Physics stays at 1/60 s (`RaceBase__updateRace` dt = 1/60, HIGH-FPS-STUDY, Confirmed). The native renderer builds a
**scene snapshot** per guest frame and interpolates between the last two:

| State | Where captured | Interpolation |
|---|---|---|
| camera eye/center/up/fov/aspect/near/far | after `applyProjection`/`applyModelview` (H5), or the key structs of the replay camera | lerp positions, slerp the orientation, lerp fov |
| per-draw model matrix (modelview at H1 = VU0 matrix stack top) | at H1 | decompose to TRS, then lerp/slerp; fall back to N+1 when there is no match |
| car body, wheel spin and steer, suspension | through H1 matrices (wheels are separate shapes); optionally car object fields | same; wheel spin needs the angle, not a matrix lerp (aliasing at high spin) |
| VM-driven objects and tweens (`pgluShapeTweenRatio`, `VariableColorScale`) | at H1 (PGL state) | lerp the scalar |
| particles (sparks, smoke) | particle list nodes (H13/H14) | by node identity; spawn and die at tick boundaries |
| flares | FlareClass entries (H17) | lerp the screen position; occlusion from the native depth |
| post effects (feedback blur) | per-frame parameters | adjust the decay per presented frame |
| HUD | GS layer of the latest tick | no interpolation (numbers must not blend) |

- **Draw identity** (the key risk): key = (shape `Cmd*` address, model address, instance pointer, call ordinal within
  the same (model, instance)). LOD switches change shapes; on a mismatch the draw is not interpolated (shown from
  the newer snapshot). Measure the matched-draw rate as in HIGH-FPS-STUDY's oracle (kill gate below 95% in races).
- **The game's own `t`:** every render pass takes `t`, and the replay camera already lerps two keys by `t`. A
  fallback that needs no native snapshot is to re-run `RacePS2Base::render` with `t` in (0,1). It costs a full EE
  render plus VU1 per extra frame (HIGH-FPS approach C) and is only useful as a cross-check.
- **Latency:** +1 tick (16.7 ms) for interpolation. Extrapolation is not recommended for cars (collisions).
- **CPU benefit:** native draws skip VU1 for replaced shapes, and VU1 is the largest guest CPU cost
  (HIGH-FPS-STUDY 4). That makes room for 120/144 Hz presents on the render thread.

### 5.2 Culling, LOD and the wider frustum

| Stage | Where | Aspect-dependent? | Fix for ultrawide |
|---|---|---|---|
| Track visibility | `VisionList::getVoronoi` (position-only nearest-sample list) | **No** (High confidence: the lookup ignores view direction, so the lists must cover all directions) | none |
| Model bounding boxes | `BBoxRender` opcode (EE, against the current PGL projection) | Yes | make the game's own projection wide (same aspect as native) so its tests match; or force the bbox test to pass (costs draws) |
| Far clip | `ModelSet2::farClip` global, scene far planes | No | none |
| Car visibility | `isRenderCar` 0x00340690, `is_able_to_render_this_car` 0x003426A0, entry sorting | Unknown | check whether a frustum test exists; patch at the hook |
| Per-primitive clip | VU1 CLIP/FCAND | Yes, but bypassed by native draws | none for native draws |
| LOD | `LODSelect` by distance, `texLOD` | No | none |
| Env map, reflection, mirror buffer sizes | `+0xE330`, `scene+0xC24/C28`, mirror rect `+0xE2E0` | their resolution, not their aspect | the native renderer sizes its own targets |

**Design rule:** the native renderer feeds a wider aspect back into the game's projection: replay key field 10,
the live camera's `applyProjection` (still Unknown), and `pgluPerspective` 0x0049AC00. That way the game's own EE
culling (bbox, car tests) sees the wide frustum, while the native renderer uses the captured camera with the same
wide aspect. The GS path does not draw the 3D in that mode, so it does not care.

### 5.3 HUD placement

- The HUD is drawn by `RaceDisplay` (H22) in a 640x448/480 ortho space. Edge-anchored elements use
  `RaceDisplayUtil::wideOffset` (±96 px when the 16:9 flag `D_006184F0` is set; flags 4/8 select anchoring and side;
  ULTRAWIDE-STUDY, Confirmed).
- **Proposal:** hook each element's `render_main` and wrap it in "layer begin/end" markers carrying the element's
  anchor (left/right/center from the `wideOffset` flags). In the GS stream these become control records. The
  compositor renders each element's GS output into its own transparent sub-layer, then places it at the anchor on
  the wide canvas, with no stretching. Elements without anchor flags stay centered in a 16:9 safe area.
  Alternative, cheaper and less exact: a single HUD layer in the game's 16:9 mode, centered.

## 6. Difficulty map and phases

Effort for one experienced engineer with agent help; **Hypothesis**. S = 1-2 weeks, M = 3-6, L = 6-12,
XL = more than 12.

| Piece | Effort | Risk | Depends on |
|---|---|---|---|
| Hook layer: function replace and observe-after-return by guest address, missing-entry audit for hooked functions | M (4-6 w) | medium: hooks on functions without translated entries halt the game (0x44EA58 class, RENDER-CONTRACTS) | producer/dispatch overlay (TASK-017 pattern) |
| Scene capture (H1 + PGL state + VU0 matrices + frame markers) and a capture file | M (4-6 w) | medium: single evaluation, ownership and lifetimes | hook layer; SCENE-OBSERVATION's shape-call work |
| Side-by-side tools (native vs GS at 1x, masked diffs, per-draw debugger) | M (3-5 w) | low | capture, headless runner |
| Renderer core (portable API, Vulkan backend, compositor with GS layers) | L (6-10 w) | medium | — |
| Asset decode (VIF shapes, GS textures and CLUTs, materials) and caches | L (6-10 w) | medium: formats partly qualified (slices 118-126) | Nenkai specs, the repo's census |
| VU1 program semantics (reference functions per family) | L (6-10 w) | **high** for lighting and fog exactness | AOT output, verify modes |
| Cars (body, wheels, wing, lights, brake callback, paint CLUTs) incl. env map | L (8-12 w) | medium-high | all of the above |
| Track (course, billboards, sky, fog, course VM objects) | L (6-10 w) | medium | cars pipeline |
| Wet reflection + blur, back mirror | M (4-6 w) | medium | track, cars, RT support |
| Shadows (projected and mesh) | M (3-6 w) | medium (unknown details in `render_cars`) | cars |
| Particles, sparks, smoke/dust | M (4-6 w) | medium (smoke uses a framebuffer grab) | track |
| Flares, sun, night lights + readback emulation | L (6-8 w) | **high** (EE-visible readback semantics) | depth ownership |
| Post effects (brightness, glare/bloom, DOF, motion-blur feedback, crossfade, color filters) | L (6-10 w) | medium-high | world pass native |
| Hybrid composition and 2D layers, HUD anchoring | M-L (5-8 w) | **high** (depth and framebuffer dependencies, section 7.4) | GS stream markers |
| High FPS interpolation | M-L (4-8 w) | medium-high (draw identity) | capture, world pass native |
| Ultrawide + any resolution | M (3-6 w) | medium (live camera aspect Unknown, bbox culling) | world pass native |
| Replays, photo mode, showroom, split screen, menu 3D | L-XL (8-16 w) | medium | everything above |
| Mobile backends (Metal, Vulkan-mobile, GLES) and memory limits | L-XL (10-20 w) | medium | core API discipline |

**Total:** about 75-130 weeks on PC, plus 10-20 weeks for mobile.

### Proposed incremental order (each phase keeps the game fully playable on the GS path)

**Phase 0: infrastructure (10-14 weeks).**
- Hook layer; scene capture at H1/H3/H5 with frame markers; a `.gt4scene` capture file; headless replay of
  captures; renderer core with the Vulkan backend; side-by-side viewer (GS frame and native frame next to each
  other, plus a difference image).
- Acceptance:
  - hooks in observe-only mode change nothing: the headless slice157 schedule gives 42/42 frame dumps identical to
    the pinned baseline (BUILD.md slice156/157 pattern);
  - capture overhead below 1 ms per frame;
  - CTest fixtures for the hook layer, the capture parser and the PGL state decoder.

**Phase 1: assets and the draw replay (8-12 weeks, overlaps Phase 0).**
- Decode the H1 draws of a captured frame (shapes, textures, materials) and redraw the whole captured frame
  natively at 1x, offline, with the game's own transformed matrices.
- Acceptance: on captured showroom and race frames, the native 1x image versus the GS 1x image (CoreGS reference)
  over the geometry mask gives PSNR ≥ 30 dB and pct_bad ≤ 8% (metrics of `docs/hw-renderer-spec/14`); every draw is
  decoded or explicitly counted as unsupported.

**Phase 2: cars, live, shadow mode (8-12 weeks).**
- Cars rendered natively *in parallel* (not shown) at any internal resolution, including the native environment map.
- Owner view: a debug window or picture-in-picture, side by side.
- Acceptance: car-mask PSNR ≥ 32 dB at 1x versus the GS on the reference scene (Supra, Nordschleife, 480p), with no
  missing parts; game speed stays at 100%.

**Phase 3: track, sky, fog, billboards in shadow mode (6-10 weeks).**
- Acceptance: full world geometry diff at 1x, PSNR ≥ 30 dB; draw coverage ≥ 99% of H1 calls in the reference race.

**Phase 4: world-pass effects, then the on-screen switch (16-26 weeks).**
- Covers reflection, mirror, shadows, particles, smoke, flares with the readback emulation, post effects.
- Then **turn on native ownership of `render_world`**: GS-side 3D submission is suppressed (VU1 not run for those
  draws), the GS draws only `render_panel`/`render_other`, and the compositor stacks them.
- Acceptance:
  - effect-by-effect diffs per scene (night race, wet track, rally) within the thresholds above;
  - the owner's in-game approval on the reference scene, night race and a rally stage;
  - 60.0 FPS at 100% game speed at 4x internal resolution;
  - the flare readback bytes written by the native path equal the GS path's bytes on captured frames (or are within
    a documented tolerance).

**Phase 5: high FPS (4-8 weeks).**
- Acceptance: 120/144 Hz presents at 100% game speed on the owner's PC; matched-draw rate ≥ 95% in races; frame-time
  p99 under the present interval; no physics change (headless guest frame dumps unchanged).

**Phase 6: ultrawide and any resolution (3-6 weeks).**
- Acceptance: 21:9 and 32:9 with no edge pop-in on the reference lap (owner check); HUD elements anchored; 4:3/16:9
  output unchanged.

**Phase 7: other modes (8-16 weeks).** Replays (including the replay camera `t`), photo mode, showroom/Concourse,
split screen, menu 3D cars. Each mode gets the same shadow-mode-first rule.

**Phase 8: mobile backends (10-20 weeks).**

## 7. Architecture proposal (MIT renderer)

### 7.1 Hook layer (runtime)

- A generic **guest function hook registry** in the runtime dispatch:
  - `observe_after(addr, fn)`: run after the translated function returns, reading results;
  - `replace(addr, fn)`: native C++ instead of the translated body, with an explicit list of the side effects it
    must keep;
  - `observe_before(addr, fn)`.

  Hooks are keyed by the guest entry address and by the return address (the TASK-017 license guard pattern,
  docs/LICENSE-DEMO-CIPHER.md). Each hook declares the guest registers it reads. A CTest checks that every hooked
  address has a translated entry (no halt path).
- Guest access goes through bounds-checked readers (`GPtr<T>` with `static_assert`ed offsets, as in
  DECOMP-INTEGRATION level 2). There is never a host pointer into guest RAM that outlives the hook.
- **Draw replacement at H1:** the native side records the draw. Then it either lets the original run (shadow mode)
  or skips the DMA emission while still calling the event callbacks and updating the counters and dirty flags that
  `func_0049D328` updates (section 2.3). Skipped draws mean no VU1 work and no GIF for them.

### 7.2 Extraction and caching

- **Per draw:** shape content hash (VIF bytes referenced by the REF tags), texture key (TEX0 plus CLUT hash plus
  upload payload hash), material record hash, PGL state block (blend, alpha/date test, depth, cull, fog, scissor,
  color mask), matrices (projection, modelview), VU1 program entry.
- **Caches:**
  - GPU mesh cache and texture cache, both keyed by content hash, with LRU and a byte budget (mobile);
  - an optional disk cache of decoded textures under the user cache folder (no game data in the repo);
  - a pipeline cache keyed by the PGL state class.
- **Texture uploads:** observe GS PATH2/PATH3 uploads (the GS stream already carries them) to keep a guest-VRAM
  shadow, so the native texture lookup by TEX0 works the same way as the GS: the "VRAM is the truth" model. This
  avoids relying on file parsing for live textures.

### 7.3 Renderer core API (portable)

- C++20, MIT, no dependency on Windows. Layers:
  1. `gt4r::scene`: snapshot types (camera, draws, lights, particles, flares, post parameters), pure data;
  2. `gt4r::passes`: GT4-specific pass graph (env map, sky, course, reflection, cars, mirror, flares, particles,
     post), written against a thin API;
  3. `gt4r::rhi`: a minimal render-hardware interface (buffers, textures, render targets, pipelines, draw, compute,
     queries) with backends Vulkan (PC and Android), Metal (iOS) and optionally GLES 3.2.
- Shaders written once (Slang or HLSL to SPIR-V and MSL, or WGSL through the repo's naga tooling, `tools/wgsl_spirv`);
  fixed-function GS behavior emulated by pipeline state plus small shader permutations (blend codes, alpha test,
  date, fog).
- Coordinate conventions: PS2 depth larger = nearer maps to reverse-Z; field and interlace logic is not ported.

### 7.4 Coexistence with the GS stream (hybrid)

Dependencies decide the order: several GS passes **read** the 3D framebuffer or depth (reflection composite, brightness
multiply, feedback blur, smoke grab, flare occlusion and readback, DOF). A native-owned scene therefore cannot be
mixed per object with GS passes that read it. Hence:

1. **Shadow mode (Phases 1-3):** the GS renders the whole frame as today; the native renderer renders its subset
   offscreen for comparison only. Zero risk to gameplay.
2. **Optional preview, CoreGS only:** depth-merge composite. The native car (high resolution) is z-tested against the
   upsampled GS depth from CoreGS's built-in VRAM (same process). Edges will be imperfect; debug or preview only. Not
   available with the PCSX2 GS process without a fork change (that process owns its VRAM).
3. **Pass ownership (Phase 4 on):** the whole `render_world` pass becomes native. The GS frame for that tick
   contains only the 2D passes. A stream marker (control record "layer: overlay begin") at the start of
   `render_panel` lets the consumer clear the overlay target to transparent. The compositor stacks: native 3D at
   internal resolution and wide aspect, then GS overlay layers (scaled, or per-element anchored for the HUD).
   Fades done by the HUD pass (`RacePS2Base::render_panel` fade, `func_003425E0`) stay in the overlay with alpha.
4. Every **GS-to-EE path** (readbacks, `CSR`/FINISH signals) must be satisfied by whoever owns the data:
   - native-owned flare data is written to the guest readback buffer by the native side;
   - the GS stream's readback for that record is answered from the native result or suppressed;
   - FINISH/SIGNAL events the EE waits on are still emitted (the celosia lesson: walk the chains for SIGNAL, FINISH
     and LABEL even when skipping raster; celosia-study 2.3).

### 7.5 Interpolation design

- **Render thread** separate from the guest thread. The guest publishes one immutable snapshot per tick (ring of 3).
  The render thread presents at the display rate with `alpha = (now - t_N) / tick`. It draws snapshot N with
  transforms blended toward N+1 (a 1-tick delay) for matched draws.
- HUD overlay from the latest GS frame (no blend). Feedback effects run per presented frame with the decay rescaled.
- The physics, game logic and guest VBlank cadence are unchanged (60 Hz). Presentation becomes independent of VBlank
  (window/pacing changes in `native/pacing.cpp` and `window.cpp`).

### 7.6 Testing tools

- Capture and replay (`.gt4scene`, private), headless native render from a capture, image diffs with masks (geometry,
  per effect), a per-draw inspector, and a "draw coverage" counter in F8 (native/GS/unsupported).
- Reference functions per VU1 family (CPU) versus the AOT VU1 (differential); shader versus CPU reference on recorded
  inputs.
- Owner gates on the reference scene, night race, wet track, rally; headless frame dumps must stay identical when
  the native renderer is off.

## 8. Scope note: fork versus in-repo

CLAUDE.md currently scopes a native (non-GS) renderer out of this repository ("possibly a fork"). This study does not
argue the policy; the technical consequences are:

- **In-repo:** the hook layer, scene capture and renderer would live next to the GS paths. It adds a third renderer to
  release builds and test matrices. The release executable must keep the no-GPL rule; the native renderer is MIT, so
  that is fine.
- **Fork with a shared runtime (recommended shape if a fork is chosen):**
  - keep the *generic* pieces in this repository: the guest hook registry, observe-only capture, GS stream layer
    markers and the per-element HUD layer markers. They are also useful in scope here: guest-side ultrawide,
    HIGH-FPS approach C checks, diagnostics;
  - put the renderer and its GT4 passes in the fork as a **renderer plugin**: a static library or DLL behind a
    narrow C ABI (`init`, `on_snapshot`, `present`, `on_readback`, `shutdown`);
  - the fork then rebases on our runtime with minimal conflicts. Pins (`docs/inputs/*.json`) and producer contracts
    stay in this repository; the fork adds its own pins for the plugin.
- **Fork without shared hooks:** the fork would patch dispatch and the producer itself and diverge on every runtime
  change. Not recommended.

## 9. Open questions (Unknown) and next read-only steps

1. Live race camera class and its `applyProjection` (aspect source): find the `CameraSys::*` vtables
   (classes.md section 15) and read their slot 1 in the ELF (method of section 2.4).
2. `render_cars` (0x00340950) is unmatched: read our translated unit for it (draw order, shadow draw, wheel calls).
3. The readback function `func_004A5178` (not decompiled): confirm it is a GS local-to-host transfer of the 64x8 flare
   buffer; map which guest bytes the EE then reads.
4. VU1 program roles: map `MSCAL n` values from captured H1 calls to the families in section 4; derive the lighting
   formula from the AOT C++.
5. Tire marks and the `func_00496638` pass (course flag 0x20, view flag 0x2000): locate them in a capture.
6. Translated-entry status for every hook address in section 2.5 (a CTest before Phase 0 ends).
7. Draw identity stability across ticks (matched-draw rate) on owner captures: the go/no-go for Phase 5.
