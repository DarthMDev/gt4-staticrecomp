# Isolated EE Runtime Candidates

Slice 118 continues the authorized P0/P1 integration plan. Two runtime profiles
share the same main-branch native runtime, adapters and frozen shaders:

- **Baseline:** the slice 116 rebuilt library, whose 327 normalized AMD64 objects
  equal the original qualified library. No guest source recovery changes.
- **Recovered:** a full fresh compilation of the slice 117 supplier-only source.
  The seed `0x0044EAC8` also recovers callback `0x0044EA58`; 45 instructions and
  nine dispatch entries are added. Existing call IDs are updated by the generator.

The production launcher/library pins remain separate. Neither candidate includes
the parallel OpenCode performance branch. A new executable is not evidence of
higher FPS, complete code recovery or PS2 fidelity.

## Preparation and gates

`scripts/ee_candidate.py prepare` verifies both frozen producers, all four
coverage-profile inventories, reviewed raw spans and analyzer/registry controls.
It copies the qualified baseline library, reviewed recovered sources and nine
hash-pinned shaders into a fresh immediate child of this checkout's `private/`.
The native adapter is derived from the verified donor with the current main
implementation. Donor, old capsules, cards and active sessions stay untouched.

The recovered library is compiled in a new CMake cache with Clang 23.1.2, C++20,
VS2022 x64 environment, Release `/O2 /Ob2 /DNDEBUG` and one worker. The library
receipt requires the same complete 327-object membership, exactly 18 reviewed
changed objects and 309 unchanged objects after timestamp normalization. The
copied baseline must match its authoritative qualified producer, not just a
mutable preparation receipt. Sources are rehashed after compilation.

`docs/inputs/slice118-ee-libraries.json` records separate reviewed library pins.
Native CMake accepts only `production`, `baseline` or `recovered`; unknown values
fail. Production still requires
`c837d13dae027f377aecdb0c39834ede2e9463eabf7f767fda6a3c5a0762de1f`. Candidate profiles require
their own exact library hash and isolated slice 118 paths. There is no arbitrary
hash override or permissive fallback.

Each native build uses a separate cache and runs existing CTest controls. Python
integration fixtures use `GT4_NATIVE_BUILD` to select the actual candidate
executables, retaining all existing assertions, including the real Vulkan,
Windows audio and native card-lock controls. Synthetic launcher controls execute
mock processes only. No game navigation or simulated gameplay is performed.

## Build recipe

From the managed checkout, with the assets checkout and qualified environment
described in [BUILD](BUILD.md), first prepare a fresh capsule:

```powershell
& C:\Antigravity\gt4-staticrecomp\private\tooling-venv\Scripts\python.exe scripts/ee_candidate.py prepare --assets-root C:\Antigravity\gt4-staticrecomp --output private/slice118-ee-candidates
```

That capsule already exists on the development machine. Preserve it; a new
attempt needs a new slice identity, corresponding reviewed paths/manifests and
fresh caches. Run builds and tests serially.

In a VS2022 x64 Native Tools terminal, configure the full recovered library:

```text
cmake -S tools/ee_producer -B private/slice118-ee-candidates/recovered/game-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang-cl.exe" -DCMAKE_MAKE_PROGRAM="C:/PROGRA~2/MICROS~2/2022/BUILDT~1/Common7/IDE/COMMON~1/MICROS~1/CMake/Ninja/ninja.exe" -DGT4_DONOR="C:/Antigravity/gt4-staticrecomp/private/references/ps2-web-recomp" -DGT4_GENERATED="C:/Users/Alano/.codex/worktrees/native-renderer-roadmap/gt4-staticrecomp/private/slice118-ee-candidates/recovered/generated" -DGT4_BUILD_TRANSLATOR=OFF
cmake --build private/slice118-ee-candidates/recovered/game-build --target ps2game
```

Then record its validated object/source differences:

```powershell
& C:\Antigravity\gt4-staticrecomp\private\tooling-venv\Scripts\python.exe scripts/ee_candidate.py record-libraries --output private/slice118-ee-candidates
```

The command writes a private library receipt. Publishing its metadata as the
candidate library manifest requires review of the observed differences; it does
not update a production pin. Once that separate manifest is qualified:

```powershell
cmd /c native\build_candidate.cmd baseline C:\Antigravity\gt4-staticrecomp
cmd /c native\build_candidate.cmd recovered C:\Antigravity\gt4-staticrecomp
```

These wrappers initialize the Visual Studio environment, reject existing build
directories, compile one worker at a time and run CTest. Prepared private assets
and previous verified producer capsules are still required. This is not an
end-to-end clean-checkout game build.

## Owner-controlled qualification

**Confirmed slice 118 result:** both profiles linked with zero observed warnings
and passed CTest 3/3; Python 59/59 passed without skips, including the actual native
candidate fixtures. All nine shaders equal production. Baseline CTest took 13.04s;
recovered CTest took 12.13s. These are check durations, not gameplay benchmarks.
See [baseline metadata](inputs/slice118-ee-baseline.json),
[recovered metadata](inputs/slice118-ee-recovered.json) and
[library metadata](inputs/slice118-ee-libraries.json).

No manual game session was launched during build qualification. The owner had an active OpenCode baseline
session, which was preserved at that checkpoint. Ready local `launch-baseline.cmd` and
`launch-recovered.cmd` files are inside the ignored slice 118 capsule.

**Confirmed subsequent startup observation (2026-10-13, 13:16 America/Sao_Paulo):**
at the owner's explicit request, the previous game closed through its normal window
close handler. The baseline candidate then opened a responding 16:9 window and its
log progressed through game module loading. The standard launcher used the existing
owner card and created `card-before.ps2` (8,650,752 bytes) in private session
`session-20261013-131623-817539`. No navigation or simulated input was performed.
The window was left open for the owner; menus, racing, audio continuity and
performance remain unqualified. Recovered has not been launched. Frozen build
manifests retain their original no-manual-launch checkpoint observations.

Close the existing game through its window before launching a candidate. Start
with **baseline** to qualify the rebuilt producer/runtime integration, then use
**recovered** to compare the intentional guest-source delta:

```powershell
cmd /c Jogar-GT4-Candidato.cmd C:\Antigravity\gt4-staticrecomp --profile baseline
cmd /c Jogar-GT4-Candidato.cmd C:\Antigravity\gt4-staticrecomp --profile recovered
```

Run one at a time. The launcher uses the assets checkout's existing
`private/manual-play/card.ps2`, the same Python session lock and native card lock,
and the existing per-session backup behavior. It never reseeds an existing card.
Executable/shader hashes are checked in the candidate checkout; ISO/CORE/ELF/card
remain in the assets checkout. Existing launch behavior defaults to production.

Owner checklist: boot/menus, showroom car display, track loading, steering and
throttle/brake, actual racing, audio and normal window closure. Report the
profile and scene where a regression occurs. Timing/performance observations
are separate from functional confirmation. Full races, broad save cases,
physical XInput and PS2 fidelity remain open qualification work.

P0 remains in progress until candidate gameplay and remaining coverage gates
are qualified. P1 still needs a named car's verified asset identity, concrete
instance/camera data, conventions and effect/lifetime contracts before native
scene rendering. The dedicated renderer has not replaced live graphics.
