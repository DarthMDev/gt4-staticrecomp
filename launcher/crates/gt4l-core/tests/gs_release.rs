//! TASK-043 tests: the renderer download against a fake release folder (no network).
//!
//! The fake server is `FolderSource`: `releases.json` in GitHub's list format plus the asset files, served by
//! the URL prefix https://github.com/moesuito/gt4recomp-gs/releases/download/<tag>/<name>.

use std::fs;
use std::io::{Cursor, Write};
use std::path::{Path, PathBuf};

use gt4l_core::gs_release::{self, FolderSource, Outcome, Version};
use gt4l_core::releases::{self as releases, Stream};

const BASE: &str = "https://github.com/moesuito/gt4recomp-gs/releases/download";

/// A fresh folder under the system temp directory, unique per test.
fn scratch(name: &str) -> PathBuf {
    let directory = std::env::temp_dir().join(format!("gt4l-gs-test-{}-{name}", std::process::id()));
    let _ = fs::remove_dir_all(&directory);
    fs::create_dir_all(&directory).unwrap();
    directory
}

/// A zip package holding `gt4-gs.exe` with the given bytes.
fn package(executable: &[u8]) -> Vec<u8> {
    let mut writer = zip::ZipWriter::new(Cursor::new(Vec::new()));
    writer.start_file("gt4-gs.exe", zip::write::SimpleFileOptions::default()).unwrap();
    writer.write_all(executable).unwrap();
    writer.finish().unwrap().into_inner()
}

/// Adds one release to the fake server: its tag folder with the package and SHA256SUMS.
/// `sums_exe` is the hash written for gt4-gs.exe; `package_bytes` is what the server serves for the package.
struct FakeRelease<'a> {
    tag: &'a str,
    prerelease: bool,
    executable: &'a [u8],
    serve: &'a [u8],
}

fn write_release(root: &Path, release: &FakeRelease<'_>) -> String {
    let version = release.tag.trim_start_matches("gt4gs-v");
    let package_name = format!("gt4-gs-{version}-windows-x64.zip");
    let folder = root.join(release.tag);
    fs::create_dir_all(&folder).unwrap();
    let package_bytes = package(release.executable);
    fs::write(folder.join(&package_name), release.serve).unwrap();
    let sums = format!(
        "{}  {}\n{}  gt4-gs.exe\n",
        gs_release::sha256_hex(&package_bytes),
        package_name,
        gs_release::sha256_hex(release.executable),
    );
    fs::write(folder.join("SHA256SUMS"), sums).unwrap();
    format!(
        r#"{{"tag_name":"{}","prerelease":{},"draft":false,"assets":[{{"name":"{}","browser_download_url":"{}/{}/{}"}},{{"name":"SHA256SUMS","browser_download_url":"{}/{}/SHA256SUMS"}}]}}"#,
        release.tag, release.prerelease, package_name, BASE, release.tag, package_name, BASE, release.tag,
    )
}

fn write_list(root: &Path, entries: &[String]) {
    fs::write(root.join("releases.json"), format!("[{}]", entries.join(","))).unwrap();
}

fn source(root: &Path) -> FolderSource {
    FolderSource { root: root.to_path_buf(), repository: gs_release::RELEASE_REPOSITORY.to_string() }
}

#[test]
fn stable_release_is_verified_and_installed_with_its_hash_recorded() {
    let server = scratch("install-server");
    let install = scratch("install-root");
    let exe = b"fake gt4-gs 0.3.2".to_vec();
    let entry = write_release(&server, &FakeRelease { tag: "gt4gs-v1.0.1", prerelease: false, executable: &exe, serve: &package(&exe) });
    write_list(&server, &[entry]);

    let outcome = gs_release::install_latest(&source(&server), &install).expect("install");
    let Outcome::Installed(installed) = outcome else { panic!("expected a fresh install") };
    assert_eq!(installed.version, Version(1, 0, 1));
    assert_eq!(installed.executable, install.join("1.0.1").join("gt4-gs.exe"));
    assert_eq!(installed.executable_sha256, gs_release::sha256_hex(&exe));
    assert_eq!(fs::read(&installed.executable).unwrap(), exe);
    assert_eq!(gs_release::installed(&install), Some(installed.clone()));
    assert_eq!(fs::read_to_string(install.join("current.txt")).unwrap().trim(), "1.0.1");

    // Asking again with the same release is an up-to-date result, not a second install.
    assert!(matches!(gs_release::install_latest(&source(&server), &install), Ok(Outcome::UpToDate(_))));
}

#[test]
fn prereleases_and_too_old_versions_are_ignored() {
    let server = scratch("prerelease-server");
    let install = scratch("prerelease-root");
    let stable = b"stable 0.3.2".to_vec();
    let newer_prerelease = b"prerelease 0.4.0".to_vec();
    let old = b"old 0.3.1".to_vec();
    let entries = vec![
        write_release(&server, &FakeRelease { tag: "gt4gs-v1.1.0", prerelease: true, executable: &newer_prerelease, serve: &package(&newer_prerelease) }),
        write_release(&server, &FakeRelease { tag: "gt4gs-v1.0.1", prerelease: false, executable: &stable, serve: &package(&stable) }),
        write_release(&server, &FakeRelease { tag: "gt4gs-v1.0.0", prerelease: false, executable: &old, serve: &package(&old) }),
    ];
    write_list(&server, &entries);

    let releases = gs_release::parse_releases(&fs::read_to_string(server.join("releases.json")).unwrap(), Stream::Renderer).unwrap();
    assert_eq!(releases::select_latest(&releases, gs_release::MINIMUM_VERSION).unwrap().tag, "gt4gs-v1.0.1", "1.1.0 is a prerelease");

    let Outcome::Installed(installed) = gs_release::install_latest(&source(&server), &install).unwrap() else {
        panic!("expected install")
    };
    assert_eq!(installed.version, Version(1, 0, 1));

    // Only a prerelease and an old release on the server: nothing to install, the error names the minimum.
    let only_old = scratch("only-old-server");
    write_list(&only_old, &[write_release(&only_old, &FakeRelease { tag: "gt4gs-v1.0.0", prerelease: false, executable: &old, serve: &package(&old) })]);
    let error = gs_release::install_latest(&source(&only_old), &install).unwrap_err();
    assert!(error.contains("1.0.1"), "{error}");
}

