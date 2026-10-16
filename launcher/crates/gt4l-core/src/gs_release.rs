//! TASK-043: download, verify and install the PCSX2 GS renderer (`gt4-gs.exe`) from GitHub Releases.
//!
//! The launcher only downloads a package and starts `gt4-gs.exe` in its own process. No GPL code is in the
//! launcher (decision 0070). Flow of [`install_latest`]:
//! 1. read the release list (`ReleaseSource::releases_json`); skip drafts and prereleases;
//! 2. pick the highest stable version that is at least [`MINIMUM_VERSION`];
//! 3. download `SHA256SUMS` and the package `gt4-gs-<version>-win64.zip`; the package must match its sum;
//! 4. unpack into a staging folder; `gt4-gs.exe` must exist and must match its line in `SHA256SUMS`;
//! 5. rename the staging folder to `<gs root>\<version>\`, then point `current.txt` at it.
//!
//! The previous version stays in place until the new one has passed step 4 and step 5. Any failure leaves the
//! installed version as it was; the caller then starts CoreGS 1.1 with a message (see `renderer_choice` in
//! `launch.rs`).

use std::collections::BTreeMap;
use std::fs;
use std::io::{Cursor, Read, Write};
use std::path::{Path, PathBuf};

use serde::Deserialize;
use sha2::{Digest, Sha256};

/// GitHub repository that publishes the renderer.
pub const RELEASE_REPOSITORY: &str = "moesuito/gt4recomp-gs";
/// Lowest stable version the launcher accepts (the version pinned for the owner's testing: gt4-gs 0.3.2).
pub const MINIMUM_VERSION: Version = Version(0, 3, 2);
/// Name of the checksum list attached to each release.
pub const CHECKSUM_ASSET: &str = "SHA256SUMS";
/// The renderer program inside the package.
pub const EXECUTABLE_NAME: &str = "gt4-gs.exe";
/// Name of the state file in the renderer root: the version in use.
pub const CURRENT_FILE: &str = "current.txt";
/// Largest package the launcher accepts (bytes). Protects memory on a bad or hostile response.
pub const MAX_PACKAGE_BYTES: u64 = 256 * 1024 * 1024;

/// A semantic version without pre-release tags: major, minor, patch.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Version(pub u32, pub u32, pub u32);

impl Version {
    /// Parses `1.2.3` or `v1.2.3`. Anything else (pre-release tags, missing parts) is refused.
    pub fn parse(text: &str) -> Option<Version> {
        let body = text.strip_prefix('v').unwrap_or(text);
        let mut parts = body.split('.');
        let major = parts.next()?.parse().ok()?;
        let minor = parts.next()?.parse().ok()?;
        let patch = parts.next()?.parse().ok()?;
        if parts.next().is_some() {
            return None;
        }
        Some(Version(major, minor, patch))
    }

    pub fn text(&self) -> String {
        format!("{}.{}.{}", self.0, self.1, self.2)
    }
}

/// One downloadable file of a release.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Asset {
    pub name: String,
    pub url: String,
}

/// One release as the launcher reads it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Release {
    pub tag: String,
    pub version: Option<Version>,
    pub prerelease: bool,
    pub draft: bool,
    pub assets: Vec<Asset>,
}

#[derive(Deserialize)]
struct ApiRelease {
    tag_name: String,
    #[serde(default)]
    prerelease: bool,
    #[serde(default)]
    draft: bool,
    #[serde(default)]
    assets: Vec<ApiAsset>,
}

#[derive(Deserialize)]
struct ApiAsset {
    name: String,
    browser_download_url: String,
}

/// Where release data comes from. GitHub in the release build; a folder in tests.
pub trait ReleaseSource {
    /// The JSON array of the release list (GitHub REST `GET /repos/{repo}/releases` format).
    fn releases_json(&self) -> Result<String, String>;
    /// The bytes behind a download URL taken from a release asset.
    fn download(&self, url: &str) -> Result<Vec<u8>, String>;
}

