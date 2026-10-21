//! `version.json`: the installed versions the launcher and the installer agree on.
//!
//! The installer writes it next to `GT4Recomp.exe` (the install folder). The launcher updates the `gt4gs` field
//! after a renderer install or update. The launcher's own version comes from its Cargo package, which is
//! authoritative when the file disagrees (a warning goes to the log). Writes are atomic (temporary file plus
//! rename). When the install folder is not writable, the file is kept in the data folder instead (see
//! [`location`]).

use std::fs;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

use crate::guard;
use crate::gs_release;
use crate::migrate::{self, Verdict};
use crate::version::Version;

/// File name next to the launcher (or in the data folder when the install folder is read-only).
pub const VERSION_FILE_NAME: &str = "version.json";

/// The document as written to disk.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct VersionFile {
    /// Schema of this file (`migrate::VERSION_FILE`).
    pub schema: u32,
    /// GT4Recomp (launcher, game and installer) version.
    pub gt4recomp: String,
    /// gt4-gs version in use, `null` until the renderer is installed.
    #[serde(default)]
    pub gt4gs: Option<String>,
    /// Schema of `settings.toml` the installed launcher writes.
    pub settings_schema: u32,
}

impl VersionFile {
    /// The file the installer writes for a fresh install: no renderer yet.
    pub fn for_install(gt4recomp: &str, settings_schema: u32) -> VersionFile {
        VersionFile {
            schema: migrate::VERSION_FILE.current,
            gt4recomp: gt4recomp.to_string(),
            gt4gs: None,
            settings_schema,
        }
    }
}

/// Where the file of this launcher lives: the install folder when writable, else the data folder.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Location {
    pub path: PathBuf,
    /// Set when the data folder is used; the caller logs it.
    pub notice: Option<String>,
}

/// Chooses the location. The install folder is probed with a temporary file; a failed probe means read-only.
pub fn location(install_dir: &Path, data_dir: &Path) -> Location {
    let probe = install_dir.join(".version-probe");
    let writable = fs::write(&probe, b"probe").is_ok();
    let _ = fs::remove_file(&probe);
    if writable {
        return Location { path: install_dir.join(VERSION_FILE_NAME), notice: None };
    }
    Location {
        path: data_dir.join(VERSION_FILE_NAME),
        notice: Some(format!(
            "install folder {} is not writable; version.json kept in {}",
            install_dir.display(),
            data_dir.display()
        )),
    }
}

/// Parses the text and runs the migration chain on it. A newer schema or a damaged file gives `Err`; the caller
/// then uses the Cargo version and logs the reason. The text itself is never rewritten by this function.
pub fn parse(text: &str) -> Result<VersionFile, String> {
    let mut document: serde_json::Value = serde_json::from_str(text).map_err(|error| format!("version.json: {error}"))?;
    match migrate::plan(&migrate::VERSION_FILE, &document) {
        Verdict::Newer { found } => return Err(format!("version.json is schema {found}, newer than this launcher")),
        Verdict::Corrupt(message) => return Err(message),
        _ => {}
    }
    migrate::migrate(&migrate::VERSION_FILE, &mut document)?;
    serde_json::from_value(document).map_err(|error| format!("version.json: {error}"))
}

/// The text written to disk (pretty JSON, one trailing newline).
pub fn render(file: &VersionFile) -> String {
    let mut text = serde_json::to_string_pretty(file).expect("a version file always serializes");
    text.push('\n');
    text
}

/// The launcher version to show: the Cargo version wins. Returns a warning when the file disagrees.
pub fn reconcile_launcher_version(file: Option<&VersionFile>, cargo_version: &str) -> (String, Option<String>) {
    match file {
        Some(file) if file.gt4recomp != cargo_version => (
            cargo_version.to_string(),
            Some(format!("version.json says GT4Recomp {}, the program is {cargo_version}; using {cargo_version}", file.gt4recomp)),
        ),
        _ => (cargo_version.to_string(), None),
    }
}

/// The file brought in line with what is installed, or `None` when it already is. The installer writes its own
/// version and a null renderer on every install or upgrade (for example 1.0.0 -> 1.1.0 over an installed gt4-gs), and
/// an older launcher may have left an older GT4Recomp version: the launcher version is the Cargo version, and the
/// renderer is the checked install (`None` keeps the recorded value, so a damaged folder does not erase it).
pub fn reconciled(file: &VersionFile, cargo_version: &str, renderer: Option<Version>) -> Option<VersionFile> {
    let mut next = file.clone();
    next.gt4recomp = cargo_version.to_string();
    if let Some(renderer) = renderer {
        next.gt4gs = Some(renderer.text());
    }
    (next != *file).then_some(next)
}

/// Reads the file at the location (no migration writes). A missing or damaged file gives `None` and a reason.
pub fn read(path: &Path) -> Result<Option<VersionFile>, String> {
    match fs::read_to_string(path) {
        Ok(text) => parse(&text).map(Some),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(None),
        Err(error) => Err(format!("{}: {error}", path.display())),
    }
}

/// Writes the file atomically. `guard_root` is the folder the data-path guard checks against.
pub fn write(path: &Path, guard_root: &Path, file: &VersionFile) -> Result<(), String> {
    gs_release::write_atomic(guard_root, path, &render(file))
}