#[test]
fn upgrade_keeps_the_previous_version_and_removes_older_ones() {
    let server = scratch("upgrade-server");
    let install = scratch("upgrade-root");
    let first = b"exe 0.3.2".to_vec();
    let second = b"exe 0.3.3".to_vec();
    let third = b"exe 0.3.4".to_vec();

    let entry = |tag: &str, bytes: &[u8]| write_release(&server, &FakeRelease { tag, prerelease: false, executable: bytes, serve: &package(bytes) });
    write_list(&server, &[entry("gt4gs-v1.0.1", &first)]);
    gs_release::install_latest(&source(&server), &install).unwrap();

    write_list(&server, &[entry("gt4gs-v1.0.1", &first), entry("gt4gs-v1.0.2", &second)]);
    let Outcome::Installed(upgraded) = gs_release::install_latest(&source(&server), &install).unwrap() else {
        panic!("expected upgrade")
    };
    assert_eq!(upgraded.version, Version(1, 0, 2));
    assert!(install.join("1.0.1").is_dir(), "the previous version stays until the next upgrade");
    assert!(install.join("1.0.2").is_dir());

    write_list(&server, &[entry("gt4gs-v1.0.3", &third)]);
    gs_release::install_latest(&source(&server), &install).unwrap();
    assert!(install.join("1.0.3").is_dir());
    assert!(install.join("1.0.2").is_dir(), "the new previous version is kept");
    assert!(!install.join("1.0.1").exists(), "two versions back is removed");
}

#[test]
fn corrupt_download_is_refused_and_the_installed_version_is_kept() {
    let server = scratch("corrupt-server");
    let install = scratch("corrupt-root");
    let good = b"exe good".to_vec();
    write_list(&server, &[write_release(&server, &FakeRelease { tag: "gt4gs-v1.0.1", prerelease: false, executable: &good, serve: &package(&good) })]);
    gs_release::install_latest(&source(&server), &install).unwrap();

    // The next release is served with a damaged package: its sum no longer matches.
    let bad = b"exe bad".to_vec();
    let mut damaged = package(&bad);
    let last = damaged.len() - 1;
    damaged[last] ^= 0xFF;
    let next = write_release(&server, &FakeRelease { tag: "gt4gs-v1.0.2", prerelease: false, executable: &bad, serve: &damaged });
    // The package was written from `damaged`, but the sums list the original package bytes.
    let original_sum = gs_release::sha256_hex(&package(&bad));
    let sums = format!("{original_sum}  gt4-gs-1.0.2-windows-x64.zip\n");
    fs::write(server.join("gt4gs-v1.0.2").join("SHA256SUMS"), sums).unwrap();
    write_list(&server, &[next]);

    let error = gs_release::install_latest(&source(&server), &install).unwrap_err();
    assert!(error.contains("SHA-256"), "{error}");
    assert_eq!(gs_release::installed(&install).unwrap().version, Version(1, 0, 1), "the old version still works");
    assert!(!install.join("1.0.2").exists());
    assert!(!install.join(".staging-0.3.3").exists(), "the staging folder is removed");
}

#[test]
fn package_without_the_program_is_refused_and_nothing_changes() {
    let server = scratch("noexe-server");
    let install = scratch("noexe-root");
    let mut writer = zip::ZipWriter::new(Cursor::new(Vec::new()));
    writer.start_file("readme.txt", zip::write::SimpleFileOptions::default()).unwrap();
    writer.write_all(b"no program here").unwrap();
    let archive = writer.finish().unwrap().into_inner();
    write_list(&server, &[write_release(&server, &FakeRelease { tag: "gt4gs-v1.0.1", prerelease: false, executable: b"", serve: &archive })]);
    assert!(gs_release::install_latest(&source(&server), &install).is_err());
    assert!(gs_release::installed(&install).is_none());
    assert!(!install.join("1.0.1").exists());
}

#[test]
fn a_damaged_installed_program_fails_the_check_so_the_launcher_falls_back() {
    let server = scratch("damaged-installed-server");
    let install = scratch("damaged-installed-root");
    let exe = b"exe installed".to_vec();
    write_list(&server, &[write_release(&server, &FakeRelease { tag: "gt4gs-v1.0.1", prerelease: false, executable: &exe, serve: &package(&exe) })]);
    gs_release::install_latest(&source(&server), &install).unwrap();
    fs::write(install.join("1.0.1").join("gt4-gs.exe"), b"changed").unwrap();
    assert!(gs_release::installed(&install).is_none());
}

#[test]
fn a_release_url_outside_the_fake_server_is_refused() {
    let server = scratch("url-server");
    let error = gs_release::ReleaseSource::download(&source(&server), "https://example.invalid/x").unwrap_err();
    assert!(error.contains("not a release URL"), "{error}");
}
