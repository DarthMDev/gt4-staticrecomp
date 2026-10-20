//! File-level migration tests for `settings.toml` and `version.json` (temporary folders, no network).

use std::fs;
use std::path::{Path, PathBuf};

use gt4l_core::config::{self, ConfigFile};
use gt4l_core::versions::{self, VersionFile};
use gt4l_core::Version;

fn scratch(name: &str) -> PathBuf {
    let directory = std::env::temp_dir().join(format!("gt4l-migration-test-{}-{name}", std::process::id()));
    let _ = fs::remove_dir_all(&directory);
    fs::create_dir_all(&directory).unwrap();
    directory
}

fn backups(directory: &Path, prefix: &str) -> Vec<PathBuf> {
    fs::read_dir(directory)
        .unwrap()
        .flatten()
        .map(|entry| entry.path())
        .filter(|path| path.file_name().unwrap().to_string_lossy().starts_with(prefix))
        .collect()
}

#[test]
fn an_unversioned_dev_build_file_is_backed_up_and_migrated_to_schema_one() {
    let directory = scratch("v0");
    let path = directory.join("settings.toml");
    let original = "[settings]\nvsync = \"fast\"\nfullscreen = \"off\"\n\n[install]\ndisc_path = \"D:/gt4.iso\"\n";
    fs::write(&path, original).unwrap();

    let (loaded, notes) = config::load(&path);
    assert_eq!(loaded.settings.get("vsync"), "fast", "the values survive the migration");
    assert_eq!(loaded.disc_path.as_deref(), Some(Path::new("D:/gt4.iso")));
    assert!(notes.iter().any(|note| note.contains("migrated from schema 0 to 1")), "{notes:?}");

    let copies = backups(&directory, "settings.toml.bak-schema0-");
    assert_eq!(copies.len(), 1, "exactly one backup");
    assert_eq!(fs::read_to_string(&copies[0]).unwrap(), original, "the backup is the untouched original");

    let rewritten = fs::read_to_string(&path).unwrap();
    assert!(rewritten.contains("schema_version = 1"), "{rewritten}");
    assert!(!rewritten.contains("\r\r"));
}

#[test]
fn a_migrated_file_is_not_migrated_or_backed_up_again() {
    let directory = scratch("second-load");
    let path = directory.join("settings.toml");
    fs::write(&path, "[settings]\nvsync = \"off\"\n").unwrap();
    let _ = config::load(&path);
    let _ = config::load(&path);
    assert_eq!(backups(&directory, "settings.toml.bak-schema0-").len(), 1, "one backup for one migration");
}

#[test]
fn a_newer_schema_is_used_as_defaults_and_the_file_is_not_touched() {
    let directory = scratch("newer");
    let path = directory.join("settings.toml");
    let newer = "schema_version = 9\n[settings]\nvsync = \"fast\"\n";
    fs::write(&path, newer).unwrap();
    let (loaded, notes) = config::load(&path);
    assert_eq!(loaded, ConfigFile::default());
    assert!(notes.iter().any(|note| note.contains("schema 9")), "{notes:?}");
    assert_eq!(fs::read_to_string(&path).unwrap(), newer, "a newer file is never rewritten");
    assert!(backups(&directory, "settings.toml.bak").is_empty());
}

#[test]
fn a_corrupt_file_is_kept_as_a_copy_and_defaults_are_used() {
    let directory = scratch("corrupt");
    let path = directory.join("settings.toml");
    fs::write(&path, "[settings\nvsync = ").unwrap();
    let (loaded, notes) = config::load(&path);
    assert_eq!(loaded, ConfigFile::default());
    assert!(!notes.is_empty());
    assert!(directory.join("settings.toml.bad").exists(), "the damaged file is kept");
}

#[test]
fn skipped_update_versions_round_trip_through_settings() {
    let directory = scratch("skip");
    let path = directory.join("settings.toml");
    let mut config = ConfigFile::default();
    config.skipped_renderer = Some(Version(1, 0, 2));
    config.skipped_launcher = Some(Version(1, 0, 1));
    config::save(&path, &config).unwrap();
    let (loaded, _) = config::load(&path);
    assert_eq!(loaded.skipped_renderer, Some(Version(1, 0, 2)));
    assert_eq!(loaded.skipped_launcher, Some(Version(1, 0, 1)));
}

#[test]
fn a_damaged_version_file_is_refused_so_the_caller_falls_back_to_the_program_version() {
    let directory = scratch("version-damaged");
    let path = directory.join("version.json");
    fs::write(&path, "{ this is not json").unwrap();
    assert!(versions::read(&path).is_err());
    fs::write(&path, r#"{"schema": 4, "gt4recomp": "9.0.0", "settings_schema": 1}"#).unwrap();
    assert!(versions::read(&path).is_err(), "a newer version file is not used");
    assert_eq!(fs::read_to_string(&path).unwrap(), r#"{"schema": 4, "gt4recomp": "9.0.0", "settings_schema": 1}"#);
    let file: Option<VersionFile> = versions::read(&directory.join("missing.json")).unwrap();
    assert!(file.is_none());
}
