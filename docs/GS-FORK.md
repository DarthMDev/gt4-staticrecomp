# GS fork: `moesuito/gt4recomp-gs`

Status: TASK-033, proof-of-concept package published (`gt4gs-v0.1.0-poc`), waiting for owner review. Decision [0070](decisions/0070-pcsx2-gs-separate-process.md) and
[GSDX-PLAN](GSDX-PLAN.md) section E.1 set the rules followed here.

## Repository

- URL: https://github.com/moesuito/gt4recomp-gs (public, GPL-3.0-or-later, not affiliated with PCSX2).
- Default branch: `gt4gs/main`, based on PCSX2 tag `v2.8.2` (commit `fd9d310ccbb6b8b62c976da8886a3c8fd3a10ff3`,
  the latest v2.8.x stable release on 2026-10-15). Upstream history is kept in full.
- **Standalone repository, not a GitHub fork.** The account already has a fork in the PCSX2 network
  (`moesuito/ARMSX2`), and GitHub allows one fork per network per account. The repository was therefore
  created with `gh repo create` and pushed with the full PCSX2 history at `v2.8.2`. The PCSX2 history,
  authors, copyright notices and credits are preserved; only the GitHub fork link is missing.
- Upstream: https://github.com/PCSX2/pcsx2 (read-only for us).

Changes from upstream are kept to `gt4gs/` (README, BUILD, NOTICE), the trimmed dependency script
`.github/workflows/scripts/windows/build-dependencies-gt4gs.bat`, the packaging script
`package-gt4gs.ps1`, the workflow `gt4gs-windows.yml`, and upstream workflow triggers set to manual dispatch.

## Build and CI

- Recipe: [gt4gs/BUILD.md](https://github.com/moesuito/gt4recomp-gs/blob/gt4gs/main/gt4gs/BUILD.md) in the fork.
  Summary: Windows Server 2025 runner, MSVC (VS 2022), Ninja, `ENABLE_QT_UI=OFF ENABLE_GSRUNNER=ON`,
  dependencies from the trimmed script (Qt removed), cached by script hash.
- Workflow: `.github/workflows/gt4gs-windows.yml`, `workflow_dispatch` only since 2026-10-16 (a manual fallback; it
  had a `gt4gs-v*` tag trigger until 0.3.0).
- **Release flow (owner decision 2026-10-16): local.** Releases from `gt4gs-v0.3.1` on are built, packaged and
  published from the owner's PC with `gt4gs/release-local.ps1` (clean tree and tag checked out; local recipe of
  `gt4gs/BUILD.md`; same zip layout as CI plus compiler, CMake, Ninja and dependency-script hash in `SOURCE.txt`;
  `git archive` source archive; `SHA256SUMS`; `gh release create`). GitHub Actions took 27 minutes per release;
  the local build takes minutes. The game pins the published zip after downloading it back. Anyone can still rebuild
  any release from its tag with `gt4gs/BUILD.md`.
