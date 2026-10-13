# Slice110 — external native pilot: profile and menu

Date: 2026-10-12. Fork baseline: `95dd453`.
Branch: `codex/runtime-menu-pilot-110`. Experiment authorized by owner
following external study/decision0051; does not modify the production runtime.

## Question and criteria

The full MIT runtime of ps2-web-recomp already renders initial screens with our
inputs. This slice verifies whether controlled commands allow creating a profile
and reaching a responsive menu, prior to deciding on a broader integration.
Completing thousands of frames without failure is not sufficient to accept a functional boot.

**Confirmed:** the same native Windows x64/Clang23.1.2 executable from the study
is used, without service patches, translator changes, or guest code modifications. SHA256:
`4a4f36899738032f44f879de422984951bd3a2e6824552ca344a06d1d1dd0ade`.
Its source is pinned to ps2-web-recomp
`1b4808793a12c45125262fd2d5f50a9ec9f7c020`. Full source and build identity:
`docs/inputs/external-recomp-references.json` and external report.

The launcher for controls 2–4 reverifies executable, CORE, ELF, and full ISO
against existing manifests. The earlier control 1 helper verifies executable, ELF,
and ISO; it does not verify CORE separately. Verification never updates manifests.

| Input | SHA256 |
| --- | --- |
| ISO USA v2.00 | `67b6c0075837f3ae1132d608acf2858bf13b2dd62d6eae83dff76df02e4e824f` |
| Reconstructed ELF | `10f82e2231a51404b95682ed3ea81171100a1af2fefdeed3391943016c7c935c` |
| CORE | `85d26aa8430154967b2633eede929286694ac39e99762527edcec365fd642ff9` |

## Instrumentation and limits

`scripts/external_runtime_probe.py` utilizes the existing fingerprint, literal
arguments, and a time-bounded subprocess. Outputs and memory card reside in a
new directory under `private/`; existing directories are rejected. Copies the ELF
with the name `SCUS_973.28` required by the game's argv0 and also verifies the copy.
Captures PPM every 100 VBlanks of the external model, syscall trace, and stop
reason. Buttons are injected via the donor's original `--press` mechanism.
PNG is a lossless conversion from PPM for inspection, not a fabricated screen.

The donor returns exit 0 even on certain guest halts. The launcher only accepts
execution completion if the log states `frame limit reached`, matching the
requested count, with exit 0 and no timeout. This outcome **does not certify menu or boot**:
captures and their effects must be examined separately.

To reread data, `--card-seed` accepts only a private image pinned in this slice's
manifest, verifies hash/size before copying, and verifies the copy. The process
receives exclusively the new card, and the result identifies the origin. This keeps
the original card and its evidence frozen.

Seven synthetic controls in the existing Python suite cover altered inputs,
new private destinations, literal argv, corrupted copy, timeout with log and
exit 0 after early halt, altered seed, and preservation of the original card.
They contain no game bytes and do not execute the donor.

## Preserved experiments

All runs start from ELF/disc and a non-existent private card, NTSC, CPU/software
GS, no GPU/SDL, no production checkpoints. Frozen results and helpers reside in
`private/slice110-menu-pilot/`; distributable metadata is pinned in
`docs/inputs/slice110-external-menu-pilot.json`.

1. `name-control1`: 2000 frames, 110s ceiling, normal termination in 52.842666s.
   Cross300 precedes the interactive screen; Left600/Cross630 cancels creation.
   Frame 700 shows that GT Mode is not playable without creating data. Negative
   control preserved, not attributed to runtime failure.
2. `name-control2`: 2000 frames, 110s ceiling, normal termination in 65.379512s.
   Start300/Cross600 accepts initial prompt; Left950/Cross980 types **L**;
   Right1010/Cross1040 selects Enter. Frame 1000 shows L, 1100 requests name
   confirmation; Cross1300 confirms. Frame 1600 prompts to create save data.
   Start1500 does not dismiss this dialog. Not the main menu.
3. `name-control3`: 3000 frames, 180s ceiling, normal termination in 125.166590s.
   Same prefix without Start1500, Cross1700 and 2000: frame 1800 reports 1488KB
   space, frame 2200 prompts to format the new card, with Cancel selected by default.
   Without further confirmation, no persistent card is created, and the menu is not reached.
4. `name-control4`: 4500 frames, 300s ceiling, normal termination in 160.715291s.
   Right2300/Cross2330 accepts formatting; frame 2400 displays actual progress.
   Cross2700/3000/3300 advances through autosave, verification, and card protection instructions.
   Frame 4500 still awaits final confirmation. Persistent 8650752-byte card produced,
   SHA256 `bb10a96fd78e45d648023f9962739c09f152ce01ae45c3fea1d998a71d5e3072`.