/// Reads the release list JSON.
pub fn parse_releases(json: &str) -> Result<Vec<Release>, String> {
    let raw: Vec<ApiRelease> = serde_json::from_str(json).map_err(|error| format!("release list: {error}"))?;
    Ok(raw
        .into_iter()
        .map(|release| Release {
            version: Version::parse(&release.tag_name),
            tag: release.tag_name,
            prerelease: release.prerelease,
            draft: release.draft,
            assets: release
                .assets
                .into_iter()
                .map(|asset| Asset { name: asset.name, url: asset.browser_download_url })
                .collect(),
        })
        .collect())
}

/// The newest stable release at or above the minimum. Drafts and prereleases are never chosen.
pub fn select_release(releases: &[Release]) -> Option<&Release> {
    releases
        .iter()
        .filter(|release| !release.draft && !release.prerelease)
        .filter_map(|release| release.version.map(|version| (version, release)))
        .filter(|(version, _)| *version >= MINIMUM_VERSION)
        .max_by_key(|(version, _)| *version)
        .map(|(_, release)| release)
}

/// The package file name of a version.
pub fn package_name(version: Version) -> String {
    format!("gt4-gs-{}-win64.zip", version.text())
}

/// Parses `SHA256SUMS` text (`<hex>  <name>` or `<hex> *<name>` per line) into name -> lowercase hex.
pub fn parse_checksums(text: &str) -> BTreeMap<String, String> {
    let mut sums = BTreeMap::new();
    for line in text.lines() {
        let line = line.trim_end();
        let Some((hex, rest)) = line.split_once(' ') else { continue };
        let name = rest.trim_start_matches([' ', '*']);
        if hex.len() == 64 && hex.chars().all(|c| c.is_ascii_hexdigit()) && !name.is_empty() {
            sums.insert(name.to_string(), hex.to_ascii_lowercase());
        }
    }
    sums
}

/// Lowercase hex SHA-256 of bytes.
pub fn sha256_hex(bytes: &[u8]) -> String {
    let digest = Sha256::digest(bytes);
    digest.iter().map(|byte| format!("{byte:02x}")).collect()
}

/// What an installed renderer looks like to the launcher.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Installed {
    pub version: Version,
    pub executable: PathBuf,
    /// SHA-256 of `gt4-gs.exe`, passed to the game as `GT4_GS_PROCESS_SHA256`.
    pub executable_sha256: String,
}

/// Result of a successful install or update.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Outcome {
    /// The version was installed (or reinstalled after a damaged folder).
    Installed(Installed),
    /// The newest stable version is already in use and passes the check.
    UpToDate(Installed),
}

/// The folder of one version under the renderer root.
pub fn version_directory(root: &Path, version: Version) -> PathBuf {
    root.join(version.text())
}

/// The installed version named by `current.txt`, if its folder passes the check. Any problem gives `None`.
pub fn installed(root: &Path) -> Option<Installed> {
    let text = fs::read_to_string(root.join(CURRENT_FILE)).ok()?;
    let version = Version::parse(text.trim())?;
    check_installed(&version_directory(root, version), version).ok()
}

/// The check of an installed folder: the program exists and its hash is the one recorded at install time.
fn check_installed(directory: &Path, version: Version) -> Result<Installed, String> {
    let executable = directory.join(EXECUTABLE_NAME);
    let bytes = fs::read(&executable).map_err(|error| format!("{}: {error}", executable.display()))?;
    let recorded = fs::read_to_string(directory.join("sha256.txt"))
        .map_err(|error| format!("{}: {error}", directory.display()))?;
    let actual = sha256_hex(&bytes);
    if recorded.trim() != actual {
        return Err(format!("{EXECUTABLE_NAME} does not match its recorded hash"));
    }
    Ok(Installed { version, executable, executable_sha256: actual })
}

