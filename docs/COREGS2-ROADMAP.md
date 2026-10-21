# CoreGS 2.0 roadmap (beyond parity)

Owner, 2026-10-21. CoreGS 2.0 (TASK-027) is our MIT hardware GS renderer on Vulkan, built into the game executable.
It follows the same idea as the PCSX2 GS (translate GS commands for a modern GPU), written clean room from the
TASK-026 specification (`docs/hw-renderer-spec`). This page lists what it can do better than the PCSX2 GS
(gt4-gs.exe) and what we can add on our own **after 1x parity** (spec milestones up to M8). Nothing here changes the
game's behaviour by default: every enhancement is optional, and the faithful image stays the default.

The rules of AGENTS.md apply: no JIT, MIT code, no GPL in the game executable, and the clean room for TASK-027
implementers (decision 0070). Third-party SDKs must have licences that allow redistribution with an MIT program and
must never be GPL.

## Why it can be better than the PCSX2 GS

1. **One process.** No GPL, so iOS becomes possible and Android simpler. No GS stream between processes (about
   0.8 ms per race frame of writes today) and no cross-process readback round trip (docs/FRAME-COST-STUDY.md).
   No separate renderer download.
2. **Game knowledge instead of heuristics.** The PCSX2 GS is generic and guesses what a draw is (texture, render
   target, effect). We have the decomp and runtime hooks, so we know when the car environment map, the mirror, the
   flare, the HUD (`RaceDisplay::render`) or a menu is drawn. Fewer guesses means fewer glitches and less wasted work.
3. **No readback stalls.** The night-race flare visibility readback (4-5 ms per frame today) can be answered inside
   the renderer, deferred one frame, or computed from depth, because we know what the game uses it for.
4. **No shader-compile stutter.** GT4 uses a finite, measurable set of GS states (spec census). Ship the pipelines
   precompiled; nothing compiles during play.

## Enhancements we can own

### Image
- **Separate UI layer.** The HUD hook marks the HUD's start and end. HUD draws also go to a 16:9 overlay at the
  display resolution, still drawn into emulated VRAM for fidelity. The present then composites the 3D at the screen
  aspect with a crisp HUD that can be repositioned, scaled or hidden. True ultrawide, no stretch tricks.
- **Fractional render scale** (the launcher's 50-200% slider without rounding).
- **Per-target resolution.** The car environment map, rear-view mirror and shadows are separate render targets at
  low resolution; each can be upscaled on its own.
- **Forced anisotropic filtering** and LOD bias on 3D textures only, never on 2D.
- **Better anti-aliasing:** SMAA first; TAA once motion vectors exist (see below).
- **HDR10 output** with faithful tone mapping; CAS sharpening.
- **HD texture packs and texture dumping** (spec chapter 11), the base for mods.

### Modern upscalers and frame generation
What the vendor SDKs need, and what CoreGS can provide:

| Input | Status in CoreGS | Notes |
|---|---|---|
| Colour before the UI | Yes, with the UI layer | The HUD must be drawn after upscaling or generation anyway |
| Depth buffer | Yes | GS Z targets, upscaled. The depth is non-linear post-projection Z; linearise it with the camera parameters from the camera hook (0x388B38) |
| Sub-pixel jitter | Yes | The camera hook already rewrites the projection matrix; add a per-frame jitter |
| Camera motion vectors | Yes | From depth and the current and previous view-projection captured by the camera hook (reprojection). Correct for the static world |
| Object motion vectors (cars, wheels, particles) | Hard | The GS sees only post-transform vertices. Options: per-object transforms from the decomp (car matrices), or primitive matching across frames (the TASK-045 tooling). Without them expect ghosting on cars |
| Exposure, reactive or transparency masks | Partly | Draw categories are known (smoke, glare, flare); build masks from them |
| Present control | Yes | In-process present scheduling |

Candidates, in order:
1. **AMD FSR 2/3 upscaling and FSR 3 frame generation:** MIT, with Vulkan versions. First, because it is open and
   vendor-neutral.
2. **NVIDIA DLSS Super Resolution and DLSS Frame Generation** through Streamline, on Vulkan. The redistributable binaries
   are under NVIDIA's licence. Needs good motion vectors for cars.
3. **Intel XeSS:** Vulkan; redistributable binary.
4. **FSR 4 (ML) is DirectX 12 only today.** That would need a D3D12 present path or interop on PC; evaluate when
   needed.
5. **Neural rendering features (new DLSS generations):** check each feature's inputs when its SDK is public. Features
   that need engine-level control of materials and lighting fit the native renderer (TASK-065..073) better than an
   emulated GS.
- Frame generation fits the fixed 60 Hz simulation (TASK-082): the game stays at 60, generation presents 120/180.
  The +latency should be offset with a Reflex-style present and late input (TASK-084 also helps).

### Effects gated by game state
- The game state is known (race, pause, photo, replay, menus: docs/CONTEXT-INPUT-STUDY.md and the hooks), so new
  effects run only where they make sense, and always **before** the UI layer. No ReShade-style UI bloom, no effects
  in menus.
- Candidates: ambient occlusion (SSAO or GTAO from depth), colour grading, bloom tuned per scene, contact shadows,
  depth of field in replays and photo mode.
- Classification of transparent draws (smoke, glare, flare, particles) keeps depth effects from smearing them.

### Performance and pacing
- Variable frame rate and interpolation in process (TASK-082, TASK-045): render-only passes and extra presents with
  no protocol between processes.
- Reflex-style latency control: present just in time and read input late.
- VRR-aware pacing.

### Platforms and future
- Mobile from the start: tile-friendly passes and memory budgets, and a Metal backend for iOS (TASK-023/024 move
  onto CoreGS).
- The foundation of the native renderer: the hybrid plan (TASK-065..073) replaces parts (the race world first) and
  CoreGS covers everything else.
- Tools: capture and replay, pixel comparison and a frame analyser (partly done).

## Honest limits
- The PCSX2 GS has years of edge cases solved. Parity must be proven scene by scene with the chapter 14 protocol
  (night, rain, rally, replays, photo). gt4-gs stays the default until CoreGS 2.0 is better.
- Object motion vectors are the hard part for DLSS/FSR and TAA quality on cars.
- Every enhancement is opt-in; the default image stays faithful to the original.

## Order
Parity first (spec milestones through M8, the current focus). Then:
1. the UI layer;
2. the fractional render scale and per-target resolution;
3. precompiled pipelines;
4. readback removal;
5. AF and SMAA;
6. FSR 2/3;
7. camera motion vectors and jitter, then DLSS SR and XeSS;
8. frame generation together with TASK-082;
9. object motion vectors;
10. state-gated effects (AO first);
11. HDR10;
12. texture packs;
13. mobile backends.