/// Sets the renderer version after an install or update. Creates the file when it is missing. The launcher version
/// stays as the file has it (or the Cargo version when the file is missing).
pub fn record_renderer(path: &Path, guard_root: &Path, renderer: Version, cargo_version: &str) -> Result<(), String> {
    guard::check_write(guard_root, path)?;
    let mut file = match read(path) {
        Ok(Some(file)) => file,
        _ => VersionFile::for_install(cargo_version, crate::config::SCHEMA_VERSION),
    };
    file.gt4gs = Some(renderer.text());
    write(path, guard_root, &file)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scratch(name: &str) -> PathBuf {
        let directory = std::env::temp_dir().join(format!("gt4l-version-test-{}-{name}", std::process::id()));
        let _ = fs::remove_dir_all(&directory);
        fs::create_dir_all(&directory).unwrap();
        directory
    }

    #[test]
    fn a_written_file_reads_back_unchanged() {
        let directory = scratch("round-trip");
        let path = directory.join(VERSION_FILE_NAME);
        let file = VersionFile { schema: 1, gt4recomp: "1.0.0".into(), gt4gs: Some("1.0.1".into()), settings_schema: 1 };
        write(&path, &directory, &file).unwrap();
        assert_eq!(read(&path).unwrap(), Some(file));
        assert!(!directory.join("version.json.tmp").exists(), "the temporary file is renamed away");
    }

    #[test]
    fn the_installer_form_has_a_null_renderer_and_the_example_keys() {
        let text = render(&VersionFile::for_install("1.0.0", 1));
        assert!(text.contains("\"gt4recomp\": \"1.0.0\""));
        assert!(text.contains("\"gt4gs\": null"));
        assert!(text.contains("\"settings_schema\": 1"));
        assert_eq!(parse(&text).unwrap(), VersionFile::for_install("1.0.0", 1));
    }

    #[test]
    fn recording_the_renderer_keeps_the_launcher_version() {
        let directory = scratch("record");
        let path = directory.join(VERSION_FILE_NAME);
        write(&path, &directory, &VersionFile::for_install("1.0.0", 1)).unwrap();
        record_renderer(&path, &directory, Version(1, 0, 1), "1.0.0").unwrap();
        let file = read(&path).unwrap().unwrap();
        assert_eq!(file.gt4gs.as_deref(), Some("1.0.1"));
        assert_eq!(file.gt4recomp, "1.0.0");
    }

    #[test]
    fn the_cargo_version_is_authoritative_with_a_warning() {
        let file = VersionFile::for_install("0.9.0", 1);
        let (version, warning) = reconcile_launcher_version(Some(&file), "1.0.0");
        assert_eq!(version, "1.0.0");
        assert!(warning.unwrap().contains("0.9.0"));
        assert!(reconcile_launcher_version(None, "1.0.0").1.is_none());
        assert!(reconcile_launcher_version(Some(&VersionFile::for_install("1.0.0", 1)), "1.0.0").1.is_none());
    }

    #[test]
    fn an_upgrade_to_1_1_0_records_the_new_launcher_and_the_installed_renderer() {
        // As the 1.1.0 installer leaves it over a 1.0.0 install with gt4-gs 1.1.0 already in place.
        let after_installer = VersionFile::for_install("1.1.0", 1);
        let fixed = reconciled(&after_installer, "1.1.0", Some(Version(1, 1, 0))).unwrap();
        assert_eq!(fixed.gt4recomp, "1.1.0");
        assert_eq!(fixed.gt4gs.as_deref(), Some("1.1.0"));
        // A file still naming 1.0.0 (an older install folder) takes the program's version.
        let old = VersionFile { schema: 1, gt4recomp: "1.0.0".into(), gt4gs: Some("1.0.1".into()), settings_schema: 1 };
        let fixed = reconciled(&old, "1.1.0", Some(Version(1, 0, 1))).unwrap();
        assert_eq!((fixed.gt4recomp.as_str(), fixed.gt4gs.as_deref()), ("1.1.0", Some("1.0.1")));
        // Nothing to change, or no checked renderer: the recorded renderer stays.
        assert!(reconciled(&fixed, "1.1.0", Some(Version(1, 0, 1))).is_none());
        assert!(reconciled(&fixed, "1.1.0", None).is_none());
    }

    #[test]
    fn a_newer_or_damaged_file_is_refused_not_rewritten() {
        assert!(parse(r#"{"schema": 7, "gt4recomp": "1.0.0", "settings_schema": 1}"#).is_err());
        assert!(parse("{not json").is_err());
        assert!(parse(r#"{"schema": "x", "gt4recomp": "1.0.0", "settings_schema": 1}"#).is_err());
    }

    #[test]
    fn an_unwritable_install_folder_falls_back_to_the_data_folder() {
        let data = scratch("fallback-data");
        let missing_install = data.join("no-such-install-folder");
        let location = location(&missing_install, &data);
        assert_eq!(location.path, data.join(VERSION_FILE_NAME));
        assert!(location.notice.unwrap().contains("not writable"));
    }

    #[test]
    fn a_writable_install_folder_is_used_without_notice() {
        let install = scratch("writable-install");
        let data = scratch("writable-data");
        let location = location(&install, &data);
        assert_eq!(location.path, install.join(VERSION_FILE_NAME));
        assert!(location.notice.is_none());
        assert!(!install.join(".version-probe").exists());
    }
}