- CI runs (all on `moesuito/gt4recomp-gs`):
  - [38818516056](https://github.com/moesuito/gt4recomp-gs/actions/runs/38818516056): `workflow_dispatch`, failed
    at configure. `vswhere -version "[17,18)"` found no VS on `windows-2025`, which has Visual Studio 18
    (MSVC 2026). The dependency build had already passed.
  - [38820151575](https://github.com/moesuito/gt4recomp-gs/actions/runs/38820151575): `workflow_dispatch`, success
    (fix: `vswhere -latest`, commit `3fd7faba8`). Dependencies built cold in about 13 minutes.
  - [38823289103](https://github.com/moesuito/gt4recomp-gs/actions/runs/38823289103): tag `gt4gs-v0.1.0-poc`,
    success, dependency cache hit, release published.
- Tool versions in the runs: CMake 4.4.4, Ninja 1.13.2, MSVC 14.51.36231 from Visual Studio 18 Enterprise
  (`windows-2025` image; the exact image version is in the job log).
- Upstream workflows were set to `workflow_dispatch` only (see the TASK-033 section of [TASKS.md](../TASKS.md)).
- Upstream workflows were set to `workflow_dispatch` only, so PCSX2's own Linux, macOS, Qt, flatpak and cron
  jobs do not consume Actions minutes. Nothing was deleted.

## Releases

- Tag `gt4gs-v0.1.0-poc` (prerelease) on commit `3fd7faba8`; `gt4gs/main` is at `70a0aa66f` or later (the
  later commits are documentation only). Release: https://github.com/moesuito/gt4recomp-gs/releases/tag/gt4gs-v0.1.0-poc
  Assets (download checked against `SHA256SUMS` with `sha256sum -c`: both OK):
  - `gt4-gs-0.1.0-poc-windows-x64.zip`, 18,202,463 bytes, SHA-256 `745760472acf0c51242998d8e5efb5c14a8a03c43f3e94ab71569a8d2f8ce7d2`
  - `gt4recomp-gs-0.1.0-poc-source.zip`, 24,954,987 bytes, SHA-256 `6cae6c3ac9cc607f1d764a891f9641b2ef79e786b48bfdbb04f7cccabc6ce371`
  - `SHA256SUMS`, 199 bytes

The release contents are:
  - `gt4-gs-<version>-windows-x64.zip`: `gt4-gs.exe` (the stock `pcsx2-gsrunner`, renamed), the DLLs it
    needs, `resources/`, `COPYING.GPLv3`, `NOTICE`, `README.md` and `SOURCE.txt` (tag and commit);
  - `SHA256SUMS` (SHA-256 of the zip and of the source archive);
  - `gt4recomp-gs-<version>-source.zip`: `git archive` of the tag (GPLv3 6(d)).

### How a release is verified

1. Download the zip and `SHA256SUMS` from the release page.
2. Run `sha256sum -c SHA256SUMS` (Git Bash or Linux) and compare with the release page.
3. Read `SOURCE.txt` in the zip, check out that commit and build it with BUILD.md. Compare the file list
   with the zip. Bit-identical output is a goal and is recorded as a result, not assumed.
4. The game never checks the hash itself; the launcher pins version and SHA-256 (TASK-043).

No build provenance attestation is published yet. It can be added once the release workflow is stable.

## Update and rebase policy (per PCSX2 release)

- We track PCSX2 **stable v2.8.x** tags only, for the release build. A new stable tag starts a new branch
  `gt4gs/<tag>` from the new tag; `gt4gs/main` is moved only after the new build passes CI and the
  frame comparison of TASK-034 is repeated. Old release tags are never rewritten.
- Our changes stay in `gt4gs/` and in the small set of build and CI files listed above, so a rebase is
  mostly a conflict check in the workflows. Any change to PCSX2 source files is listed in NOTICE with date.
- Each rebase is recorded in `docs/journal/` and in the release notes of the next `gt4gs-v*` tag.
- Security or crash fixes from later PCSX2 releases are taken on the same rebase path; no fix is applied
  to a published tag.
- A `v2.9.x` nightly is used only for the proof of concept (TASK-034 uses `685637d`), not for releases.

## Clean room and content rules

- The fork holds no game code, game data, BIOS, keys, disc images or GT4Recomp private content.
- GT4Recomp's MIT code does not go into the fork. The MIT protocol header is vendored later by hash (TASK-035).
- Agents that work on the fork are PCSX2-tainted and never work on TASK-027 (CoreGS 2.0).

### gt4gs-v0.3.1: first local release (2026-10-16)

Built from the tag checkout with `gt4gs/release-local.ps1` (fork commit `3834dbf57` of the script; tag commit
`0099d77e1`), MSVC cl 19.44.35222 (VS 2022 Build Tools), CMake 4.3.1, Ninja 1.13.2, no LTO, dependencies of
`private/slice193-gt4gs-deps` (dependency script sha256 `005368ff...`). Release:
https://github.com/moesuito/gt4recomp-gs/releases/tag/gt4gs-v0.3.1 (prerelease):
  - `gt4-gs-0.3.1-windows-x64.zip`, 13,395,329 bytes, SHA-256 `e24dd2b894d2f6e68b96735df83a62da7a7c3e6c449ad9a8743a6a01dd45546f`
  - `gt4recomp-gs-0.3.1-source.zip`, SHA-256 `2046bcbc5493346c6b62e6458c3a57beb29678356e7baca95cd2e5bd23f9a61f`
  - `SHA256SUMS`
The zip was downloaded back, checked with `sha256sum -c SHA256SUMS` (both OK) and is the package slice195 pins.
The tag's CI run was cancelled before publishing (the tag trigger was removed afterwards; the workflow is manual only).

### gt4gs-v0.4.0: JIT-free (task 042, 2026-10-16)

Branch `gt4gs/nojit` (from `gt4gs/main` `660017bb1`), commit `4c8b63a6e`, built with `GT4GS_ONLY=ON` by
`gt4gs/release-local.ps1` (MSVC cl 19.44.35222, CMake 4.3.1, Ninja 1.13.2, no LTO, deps of `private/slice193-gt4gs-deps`;
`gt4gs/check-nojit.ps1` passed on the package before zipping). Release:
https://github.com/moesuito/gt4recomp-gs/releases/tag/gt4gs-v0.4.0 (prerelease):
  - `gt4-gs-0.4.0-windows-x64.zip`, 13,008,479 bytes, SHA-256 `5c92ad003cf26237c3e859a82da6b400132a318479935b60a1ece5382fe1f44d`
  - `gt4recomp-gs-0.4.0-source.zip`, SHA-256 `95f6a96e3106006a7f694f9ab4b95cb9ce0ad7da34e0bd9f0764fdecc4eed5a0`
  - `SHA256SUMS`
Downloaded back, `sha256sum -c SHA256SUMS` OK, byte-identical to the local zip; extracted to
`private/slice197-gt4gs-release-pkg` (140 files, tree sha256 `e9402cf2...`, `gt4-gs.exe` 8,928,256 bytes, sha256
`1c4c45da...`), the package slice197 pins. No FFmpeg or x264 DLLs (not needed: gt4-gs.exe does not import them; checked
by running every replay with the package as shipped). What `GT4GS_ONLY` removes and how it is enforced: [GS-LIVE](GS-LIVE.md)
section 9.
