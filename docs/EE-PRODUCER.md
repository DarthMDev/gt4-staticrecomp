# Reproducible EE Producer

Slice 116 starts P0 with a standalone producer. It rebuilds the frozen donor's
offline translator using the qualified Clang toolchain, reconstructs the pinned
ELF from verified CORE bytes, regenerates EE C++, and builds `ps2game.lib` without
an existing CMake cache. It does not replace the production library or launcher.

## Inputs and trust boundaries

- Pinned USA v2.00 identity: `docs/inputs/usa-v2.00.json`.
- Reviewed ELF layout, entry and BSS policy: `docs/inputs/usa-v2.00-native.json`.
- Donor revision, source identities and frozen generated inventory:
  `docs/inputs/external-recomp-references.json`.
- Windows x86-64, VS2022 SDK/ABI, clang-cl 23.1.2, C++20, Ninja, CMake 3.24+,
  and Python with `scripts/requirements.txt`.

The script anchors canonical manifest content hashes. Whitespace changes do not
repin inputs; content changes fail until a reviewed implementation updates those
anchors. A clean donor checkout at
`1b4808793a12c45125262fd2d5f50a9ec9f7c020` and recorded source hashes are required.
No old ELF or old generator binary is necessary for the full recipe.

CORE is raw-deflate decoded with an 8 MiB limit. Every payload record, address,
size and hash must match the reviewed policy. ELF header/program-header layout,
zero-filled gaps and inherited BSS extent reproduce the qualified ELF exactly.
**Confirmed:** byte identity. **Unknown:** independent PS2 fidelity of that BSS
policy; this work does not establish it.

## Full CORE-to-library recipe

From the checkout receiving the new producer, use absolute paths:

```powershell
cmd /c tools\ee_producer\build.cmd C:\Antigravity\gt4-staticrecomp C:\Users\Alano\.codex\worktrees\native-renderer-roadmap\gt4-staticrecomp\private\slice116-ee-producer
```

The first argument identifies a read-only local assets checkout containing the
verified donor and Python environment. It may be the current checkout. The second
must be a **fresh immediate child of the receiving checkout's `private/`**. Use a
new slice/output identity for each attempt; the example directory already exists
on the development machine and must not be reused.

Stages: verify inputs; reconstruct ELF; configure/build translator; regenerate
source; configure/build library; verify source inventory again and record library.
The two CMake caches are new and independent. Compilation uses one worker to keep
resource use bounded. Execute this build serially with other local builds/tests.
The wrapper ends with the standalone producer's CTest Python controls. In a
checkout without the native runtime binaries, three existing integration fixtures
are explicitly skipped; that result does not qualify the candidate runtime.

The full recipe uses the verified extracted CORE by default. For independent ISO
preparation in a different fresh directory:

```powershell
private/tooling-venv/Scripts/python.exe scripts/ee_producer.py prepare --assets-root C:\Antigravity\gt4-staticrecomp --output private/slice116-iso-verification --iso "C:\Antigravity\gt4-staticrecomp\Gran Turismo 4 (USA) (v2.00).iso"
```

This verifies the ISO/config/bootstrap/CORE and extracts only the verified CORE.
If Python lives in a separate assets checkout, substitute its absolute executable
path for the relative Python path in that example.
It prepares input files, not a runnable game. `generate --output <producer>` can
use the hash-pinned historical translator for a source comparison only; it does
not qualify a rebuilt toolchain. The full wrapper builds its own translator.

## Receipts and checks

Private receipts contain source/tool/configuration hashes and absolute paths:
`producer.json`, `generation.json`, `library.json`, both `toolchain.txt` files and
`generation.log`. ELF/CORE, generated code, executables and libraries stay ignored.
The public [slice 116 metadata](inputs/slice116-ee-producer-contracts.json) records
distributable evidence and limitations.

Every generated file is hashed, with normalized names compared to the frozen
328-file inventory. A difference fails; this producer has no permissive mode for
new entries or game modifications. Future deliberate changes require a separately
reviewed producer contract, input/options manifest and semantic qualification.
Fresh compilation does not make a changed library eligible for production: the
existing CMake and launcher hash gates remain unchanged.
The library receipt also validates AMD64 archive membership and a pinned inventory
of complete COFF object payloads after normalizing only their four-byte timestamps.
Slice 116's 327 objects match that baseline inventory. Archive member paths and
timestamps explain the differing library hash/size; runtime behavior remains untested.

Synthetic Python controls cover independent ELF construction, malformed compressed
inputs, bounded allocation/layout, pin changes, protected output paths, linked
artifacts, inventory changes, forged receipts and toolchain configuration.
They run through the existing Python discovery and root CTest Python entry.

## Qualification still open

P0's generation/build portion is established by slice 116. P0 remains **in
progress**: registry coverage includes known omissions and owner menus/showroom/
race checks are pending. Slice 118 now links separate candidates and passes their
active native checks. Source equality reproduces the current implementation's limits.
It proves neither complete function recovery nor PS2 fidelity.

[Slice 117 callback recovery](EE-CALLBACK-COVERAGE.md) diagnoses one cold supplier/
callback pair and compares four explicit-seed profiles in a fresh diagnostic capsule.
The supplier-only seed recovers both bodies, with 18 changed units compiled offline.
This experiment has separate controls/receipts and does not relax this baseline
producer's source equality or the production library gates.

The ordinary native build still requires the original pinned private library and
private adapters/shaders. [Slice 118 candidates](EE-CANDIDATES.md) integrate the
rebuilt baseline and recovered source in isolated runtime builds with separate pins;
owner gameplay and remaining coverage are pending. This producer does not yet
provide an end-to-end clean-checkout game build or replace production.