5. `card-reread5`: fresh ELF initialization using a hash-verified copy of card 4.
   Frame 600 displays the main menu (Arcade Mode / Gran Turismo Mode);
   frame 1000 displays course categories, 1400 displays Nürburgring selection.
   Cross600/900/1200 advances screens. Does not repeat creation/formatting/naming.
   Frame 1900 displays car manufacturers, 2500 displays 3D Ford Ka with specs
   and rendered model. Execution requests 3000 frames but reaches the 180s host ceiling
   after the last capture at 2700 (180.923369s). Launcher correctly returns 1,
   preserving log/captures; not a diagnosed guest halt or proof of stability.
   No race observed.
6. `menu-repeat6`: reduced 1500-frame repetition of prefix 5, same seed 4 and
   fresh destination; normal termination in 56.565667s, frame-limit/exit 0.
   The 14 PPM captures from 100 to 1400 are byte-identical to control 5 (SHA256),
   including menu and course selection. Not a full guest-state or PS2 reference comparison.
7. `gt-mode7`: initialization with the same card, Down600/Cross630 for GT Mode;
   frame 1000 shows Home / License Center / event map, Cr 10,000, and Day 1.
   Cross1100 enters Home; frame 1200 displays **L's Home**, with 0.0% / 0 points / Day 1
   and Garage / Status / Options / Photo Lab / Trade / Diary / Save menus.
   Confirms reread of name L created in control 4. 1500 frames, normal termination
   in 113.342559s, frame-limit/exit 0; does not replace the repeated Arcade control.

Timings are host measurements of the pilot under tracing and software rendering,
under variable loads; they are not benchmarks or instruction-to-PS2-cycle conversions.

## Evidence boundaries

**Confirmed:** controls 1–3 respond to buttons and draw the actual screens described.
The launcher does not modify the external runtime or production state.
The legacy `build/gt4boot.exe` remains with SHA256
`839adaa22f48ba674fc8f5f6c498299a9329598dedf23ca3ee507f1f1c76dfab`.

**Confirmed in control 5:** main menu and Arcade navigation up to course selection,
starting from a clean boot and rereading a private card created by the game.
Control 7 confirms GT Mode map and **L's Home** identity—not merely the existence
of a card file. No race or progress saves were performed.

**Unknown at this stage:** racing, broad save coverage, temporal/hardware fidelity,
and runtime adoption. Donor fallbacks and limits remain those identified in
study 0051; they do not become correct contracts simply because an image appears.
The observed `cfc2 28` returning 0 requires dedicated handling prior to production.
Direct Vulkan, audio, and XInput remain subsequent goals.

## Verification

Final Python suite: 104 tests, 98 run / 6 skipped, 73.546s, all passing.
After making unpinned seed rejection descriptive, the seven launcher controls
pass again in 0.183s, including external/unpinned seed rejection.
Suite socket ResourceWarnings were preserved; they are not new launcher failures.
Native CTest: 54/54 pass, 60.11s. Production executable hash rechecked after
suites; identical to 107. Control 4 terminated; reread/repetition controls terminated.
Production binary and seed card retain their hashes. Donor fallbacks remain
qualification debt.

## Reproduction and next steps

Example of short repetition (always use a new output directory):

```powershell
private/tooling-venv/Scripts/python.exe scripts/external_runtime_probe.py private/slice110-menu-pilot/new-menu-repeat --frames 1500 --timeout 120 --card-seed private/slice110-menu-pilot/name-control4/card.ps2 --press 300:start --press 600:cross --press 900:cross --press 1200:cross
```

The seed must exist and match the pinned hash exactly; if absent, recreate
control 4 using the argument list in the manifest; never fabricate the card.
For GT Mode: Down600/Cross630/Cross1100 after Start300, same seed.
All original commands, reasons, timings, helper versions, and hashes of
logs/cards/captures are in the 110 manifest. No payload is published.

The manifest records seven runs and 239 private artifacts/controls. Census across the
seven logs: zero `unhandled syscall`, missing imports **actually called**,
unknown IOP instructions, unknown RPC servers, or card write errors.
Each run records one `cfc2 28` warning. This does not validate service contracts
implemented with stubbed or simplified responses. Tracing in this launcher does
not enable `--trace-iop-modules`: do not interpret the absence of resolution
warnings as proof that every import has a dedicated implementation.

Helpers were frozen per run: control 2 predates ELF copy verification; 3/4 add
this verification; 5/6 add pinned seed; 7 uses descriptive unpinned seed rejection.
Final code is pinned separately in the manifest.

**High confidence:** leveraging the full runtime reduced the boot frontier to
practical Windows integration: the pilot launches and navigates the menu, Arcade,
and GT Mode. This outcome justifies proceeding with 0051/0052, retaining legacy
contracts to evaluate differences. It does not establish PS2 equivalence.

Next slice: make MIT build/host reproducible in a target separate from MSVC
`gt4boot`, expose live guest output in Win32, and qualify the GPU path toward
a native Vulkan renderer. Audit actually executed services and compare contracts
before any adoption. Without qualified racing or performance, no automatic
clock/IRQ/production alterations.