/// Downloads, verifies and installs the newest stable release (see the module comment).
pub fn install_latest(source: &dyn ReleaseSource, root: &Path) -> Result<Outcome, String> {
    let releases = parse_releases(&source.releases_json()?)?;
    let release = select_release(&releases).ok_or_else(|| {
        format!("no stable release of {RELEASE_REPOSITORY} at or above {}", MINIMUM_VERSION.text())
    })?;
    let version = release.version.expect("select_release only returns versions");
    let asset = |name: &str| {
        release
            .assets
            .iter()
            .find(|asset| asset.name == name)
            .cloned()
            .ok_or_else(|| format!("release {} has no {name}", release.tag))
    };
    let sums_text = source.download(&asset(CHECKSUM_ASSET)?.url)?;
    let sums = parse_checksums(&String::from_utf8_lossy(&sums_text));
    let package = package_name(version);
    let expected = sums.get(&package).ok_or_else(|| format!("{CHECKSUM_ASSET} has no line for {package}"))?;

    if let Some(current) = installed(root) {
        if current.version == version {
            return Ok(Outcome::UpToDate(current));
        }
    }

    let package_bytes = source.download(&asset(&package)?.url)?;
    if package_bytes.len() as u64 > MAX_PACKAGE_BYTES {
        return Err(format!("{package} is larger than the limit"));
    }
    if sha256_hex(&package_bytes) != *expected {
        return Err(format!("{package} does not match its SHA-256 (corrupt or changed download)"));
    }

    fs::create_dir_all(root).map_err(|error| format!("{}: {error}", root.display()))?;
    let staging = root.join(format!(".staging-{}", version.text()));
    let _ = fs::remove_dir_all(&staging);
    let result = stage_and_publish(&package_bytes, &staging, root, version, &sums);
    if result.is_err() {
        let _ = fs::remove_dir_all(&staging);
    }
    result
}

fn stage_and_publish(
    package: &[u8],
    staging: &Path,
    root: &Path,
    version: Version,
    sums: &BTreeMap<String, String>,
) -> Result<Outcome, String> {
    unpack_zip(package, staging)?;
    let executable = staging.join(EXECUTABLE_NAME);
    let bytes = fs::read(&executable).map_err(|_| format!("the package has no {EXECUTABLE_NAME}"))?;
    let actual = sha256_hex(&bytes);
    if let Some(expected) = sums.get(EXECUTABLE_NAME) {
        if *expected != actual {
            return Err(format!("{EXECUTABLE_NAME} does not match {CHECKSUM_ASSET}"));
        }
    }
    fs::write(staging.join("sha256.txt"), format!("{actual}\n")).map_err(|error| error.to_string())?;

    let target = version_directory(root, version);
    if target.exists() {
        fs::remove_dir_all(&target).map_err(|error| format!("{}: {error}", target.display()))?;
    }
    fs::rename(staging, &target).map_err(|error| format!("{}: {error}", target.display()))?;
    let installed = check_installed(&target, version)?;

    let previous = installed_version(root);
    write_atomic(&root.join(CURRENT_FILE), &format!("{}\n", version.text()))?;
    remove_other_versions(root, &[Some(version), previous])?;
    Ok(Outcome::Installed(installed))
}

/// The version named by `current.txt`, whether or not its folder passes the check.
fn installed_version(root: &Path) -> Option<Version> {
    let text = fs::read_to_string(root.join(CURRENT_FILE)).ok()?;
    Version::parse(text.trim())
}

/// Removes version folders other than the ones kept (the new version and the previous one).
fn remove_other_versions(root: &Path, keep: &[Option<Version>]) -> Result<(), String> {
    let entries = fs::read_dir(root).map_err(|error| error.to_string())?;
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().to_string();
        let Some(version) = Version::parse(&name) else { continue };
        if !keep.contains(&Some(version)) && entry.path().is_dir() {
            let _ = fs::remove_dir_all(entry.path());
        }
    }
    Ok(())
}

fn write_atomic(path: &Path, text: &str) -> Result<(), String> {
    let temporary = path.with_extension("tmp");
    {
        let mut file = fs::File::create(&temporary).map_err(|error| error.to_string())?;
        file.write_all(text.as_bytes()).map_err(|error| error.to_string())?;
        file.sync_all().map_err(|error| error.to_string())?;
    }
    fs::rename(&temporary, path).map_err(|error| format!("{}: {error}", path.display()))
}

