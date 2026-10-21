# Releases and the launcher's update rules

Status: launcher TASK-019 / TASK-043, 2026-10-20. Code: `launcher/crates/gt4l-core/src/{version,releases,update,versions,gs_release}.rs`.

## Two release streams

| Stream | What it is | Repository | Tag | Assets |
|---|---|---|---|---|
| **GT4Recomp** | launcher, game executable and installer | `moesuito/gt4-staticrecomp` | `vX.Y.Z` | `GT4Recomp-Setup-X.Y.Z.exe`, `SHA256SUMS` |
| **gt4-gs** | the PCSX2 GS renderer (`gt4-gs.exe`) | `moesuito/gt4recomp-gs` | `gt4gs-vX.Y.Z` | `gt4-gs-X.Y.Z-windows-x64.zip`, `gt4recomp-gs-X.Y.Z-source.zip`, `SHA256SUMS` |

- Both streams use SemVer `MAJOR.MINOR.PATCH`. Comparison is numeric: `1.10.0` is newer than `1.9.0`.
- Current versions: GT4Recomp **1.0.0**, gt4-gs **1.0.1** (`gh release view gt4gs-v1.0.1 --repo moesuito/gt4recomp-gs --json assets` lists the real asset names above).
- The launcher's minimum for gt4-gs is **1.0.1**; for GT4Recomp it is **1.0.0** (`releases.rs`).
- A tag that does not match the stream's format is ignored. A renderer tag in the launcher repository (or the reverse) is never offered.

## The rule: stable only

- **Drafts** and **prereleases** are never offered, installed or checked. The launcher reads `draft` and `prerelease` from the GitHub release list.
- The newest stable release at or above the minimum is the candidate. Nothing else is.
- A release marked prerelease on GitHub stays out of the launcher even when its version is higher.

## Release notes

- The release **body** is the release notes. The update popup shows it under "What's new".
- The launcher shows the body as text. Supported: `#` headings, `-` or `*` bullets, `**bold**`. Any HTML in the body is shown as plain text.
- Notes are cut at 6000 characters.

## Checksums

- Every release has a `SHA256SUMS` asset: one line per file, `<sha256>  <file name>`.
- The launcher downloads the package and checks its SHA-256 against its line before anything is unpacked or started.
- For gt4-gs, `gt4-gs.exe` inside the zip must also match its own line.
- A missing line, a mismatch or a missing file is refused and the installed version stays as it was.

## How the launcher uses the streams

- **On every launch** (background, timeout 6 s, never blocks Play): the launcher reads both release lists. A failure is logged only.
- **gt4-gs not installed**: the launcher installs the newest stable one (setup does it too). No prompt. If the download fails, the launcher uses CoreGS 1.1 and says so.
- **An update exists** (newer stable, not skipped): the popup shows the current and new version and the notes. Buttons: **Update**, **Not now**, **Skip this version** (remembered in `settings.toml` under `[updates]`, for exactly that version).
- **Updates are never automatic.** The launcher installs only when the player presses Update.
- **gt4-gs update**: only while the game is not running. The new version goes into its own folder (`%LOCALAPPDATA%\GT4Recomp\gs\<version>\`), `current.txt` switches to it, the previous folder is kept for rollback, and `version.json` records the version.
- **GT4Recomp update**: the setup program is downloaded to a temporary folder, its SHA-256 is checked, it is started, and the launcher exits. The installer upgrades the program files in place. It never touches the data folder.
- **Check for updates** (Settings > About) runs the same check. With nothing new it says "You're up to date."

## version.json

- Location: next to `GT4Recomp.exe` (the install folder). If that folder is not writable, the file is kept in the data folder and the launcher logs it.
- Written by the installer and updated by the launcher. Written atomically (temporary file, then rename).

```json
{
  "schema": 1,
  "gt4recomp": "1.0.0",
  "gt4gs": "1.0.1",
  "settings_schema": 1
}
```

- `gt4recomp` is the GT4Recomp version. The launcher's own Cargo version is authoritative when the file disagrees (a warning goes to the log).
- `gt4gs` is `null` until the renderer is installed.
- `settings_schema` is the schema of `settings.toml` that this launcher writes.
- A damaged or newer `version.json` is not rewritten; a damaged one is kept as `version.json.bad` and replaced with a fresh one.

## Publishing a release (checklist)

1. Bump the version in `launcher/Cargo.toml` (workspace), `launcher/crates/gt4l-tauri/tauri.conf.json` and the installer define in the release command.
2. Build the launcher and the installer (`installer/gt4recomp.nsi`, `makensis /DVERSION=X.Y.Z ...`).
3. Hash the setup program and write `SHA256SUMS`.
4. Create the tag `vX.Y.Z` (GT4Recomp) or `gt4gs-vX.Y.Z` (gt4-gs) and attach the assets with their exact names above.
5. Write the release notes into the release body. Publish as a stable release (not draft, not prerelease).
6. The owner runs the live test (the update popup and the install) before the release is announced.

Nothing in this document creates a release. The owner publishes releases.

## What the tests cover

- SemVer parsing and numeric comparison, and refusal of pre-release tags (`version.rs`).
- Release lists of both repositories from fixtures with the real asset names, drafts and prereleases (`tests/release_lists.rs`).
- The update decision: newer stable, same version, prerelease, skipped version, first install, minimum (`update.rs`).
- `version.json` round trip, the installer form, the unwritable-folder fallback and the Cargo version rule (`versions.rs`).
- The data-path guard: installs and updates never write to saves, cards, library, replays or the runtime card folder, nor to card or save file types (`guard.rs`).
- The gt4-gs install against a fake folder source: verified install, corrupt download refused, prereleases ignored, upgrade keeps one previous version (`tests/gs_release.rs`).
- Migrations: the chain, backups, newer and corrupt files (`migrate.rs`, `tests/config_migration.rs`).
- The tray state machine (`gt4l-tauri/src/tray.rs`).

No test touches the network. The GitHub source is replaced by `FolderSource` (fixtures), and the live check is the owner's test.
