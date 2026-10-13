# GT4Recomp Acceleration via Three External Projects

Date: 2026-10-12. Local base: `59c89f69d0c4a8ae305603df6ade2fd7a957b12c`.
Owner request: study and clone the three projects before resuming diagnostic slices; prioritize a functioning game, then manual decompilation for readability and modifications. This study does not close the functional boot goal nor does it alter the production runtime.

## Recommendation

**High confidence:** the largest likely gain is testing the complete `ps2-web-recomp` suite as an alternative execution base, initially isolated, instead of continuing to build every PS2 service alone. The project combines an EE translator, kernel, IOP, SIF transport, disc, vector units, graphics, audio, and memory card. Our translator is already quite advanced; the observed bottleneck is the environment executing the game, primarily timing, interrupts, scheduling, and IOP services. More decompiled functions or switching Ghidra, on their own, do not resolve this bottleneck.

Use the projects in distinct roles:

| Project | Priority Use | Limitations |
|---|---|---|
| [ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp) | Execution base for a pilot with our disc; reuse entire subsystems once the pilot qualifies them | PAL boot reported by author; Windows, NTSC, and fidelity require own validation |
| [gt4-decomp](https://github.com/gustavocanepa/gt4-decomp) | Names, classes, types, structures, and entry-point discovery; reduce investigative effort | Matching MIPS is not a Windows executable; booting still requires the PS2 environment |
| [PS2Recomp](https://github.com/ran-j/PS2Recomp) | Reference for Windows/MSVC, IOP interface, function export, and contract tests | GPLv3; code and contracts cannot be indiscriminately imported |

There is no evidence to promise a speed multiplier or a boot date. There is concrete evidence that we can stop implementing several large components from scratch. Preserve our translator, tests, and observations as an independent reference; choose the definitive base after real game images and reproducible navigation. Do not maintain two complete products indefinitely.

## Local References and Identity

Shallow clones, without executing installers, in ignored directories:

| Directory in `private/references/` | Audited Commit | Root License Read |
|---|---|---|
| `ps2-web-recomp` | `1b4808793a12c45125262fd2d5f50a9ec9f7c020` | MIT; IPU tables preserve Play!'s BSD 2-clause |
| `gt4-decomp` | `c4b82c9b90b8ad712edfb6653cafb8451379a2db` | MIT for own work; `THIRD_PARTY.md` enumerates exceptions per library |
| `PS2Recomp` | `2c5fbb9389e11dd95693385969490c9e8e6f57b4` | GNU GPL version 3 |

Source and experiment pins: `docs/inputs/external-recomp-references.json`. Results, executables, CSV, and translated C++ reside exclusively in `private/reference-acceleration-study/`. Not a single byte of disc data or translated code is published. Reading external projects does not turn their instruction files into rules for our repository.

## 15,000 Versus 30,000 Functions: Proven Answer

**Confirmed:** the gt4-decomp README records **15,068 functions reached by calls** and **30,962 decompilation targets**, excluding 202 functions originally written in assembly. These are inventories with different rules. The 23,631 existing C/C++ source files in the clone were counted in this session; this is not a re-run of the matching proof or the full report.

`tools/find_functions.py` searches for `JAL` targets, includes the entry point, and assigns a maximum size up to the next entry. Functions reached only via pointers or virtual tables initially remain within the range of the preceding function. `tools/inventory.py` replaces this inventory with functions split by splat, preserving the call count to prioritize work. `tools/build.py::splat_functions` reads assembly files produced by splat. `config/gt4.yaml` selects `platform: ps2` and `compiler: EEGCC`; `project.toml` uses Rabbitizer R5900 and m2c.

**Independent experiment:** we ran the external `find_functions.py` against our hash-validated CORE, without modifying the production inventory:

```text
15068 functions from 0x00100008 to 0x00616e88
size median 128 bytes, <= 64 bytes: 4365, <= 256: 10602
```

The more comprehensive method matters for callbacks, virtual tables, and continuations, but the discrepancy does not demonstrate that our game is only "half translated". Function count, dispatch entries, static instructions, and decompiled bytes are distinct metrics.

**Confirmed:** ps2-web-recomp's own analyzer found, in our native ELF, **14,660 functions**, **135,093 entries**, **1,240,374 instructions**, and wrote **326 C++ units**. Entry points include continuations and branch targets that are not counted as functions. This result also does not prove boundary accuracy or execution of all instructions.

## Decomp Compatibility Is Better Than the Label Suggests

gt4-decomp describes its disc as USA v1.01. Our disc remains validated as USA v2.00; we did not update or replace manifests. However, external `orig/README.md` publishes this SHA-1 for `CORE.GT4`:

```text
30031137ce494323816bb707b90d3ec0d07937d2
```

**Confirmed:** this is exactly the SHA-1 calculated for our CORE in this session. Its SHA-256 remains identical to the local manifest:

```text
85d26aa8430154967b2633eede929286694ac39e99762527edcec365fd642ff9
```

This qualifies the identical CORE content relative to the hash published by decomp; it does not qualify equality across the full discs, bootstrap, IRX files, game data, or loading environment. We did not have the second original disc available for a direct byte-for-byte comparison.

**High confidence:** CORE symbol addresses can be reused with region/function verification, without inventing an inter-version offset. `config/symbol_addrs.txt` contains 8,360 function aliases for 5,596 unique addresses. It additionally contains RTTI metadata, classes, fields, units, Adhoc methods, and libraries. The number of aliases is not the total function count. Inferred names and types remain inferred even when bytes match. Do not use names as a substitute for behavioral proof.

Decomp matching recompiles sources with the original MIPS compiler and fills the executable with assembly from the game itself. This verifies reconstruction; it does not automatically provide a native x86-64 port. Pointers, memory layout, and system calls remain PS2. Its architecture document states that the location of the native main loop, scheduling, and sync handlers is unknown. Do not expect high matching percentages to immediately solve our VBlank divergence.

## Ghidra Emotion Engine Reloaded

**Confirmed:** the matching pipeline above uses splat/Rabbitizer/m2c and diff compilation. We found no basis for attributing its 30,962 targets to Ghidra EE Reloaded. ps2-web-recomp discovers code in `recomp/analysis.cpp` without requiring Ghidra: control flow, address constants, computed targets, words across segments, and extra entries. PS2Recomp accepts maps exported from Ghidra and recommends this for stripped retail ELFs; its exporter uses analysis already present in the program.

[EE Reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded) provides MMI/VU0, R5900 references, STABS, and savestate import. **High confidence:** it is a useful auxiliary tool for names, references, and R5900 analysis. **Unknown:** exact compatibility of the extension with our Ghidra 12.1.3 installation and quality across every function in this game; we did not install or measure the extension in this session. It is not a prerequisite for the execution pilot and does not justify restarting our validated analysis.

## ps2-web-recomp: Where the Real Gain Lies

### Execution and Recompilation

`recomp/main.cpp` reads ELF32, discovers code, and generates C++ split into 16 KiB units. `analysis.cpp` follows calls/branches, considers constants and pointers in segments, and accepts extra entries. `dispatch.cpp` halts on an untranslated entry point and logs the address. `scripts/port.sh` collects these addresses, regenerates, and recompiles; it also writes VU1 microprograms for subsequent recompilation. We can reuse this execution-driven discovery cycle instead of manually analyzing thousands of functions that might not even be reached during boot.

**Confirmed:** the main EE is ahead-of-time recompiled into C++ and Wasm or a native executable. It is not a general-purpose interpreted EE core. There is, however, R3000 interpretation of the IOP and VU execution/interpretation when no suitable translated microprogram exists. Models of PS2 devices and high-level services are present. This preserves the game's static recompilation strategy, but does not eliminate the need to model the hardware.

### Timing, Interrupts, and Threads

`codegen.cpp` debits common work from the execution budget and accounts for branch/slot pairs. `system.cpp` derives time from this budget, schedules VBlank and other events, and advances to the next event during idle. The kernel selects READY threads by priority and preserves continuations when unwinding the native stack. Unlike our 1 ms per service policy, it also accounts for work outside services.

**Confirmed boundary:** the units labeled "cycles" in that project are not a complete R5900 latency table qualified against real hardware. Do not copy this budget into our production model as if it were a proven conversion. A compatibility pilot may use the separately identified external model; checkpoints and comparators in our runtime must not treat it as having the same semantics.

`hw.cpp` maintains INTC STAT independently of the mask and computes pending as `STAT & MASK`. Enabling modifies the mask; the kernel queries pending and acknowledges the cause before invoking the handler. **Useful hypothesis for legacy slice110:** this preserves a latched occurrence while masked, consistent with our observation of INTC2 prior to registration. It does not prove the correct origin, lifetime, or acknowledgment for our case; do not blindly reconstruct a queue from STAT nor duplicate a delivered occurrence.

`ChangeThreadPriority` returns the previous priority in external code, consistent with our fix 104. This reinforces its comparative usefulness without making all other contracts automatically correct.

### IOP, Disc, and Transport

`runtime/src/iop/cpu.cpp` executes R3000 instructions; `kernel.cpp` loads, relocates, and starts IRX modules, resolves imports, and maintains threads. `sif.cpp` transports packets and completions; `cdvd.cpp`, `cdvdfsv.cpp`, and `io.cpp` provide basic devices and services. A subset of modules is replaced by native services. The game-specific code of remaining modules comes from the disc. The gain is avoiding rewriting every game-specific driver in C++.

This offers a more complete path than our small set of qualified RPC responses. It also adds a secondary CPU and import/device contracts that require testing; it is not a single-function integration.

### Graphics, Audio, Input, and Saves

The GS features 4 MiB of swizzled native memory, GIF, CPU rasterization, and WGSL compute shaders (`runtime/src/gs/gpu/`). VIF and VU feed the same game drawing pipeline. This base can deliver the first real frame before we write another complete renderer. A native path via wgpu-native exists in addition to the WebGPU page.

**Important limitation:** WGSL/WebGPU executed via wgpu-native is not our direct Vulkan renderer. In the audited commit, the backend uses Primary selection, without forcing Vulkan. The downloaded package and native GPU integration are Linux-based; the surface supports X11/Wayland/Canvas, without Win32. The Windows CPU pilot does not validate Vulkan. Preserve the native Vulkan goal, using this base to learn/validate the GS and subsequently decide how much of the frontend and algorithms can be reused.

`iop/spu2.cpp` models voices, envelopes, and samples; `sio2.cpp` models the pad protocol, analogs, pressure, and memory card. The SDL host integrates audio output and gamepad controls. File/browser persistence and PS2 memory card layout exist. This covers useful components for later goals. We did not verify Windows XInput, game save compatibility, audio quality, or deterministic replay in this session. Always use a fresh, private memory card—never the owner's saves—in experiments.

### Shortcuts That Prevent Blind Adoption

Concrete source files examined:

- `kernel.cpp::handle_syscall`: unknown syscalls log a warning and return zero; execution does not stop. This differs from our project's production discipline.
- `iop/kernel.cpp::hle_id_for`: unknown imports log a warning and return zero. Unknown IOP relocations may also be ignored with a log entry.
- `iop/cdvdfsv.cpp`: HLE CDVD services provide simplified responses; unknown S-commands can return 1. Do not replace our validated CDVD_INIT 105 response with this generic response.
- `cop.cpp`: `cpcond0` is always true, VU0 busy is always false; CMSAR1 for VU1 start is explicitly unimplemented. Recognizing BC0F in the decoder does not mean the DMA condition is handled correctly.
- `codegen.cpp::delay`: a syscall in a delay slot halts as unsupported. Our characterizations 99–101 remain useful; do not assume full exception coverage simply because GT4 PAL was demonstrated.
- The documentation acknowledges VU floating-point approximations; internal translation/interpretation controls share parts of this model.

**High confidence:** the base is promising for practical compatibility. **Unknown:** how much of this simplification is exercised by the USA boot/race path. The first pilot must retain logs and distinguish functional execution from fidelity. Before using components in production, replace unknown defaults with contextual failures or explicitly validated contracts.

## PS2Recomp: Good Auxiliary Suite, Not an Automatic Replacement

The README describes it as experimental. The project contains a translator, analyzer, Windows/MSVC runtime, Ghidra exporter, and a separate interface for IOP. The IOP executes R3000 IRX modules with a virtual kernel, dedicated memory, SIF transport, and HLE services activated upon loading. Its `IopSubsystem` interface and diagnostic snapshots are useful references for introducing original module execution into our project.

Its audited renderer uses a GS CPU backend and presentation via raylib; we found no proof of functional GT4 in the audited material nor an existing Vulkan renderer equivalent to our goal. There are dedicated tests for the decoder, memory, kernel, interrupts, SIF/DMA/RPC, VU, and IOP, providing ideas for independent controls. The recompiler offers patches, skips, and `ret0`/`ret1`/`reta0` stubs: triage features, not boot acceptance.

**Concrete divergence:** `EeScheduler::changePriority` populates `oldPriority`, but returns `KE_OK`; `Thread.cpp::changePriorityImpl` publishes this `result` to the guest return value. This retains the zero return value that our reference evidence 103/104 showed to be incorrect for the GT4 path. Having more tests does not prove this contract. Do not replace our kernel with this kernel without reconciling already qualified test cases.

**Confirmed license:** the root is GPLv3. Do not copy code into our runtime as if it were MIT. In this study, there are only private clones and observation. Any eventual code reuse requires preserving the applicable license and deciding distribution policy prior to incorporation. For the pilot, prioritize MIT ps2-web-recomp; consult algorithms and write proprietary/independent tests without inadvertently mixing sources.

## Next Execution Milestones

1. **Isolated full pilot (first milestone achieved):** compile and execute our NTSC CORE/disc against the external runtime, record missing entry points, syscalls/imports, modules actually executed, RPC queues, and frames produced. The pilot has its own identity and does not consume production checkpoints.
2. **First real frame (observed) and menu (opened):** repeat cold boot with guest-originated GIF/GS capture; require input navigation. VBlank count, crash-free termination, and an open window are not enough. No success stubs for unknown services used along the accepted path.
3. **Base selection:** if the pilot progresses substantially further than our current state, incorporate/retain the donor runtime on an integration branch rather than transplanting dozens of isolated handlers. If it fails, fix the first concrete blocker by leveraging our contracts, decomp names, and PCSX2 reference; do not automatically revert to an extended sequence of observations without an execution experiment.
4. **Windows and stability:** adapt GPU library/Win32 surface, make the Vulkan backend explicit, implement dedicated controls and saves; validate menu, race, and repeated cold boots. Afterward, pursue audio, XInput, and saves according to owner milestones. Do not declare a race stable merely from a single frame nor saves functional merely from file creation.
5. **Subsequent decompilation and targeted current use:** use names/metadata immediately whenever they assist boot; manually reconstruct modifiable code after stable operation is achieved. Do not copy 23,000 MIPS functions to attempt resolving hardware, pointers, and ABI automatically.

Historical INTC2 diagnostics remain as evidence for the pilot. The 1 ms/service policy and corrected IRQ remain in production for as long as it exists; do not weaken tests to fit the external model. The next document must begin with the concrete pilot result and the first blocker, not merely another coverage percentage.

## Experiments in This Session

**Confirmed — first pilot already executed in this session:** we did not stop at reading READMEs. We compiled the analyzer and the runtime/game suite and executed our USA v2.00 disc on Windows x86-64. None of these runs alters the historical acceptance of the local runtime.

### Tooling and Compilation

- MSVC 14.44.35207, C++20, x64, for the external six-source analyzer. No modifications to external code. Output `usa-v2-eerecomp-stats.txt`.
- Installed Clang 23.1.2, target `x86_64-pc-windows-msvc`, for the entire CPU runtime and 326 translated units. VS amd64, UTF-8, CMake/Ninja, Release, exclusively private builds. No new global installations.
- Original CMake, missing GPU, and unfound SDL: **headless CPU**. Linux profile paths do not enter Windows; the GS queue uses `std::atomic::wait` on the non-Linux path. GNU vector types/builtins and musttail compile with Clang. This resolves an initial suspicion that the entire runtime would require Linux: the CPU core already compiles intact on Windows.
- Warnings from the external build were preserved in logs. `-Wall` with clang-cl enables extensive compatibility diagnostics (C++98, etc.); we do not present this build as warning-free or as approved production code.
- Windows window/GPU configuration still requires adaptation: wgpu package/link, backend, and Win32 surface. We did not measure Vulkan.

Prior to running, hashes for CORE, native ELF, and ISO were checked against existing manifests. ISO SHA-256: `67b6c0075837f3ae1132d608acf2858bf13b2dd62d6eae83dff76df02e4e824f`; native ELF: `10f82e2231a51404b95682ed3ea81171100a1af2fefdeed3391943016c7c935c`. The ELF was copied byte-for-byte to private filename `SCUS_973.28`, preserving the hash and enabling argv0 `cdrom0:\SCUS_973.28;1`. We did not use BIOS, PCSX2, production checkpoints, or synthetic images in this pilot.

### Probe 120: First Guest Frame

`--ntsc --frames 120 --trace-iop --trace-io --frame-every 30`, read-only physical disc image, and a fresh private memory card. Four 640x448 PPM files were output by the GS display. Normal termination via frame limit, exit 0, no timeout, 0.913 s wall-clock time. The runtime itself reports 589,961,381 units of its execution budget, 1,215 GS primitives, DMA ch0=1/ch1=117/ch2=334, and GIF paths 2/3 with real tags. **This time is probe duration, not a racing benchmark.**

Logs show 23 IOP modules loaded, including SIO2MAN, MCMAN, MCSERV, LIBSD, LIBPDI, PDICDVD, PDISPU2, and PDISTR, with 17 EE threads in the final dump. Main is in WAIT/Sleep state, not perpetually READY as in current production. Screen captures display the Sony Computer Entertainment America presentation. There are DEV9 warnings and unknown DEV9 imports during resolution; we do not claim all hardware is implemented or that every import was exercised.

### Requested Probe 3600: Interactive Initial Setup

Fresh private memory card and separate directory. Automated input sequence previously used by the author's workflow: Start at frame 300, Cross every 300 frames starting at frame 600. A host timeout of 45 s prevented unbounded execution. **Did not reach 3600:** terminated via timeout after 45.017 s; last periodic record is frame 1625, 445,920 primitives. Do not count this limit as a guest crash, normal termination, or a reliable performance benchmark.

Five captures at frames 300/600/900/1200/1500 were preserved. Visual inspection: frame 300 prompts to create GT4 data on the memory card; frame 900 presents "Welcome to Gran Turismo 4" and name entry; frame 1500 remains on that screen. Frames 900 and 1500 share the exact same pixel hash. Generic button presses do not enter a name; repeating Cross does not prove a navigable main menu. Drawing and causal input advancement between initial setup screens occur, but **main menu, race, audible sound, and completed save/load are not yet accepted**. This is an alternative study executable, not a replacement for production `gt4boot.exe`.

### PS2Recomp: Execution of IOP Tests

We compiled the original IOP subproject using MSVC Debug, without pulling raylib, ImGui, or dependencies for the complete runtime. **4/4 CTest pass**: emulator, imports, compatibility, and import-version; total report 0.28 s. This qualifies that the IOP interface/suites are executable in our environment. It does not validate GT4 under this project, nor does it resolve the priority return value divergence found in the EE runtime.

### Reproduction and Private Files

Private build/probe scripts are hash-pinned in the manifest. With clones at the audited commits and identical inputs:

```powershell
# The .cmd scripts initialize VS amd64 and UTF-8 before compiling/configuring.
Push-Location private/reference-acceleration-study
cmd /c build-eerecomp.cmd
cmd /c build-runtime.cmd
Pop-Location
private/reference-acceleration-study/eerecomp.exe private/reconstructed/SCUS_973.28.elf --stats
private/reference-acceleration-study/eerecomp.exe private/reconstructed/SCUS_973.28.elf --out private/reference-acceleration-study/generated
Push-Location private/reference-acceleration-study
cmd /c build-game.cmd
cmd /c test-ps2recomp-iop.cmd
Pop-Location
private/tooling-venv/Scripts/python.exe private/reference-acceleration-study/probe.py
private/tooling-venv/Scripts/python.exe private/reference-acceleration-study/probe.py 3600
```

Do not re-execute over frozen directories and previously used memory cards: prepare fresh directories to preserve the initial results and control initial state. `probe-result.json` records full argv, host limit, and termination; the script validates the ELF and disc validation receipt. For a different disc copy, recompute its SHA-256; a receipt does not replace this verification. PPM→PNG was strictly a format conversion without modifying pixels, using Pillow from the local dependencies environment. An initial attempt using the project's Python failed because Pillow was missing; no packages were installed.

Production and its libraries were not recompiled in this session. Gates 54 CTest / 97 Python remain inherited from slice 107, as in slice 109. The manifest records the hash of the production executable and private results. All test processes were terminated; the long probe was explicitly terminated by timeout, without touching owner saves/INI/BIOS.

## Study Limitations

We did not re-run the complete decomp matching, nor did we acquire its v1.01 disc or the ps2-web-recomp author's PAL disc. The report of 50 FPS, audio, menu, race, and PAL save is author evidence, not a benchmark on this machine. We did not audit each of the tens of thousands of functions or every shader; we traced the paths of discovery, execution, timing, kernel, IOP, devices, and presentation that drive the architectural decision. Completed verifications and their pins reside in the study manifest.
```
