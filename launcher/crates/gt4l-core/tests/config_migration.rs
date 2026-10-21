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
    assert!(notes.iter().any(|note| note.contains("migrated from schema 0 to 4")), "{notes:?}");

    let copies = backups(&directory, "settings.toml.bak-schema0-");
    assert_eq!(copies.len(), 1, "exactly one backup");
    assert_eq!(fs::read_to_string(&copies[0]).unwrap(), original, "the backup is the untouched original");

    let rewritten = fs::read_to_string(&path).unwrap();
    assert!(rewritten.contains("schema_version = 4"), "{rewritten}");
    assert!(rewritten.contains("widescreen = \"on\""), "{rewritten}");
    assert!(rewritten.contains("aspect_ratio = \"auto\""), "{rewritten}");
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

#[test]
fn the_fullscreen_default_is_on_for_new_installs() {
    let settings = gt4l_core::settings::Settings::default();
    assert_eq!(settings.get("fullscreen"), "on");
    assert!(!settings.was_chosen("fullscreen"));
}

#[test]
fn schema_one_fullscreen_off_that_was_never_chosen_becomes_on_and_keeps_a_backup() {
    let directory = scratch("fullscreen-default");
    let path = directory.join("settings.toml");
    let original = "schema_version = 1

[settings]
fullscreen = \"off\"
vsync = \"fast\"
";
    fs::write(&path, original).unwrap();

    let (loaded, notes) = config::load(&path);
    assert_eq!(loaded.settings.get("fullscreen"), "on", "the old windowed default is replaced");
    assert_eq!(loaded.settings.get("vsync"), "fast", "other values survive");
    assert!(notes.iter().any(|note| note.contains("migrated from schema 1 to 4")), "{notes:?}");

    let copies = backups(&directory, "settings.toml.bak-schema1-");
    assert_eq!(copies.len(), 1, "exactly one backup of the schema 1 file");
    assert_eq!(fs::read_to_string(&copies[0]).unwrap(), original, "the backup is the untouched original");
    assert!(fs::read_to_string(&path).unwrap().contains("fullscreen = \"on\""));
}

#[test]
fn a_fullscreen_value_the_user_chose_is_kept_through_the_migration() {
    let directory = scratch("fullscreen-chosen");
    let path = directory.join("settings.toml");
    fs::write(&path, "schema_version = 1

[settings]
fullscreen = \"off\"

[choices]
fullscreen = true
").unwrap();

    let (loaded, _) = config::load(&path);
    assert_eq!(loaded.settings.get("fullscreen"), "off", "a deliberate windowed choice stays");
    assert!(loaded.settings.was_chosen("fullscreen"));
}

#[test]
fn a_choice_made_on_the_settings_page_is_saved_and_read_back() {
    let directory = scratch("fullscreen-save");
    let path = directory.join("settings.toml");
    let mut config_file = ConfigFile::default();
    config_file.settings.set_by_user("fullscreen", "off").unwrap();
    config::save(&path, &config_file).unwrap();

    let text = fs::read_to_string(&path).unwrap();
    assert!(text.contains("[choices]
fullscreen = true"), "{text}");
    let (loaded, _) = config::load(&path);
    assert_eq!(loaded.settings.get("fullscreen"), "off");
    assert!(loaded.settings.was_chosen("fullscreen"));
}