/// Unpacks a zip into `destination`. Entries that would leave the folder are refused.
fn unpack_zip(bytes: &[u8], destination: &Path) -> Result<(), String> {
    let mut archive = zip::ZipArchive::new(Cursor::new(bytes)).map_err(|error| format!("package is not a zip: {error}"))?;
    fs::create_dir_all(destination).map_err(|error| error.to_string())?;
    for index in 0..archive.len() {
        let mut entry = archive.by_index(index).map_err(|error| error.to_string())?;
        let relative = entry.enclosed_name().ok_or_else(|| format!("unsafe path in package: {}", entry.name()))?;
        let target = destination.join(relative);
        if entry.is_dir() {
            fs::create_dir_all(&target).map_err(|error| error.to_string())?;
            continue;
        }
        if let Some(parent) = target.parent() {
            fs::create_dir_all(parent).map_err(|error| error.to_string())?;
        }
        let mut data = Vec::new();
        entry.by_ref().take(MAX_PACKAGE_BYTES).read_to_end(&mut data).map_err(|error| error.to_string())?;
        fs::write(&target, data).map_err(|error| error.to_string())?;
    }
    Ok(())
}

/// A release source that reads a folder: `releases.json` plus the assets. Download URLs of the form
/// `https://github.com/<repository>/releases/download/<tag>/<name>` map to `<root>/<tag>/<name>`. Used by tests
/// and for an offline copy of the release.
pub struct FolderSource {
    pub root: PathBuf,
}

impl ReleaseSource for FolderSource {
    fn releases_json(&self) -> Result<String, String> {
        fs::read_to_string(self.root.join("releases.json")).map_err(|error| error.to_string())
    }

    fn download(&self, url: &str) -> Result<Vec<u8>, String> {
        let prefix = format!("https://github.com/{RELEASE_REPOSITORY}/releases/download/");
        let relative = url.strip_prefix(&prefix).ok_or_else(|| format!("not a release URL: {url}"))?;
        if relative.contains("..") {
            return Err(format!("refused URL: {url}"));
        }
        fs::read(self.root.join(relative)).map_err(|error| format!("download failed: {error}"))
    }
}

/// The release list and downloads from GitHub.
pub struct GithubSource;

impl ReleaseSource for GithubSource {
    fn releases_json(&self) -> Result<String, String> {
        let url = format!("https://api.github.com/repos/{RELEASE_REPOSITORY}/releases");
        let mut response = ureq::get(&url)
            .header("User-Agent", "GT4Recomp-launcher")
            .header("Accept", "application/vnd.github+json")
            .call()
            .map_err(|error| format!("release list: {error}"))?;
        response.body_mut().read_to_string().map_err(|error| format!("release list: {error}"))
    }

    fn download(&self, url: &str) -> Result<Vec<u8>, String> {
        let mut response = ureq::get(url)
            .header("User-Agent", "GT4Recomp-launcher")
            .call()
            .map_err(|error| format!("download: {error}"))?;
        let mut bytes = Vec::new();
        response
            .body_mut()
            .with_config()
            .limit(MAX_PACKAGE_BYTES)
            .reader()
            .read_to_end(&mut bytes)
            .map_err(|error| format!("download: {error}"))?;
        Ok(bytes)
    }
}

/// The renderer root: `%LOCALAPPDATA%\GT4Recomp\gs` (the caller passes the local data folder).
pub fn renderer_root(local_data: &Path) -> PathBuf {
    local_data.join("GT4Recomp").join("gs")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn versions_parse_with_and_without_v_and_refuse_suffixes() {
        assert_eq!(Version::parse("v0.3.2"), Some(Version(0, 3, 2)));
        assert_eq!(Version::parse("1.0.0"), Some(Version(1, 0, 0)));
        assert_eq!(Version::parse("0.3.2-rc1"), None);
        assert_eq!(Version::parse("0.3"), None);
        assert!(Version(0, 10, 0) > Version(0, 9, 9));
    }

    #[test]
    fn checksum_lines_parse_both_common_forms() {
        let text = format!("{}  a.zip\n{} *b.exe\nnot a line\n", "A".repeat(64), "b".repeat(64));
        let sums = parse_checksums(&text);
        assert_eq!(sums.get("a.zip").map(String::as_str), Some("a".repeat(64).as_str()));
        assert_eq!(sums.get("b.exe").map(String::as_str), Some("b".repeat(64).as_str()));
        assert_eq!(sums.len(), 2);
    }

    #[test]
    fn sha256_matches_the_known_empty_digest() {
        assert_eq!(sha256_hex(b""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    }
}
