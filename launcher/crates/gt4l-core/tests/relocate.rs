// The move from the 1.2.0 layout to the 1.2.1 layout (TASK-091). Every test runs in its own scratch folder under the
// Cargo target dir with synthetic files; nothing here reads or writes a real user folder. A real second volume is used
// only when GT4_TEST_SECOND_VOLUME names a scratch folder on another drive.

use std::collections::BTreeMap;
use std::fs;
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

use gt4l_core::paths::{DataDirs, LegacyLocations, UserDirs, UserRootSource, MOVED_USER_FOLDER_NAME, USER_FOLDER_NAME};
use gt4l_core::relocate::{
    self, marker_status, FsOps, LargeMove, LargeOutcome, UnitResult, UserMove, UserOutcome, ERROR_NOT_SAME_DEVICE, KEPT_FOLDER_NAME,
    README_NAME,
};

const STAMP: &str = "20261022-120000";

struct Fixture {
    base: PathBuf,
    local: PathBuf,
    documents: PathBuf,
    appdata: PathBuf,
    data: PathBuf,
}

impl Fixture {
    fn new(name: &str) -> Fixture {
        Fixture::under(&PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join(format!("relocate-{name}")), None)
    }

    /// `data_base` puts the install folder elsewhere (another volume).
    fn under(base: &Path, data_base: Option<&Path>) -> Fixture {
        let _ = fs::remove_dir_all(base);
        fs::create_dir_all(base).unwrap();
        let data = data_base.map(Path::to_path_buf).unwrap_or_else(|| base.join("Program Files").join("GT4Recomp")).join("data");
        if let Some(data_base) = data_base {
            let _ = fs::remove_dir_all(data_base);
        }
        let fixture = Fixture {
            local: base.join("Local").join("GT4Recomp"),
            documents: base.join("Documents"),
            appdata: base.join("Roaming").join("GT4Recomp"),
            data,
            base: base.to_path_buf(),
        };
        fs::create_dir_all(&fixture.documents).unwrap();
        fs::create_dir_all(&fixture.data).unwrap();
        fixture
    }

    fn legacy(&self) -> LegacyLocations {
        LegacyLocations {
            local: Some(self.local.clone()),
            documents_player: Some(self.documents.join(USER_FOLDER_NAME)),
            previous_data: None,
        }
    }

    fn user(&self) -> UserDirs {
        UserDirs::new(&self.appdata, UserRootSource::AppData)
    }

    fn player(&self) -> PathBuf {
        self.documents.join(USER_FOLDER_NAME)
    }

    /// The 1.2.0 user data: settings and controls in Local, a save library with an active save, a slot, a backup
    /// copy and a replay in Documents, and a card backup in Local.
    fn user_data_120(&self) {
        write(&self.local.join("config").join("settings.toml"), b"schema_version = 6\n[settings]\nvsync = \"on\"\n");
        write(&self.local.join("controls.toml"), b"schema = 1\n");
        write(&self.local.join("library").join("backups").join("20261021-000000-card.ps2"), &[7u8; 4096]);
        let saves = self.player().join("Saves");
        write(&saves.join("Active").join("BASCUS-97328GT4").join("icon.sys"), b"active save");
        write(&saves.join("Save 01").join("BASCUS-97328GT4").join("icon.sys"), b"slot one");
        write(&saves.join(".backup").join("BASCUS-97328GT4").join("icon.sys"), b"backup copy");
        write(&self.player().join("Replays").join("BASCUS-97328R0001").join("data"), b"replay");
        write(&self.player().join("Textures").join("replacements").join("a.png"), b"texture");
    }

    /// The 1.2.0 large data: a disc copy and prepared files with a receipt, gt4-gs, runtime card, sessions, logs.
    fn large_data_120(&self) {
        write(&self.local.join("disc").join("SCUS-97328.iso"), &vec![0x5Au8; 3 * 1024 * 1024 + 17]);
        write(&self.local.join("prepared").join("prepare.json"), b"{\"image\":\"SCUS-97328.iso\"}");
        write(&self.local.join("prepared").join("CORE.GT4"), b"core");
        write(&self.local.join("gs").join("current.txt"), b"1.2.0");
        write(&self.local.join("gs").join("1.2.0").join("gt4-gs.exe"), b"gs");
        write(&self.local.join("runtime").join("card.ps2"), &[1u8; 1024]);
        write(&self.local.join("sessions").join("20261021-1").join("game.log"), b"session");
        write(&self.local.join("logs").join("launcher.log"), b"old log");
        // An old modified time, so a copy that does not keep it is caught.
        let old = SystemTime::UNIX_EPOCH + Duration::from_secs(1_700_000_000);
        fs::OpenOptions::new().write(true).open(self.local.join("disc").join("SCUS-97328.iso")).unwrap().set_modified(old).unwrap();
    }

    fn user_move<'a>(&'a self, legacy: &'a LegacyLocations, user: &'a UserDirs, game_running: bool) -> UserMove<'a> {
        UserMove { legacy, user, data_root: &self.data, game_running, stamp: STAMP }
    }

    fn large_move<'a>(&'a self, check: &'a dyn Fn(&Path, &BTreeMap<PathBuf, String>) -> Result<(), String>, game_running: bool) -> LargeMove<'a> {
        LargeMove {
            local: &self.local,
            data_root: &self.data,
            textures_sources: vec![
                self.player().join("Textures"),
                self.documents.join(MOVED_USER_FOLDER_NAME).join("Textures"),
            ],
            marker: self.data.join("migration-1.2.1.json"),
            game_running,
            stamp: STAMP,
            check_game_files: check,
            old_copy_folder: "from-1.2.0",
        }
    }
}

/// Writes a file, creating its folders.
fn write(path: &Path, bytes: &[u8]) {
    fs::create_dir_all(path.parent().unwrap()).unwrap();
    fs::write(path, bytes).unwrap();
}

fn read(path: &Path) -> Vec<u8> {
    fs::read(path).unwrap_or_else(|error| panic!("{}: {error}", path.display()))
}

/// True when no file lies under `path` (empty folders do not count).
fn no_file_under(path: &Path) -> bool {
    match fs::read_dir(path) {
        Err(_) => true,
        Ok(entries) => entries.flatten().all(|entry| {
            let child = entry.path();
            child.is_dir() && no_file_under(&child)
        }),
    }
}

fn no_check(_: &Path, _: &BTreeMap<PathBuf, String>) -> Result<(), String> {
    Ok(())
}

fn cross_volume_rename(_: &Path, _: &Path) -> io::Result<()> {
    Err(io::Error::from_raw_os_error(ERROR_NOT_SAME_DEVICE))
}

fn real_rename(from: &Path, to: &Path) -> io::Result<()> {
    fs::rename(from, to)
}

fn real_open(path: &Path) -> io::Result<Box<dyn Read>> {
    Ok(Box::new(fs::File::open(path)?))
}

/// Opens every file except replays, which fail like an offline OneDrive placeholder (ERROR_CLOUD_FILE_UNAVAILABLE).
fn onedrive_offline_open(path: &Path) -> io::Result<Box<dyn Read>> {
    if path.components().any(|part| part.as_os_str() == "Replays") {
        return Err(io::Error::from_raw_os_error(362));
    }
    real_open(path)
}

/// A reader that fails after its first megabyte, like a full disk or a file locked half way.
struct FailingReader(fs::File, usize);
impl Read for FailingReader {
    fn read(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        if self.1 >= 1 << 20 {
            return Err(io::Error::other("simulated: there is not enough space on the disk"));
        }
        let read = self.0.read(buffer)?;
        self.1 += read;
        Ok(read)
    }
}

fn failing_iso_open(path: &Path) -> io::Result<Box<dyn Read>> {
    let file = fs::File::open(path)?;
    if path.extension().is_some_and(|extension| extension == "iso") {
        return Ok(Box::new(FailingReader(file, 0)));
    }
    Ok(Box::new(file))
}

// User data ---------------------------------------------------------------------------------------------------------

#[test]
fn user_data_moves_to_appdata_and_the_documents_folder_is_renamed_with_a_note() {
    let fixture = Fixture::new("user-same-volume");
    fixture.user_data_120();
    let (legacy, user) = (fixture.legacy(), fixture.user());
    let outcome = relocate::migrate_user_data(&fixture.user_move(&legacy, &user, false), &FsOps::real());
    let UserOutcome::Moved { files, kept_both, .. } = &outcome else { panic!("{outcome:?}") };
    assert_eq!(*files, 7, "settings, controls, card backup, active, slot, backup copy, replay");
    assert!(kept_both.is_empty());
    assert!(outcome.uses_new_place());
    assert_eq!(read(&fixture.appdata.join("config").join("settings.toml")), b"schema_version = 6\n[settings]\nvsync = \"on\"\n");
    assert_eq!(read(&fixture.appdata.join("controls.toml")), b"schema = 1\n");
    assert_eq!(read(&fixture.appdata.join("backups").join("20261021-000000-card.ps2")), vec![7u8; 4096]);
    assert_eq!(read(&fixture.appdata.join("Saves").join("Active").join("BASCUS-97328GT4").join("icon.sys")), b"active save");
    assert_eq!(read(&fixture.appdata.join("Saves").join("Save 01").join("BASCUS-97328GT4").join("icon.sys")), b"slot one");
    assert_eq!(read(&fixture.appdata.join("Saves").join(".backup").join("BASCUS-97328GT4").join("icon.sys")), b"backup copy");
    assert_eq!(read(&fixture.appdata.join("Replays").join("BASCUS-97328R0001").join("data")), b"replay");
    assert!(!fixture.appdata.join("Textures").exists(), "textures are large data, not user data");
    // Nothing old is deleted: the Documents folder is renamed, with a note; Local keeps its files.
    let moved = fixture.documents.join(MOVED_USER_FOLDER_NAME);
    assert!(!fixture.player().exists());
    assert_eq!(read(&moved.join("Saves").join("Active").join("BASCUS-97328GT4").join("icon.sys")), b"active save");
    let readme = String::from_utf8(read(&moved.join(README_NAME))).unwrap();
    assert!(readme.contains(&fixture.appdata.display().to_string()), "{readme}");
    assert!(readme.contains("nothing in it was deleted"), "{readme}");
    assert!(fixture.local.join("config").join("settings.toml").exists());
    assert_eq!(marker_status(&user.relocation_marker()).as_deref(), Some("migrated"));
    assert!(!fixture.appdata.join(".migrating-1.2.1").exists(), "no staging folder left");
    // Idempotent.
    let again = relocate::migrate_user_data(&fixture.user_move(&legacy, &user, false), &FsOps::real());
    assert_eq!(again, UserOutcome::AlreadyMoved { notes: Vec::new() });
}

#[test]
fn nothing_to_move_on_a_new_pc() {
    let fixture = Fixture::new("user-nothing");
    let (legacy, user) = (fixture.legacy(), fixture.user());
    let outcome = relocate::migrate_user_data(&fixture.user_move(&legacy, &user, false), &FsOps::real());
    assert_eq!(outcome, UserOutcome::NothingToMove);
    assert!(outcome.uses_new_place());
    assert!(!fixture.appdata.exists(), "no folder is made for nothing");
}

#[test]
fn user_data_is_not_moved_while_the_game_runs() {
    let fixture = Fixture::new("user-game-running");
    fixture.user_data_120();
    let (legacy, user) = (fixture.legacy(), fixture.user());
    let outcome = relocate::migrate_user_data(&fixture.user_move(&legacy, &user, true), &FsOps::real());
    assert!(matches!(outcome, UserOutcome::Deferred(_)), "{outcome:?}");
    assert!(!outcome.uses_new_place());
    assert!(!fixture.appdata.exists());
    assert!(fixture.player().join("Saves").exists());
}

#[test]
fn an_offline_onedrive_file_keeps_the_old_place_and_the_next_start_finishes_the_move() {
    let fixture = Fixture::new("user-onedrive");
    fixture.user_data_120();
    let (legacy, user) = (fixture.legacy(), fixture.user());
    let offline = FsOps { rename: &real_rename, open: &onedrive_offline_open };
    let outcome = relocate::migrate_user_data(&fixture.user_move(&legacy, &user, false), &offline);
    let UserOutcome::Failed(error) = &outcome else { panic!("{outcome:?}") };
    assert!(error.contains("Replays"), "{error}");
    assert!(!outcome.uses_new_place());
    assert!(!fixture.appdata.join("Saves").exists(), "nothing is placed before every file passed");
    assert!(!fixture.appdata.join(".migrating-1.2.1").exists(), "the staging copy is removed");
    assert!(fixture.player().join("Replays").join("BASCUS-97328R0001").join("data").exists(), "the old place is untouched");
    assert_eq!(marker_status(&user.relocation_marker()), None);
    // Back online: the next start moves everything.
    let outcome = relocate::migrate_user_data(&fixture.user_move(&legacy, &user, false), &FsOps::real());
    assert!(matches!(outcome, UserOutcome::Moved { .. }), "{outcome:?}");
    assert_eq!(read(&fixture.appdata.join("Replays").join("BASCUS-97328R0001").join("data")), b"replay");
}

#[test]
fn existing_destination_items_are_never_overwritten() {
    let fixture = Fixture::new("user-existing");
    fixture.user_data_120();
    // AppData already holds: a different active save, the same settings and different controls.
    write(&fixture.appdata.join("Saves").join("Active").join("BASCUS-97328GT4").join("icon.sys"), b"newer active save");
    write(&fixture.appdata.join("config").join("settings.toml"), b"schema_version = 6\n[settings]\nvsync = \"on\"\n");
    write(&fixture.appdata.join("controls.toml"), b"schema = 1\n# changed\n");
    let (legacy, user) = (fixture.legacy(), fixture.user());
    let outcome = relocate::migrate_user_data(&fixture.user_move(&legacy, &user, false), &FsOps::real());
    let UserOutcome::Moved { kept_both, .. } = &outcome else { panic!("{outcome:?}") };
    assert_eq!(kept_both.len(), 2, "{kept_both:?}");
    // Existing files are as they were.
    assert_eq!(read(&fixture.appdata.join("Saves").join("Active").join("BASCUS-97328GT4").join("icon.sys")), b"newer active save");
    assert_eq!(read(&fixture.appdata.join("controls.toml")), b"schema = 1\n# changed\n");
    // Free places are filled first: the 1.2.0 slot 01 stays slot 01, and the 1.2.0 active save became slot 02.
    assert_eq!(read(&fixture.appdata.join("Saves").join("Save 01").join("BASCUS-97328GT4").join("icon.sys")), b"slot one");
    assert_eq!(read(&fixture.appdata.join("Saves").join("Save 02").join("BASCUS-97328GT4").join("icon.sys")), b"active save");
    assert_eq!(read(&fixture.appdata.join(KEPT_FOLDER_NAME).join("controls.toml")), b"schema = 1\n");
    // The identical settings file counts as already in place.
    assert!(!fixture.appdata.join(KEPT_FOLDER_NAME).join("config").exists());
}

#[test]
fn a_run_that_stopped_after_placing_some_items_is_resumed() {
    let fixture = Fixture::new("user-resume");
    fixture.user_data_120();
    // An earlier run placed the replay (identical) and left a staging folder behind, then stopped.
    write(&fixture.appdata.join("Replays").join("BASCUS-97328R0001").join("data"), b"replay");
    write(&fixture.appdata.join(".migrating-1.2.1").join("9").join("half"), b"half");
    let (legacy, user) = (fixture.legacy(), fixture.user());
    let outcome = relocate::migrate_user_data(&fixture.user_move(&legacy, &user, false), &FsOps::real());
    let UserOutcome::Moved { kept_both, .. } = &outcome else { panic!("{outcome:?}") };
    assert!(kept_both.is_empty(), "an identical item is not a conflict: {kept_both:?}");
    assert!(!fixture.appdata.join(".migrating-1.2.1").exists());
}

#[test]
fn the_1_2_0_data_folder_fallback_library_is_moved_too() {
    let fixture = Fixture::new("user-local-player");
    write(&fixture.local.join(USER_FOLDER_NAME).join("Saves").join("Active").join("BASCUS-97328GT4").join("icon.sys"), b"fallback save");
    let (legacy, user) = (fixture.legacy(), fixture.user());
    let outcome = relocate::migrate_user_data(&fixture.user_move(&legacy, &user, false), &FsOps::real());
    assert!(matches!(outcome, UserOutcome::Moved { .. }), "{outcome:?}");
    assert_eq!(read(&fixture.appdata.join("Saves").join("Active").join("BASCUS-97328GT4").join("icon.sys")), b"fallback save");
    assert!(fixture.local.join(MOVED_USER_FOLDER_NAME).join(README_NAME).exists());
}

#[test]
fn a_failed_placement_puts_back_what_this_run_placed_and_the_next_start_moves_everything() {
    let fixture = Fixture::new("user-placement-fails");
    fixture.user_data_120();
    let (legacy, user) = (fixture.legacy(), fixture.user());
    let staging = fixture.appdata.join(".migrating-1.2.1");
    // The second placement (not a staging rename) fails, as a full disk or a locked folder would.
    let placements = std::cell::Cell::new(0);
    let failing_second = |from: &Path, to: &Path| {
        if to.starts_with(&staging) {
            return fs::rename(from, to);
        }
        placements.set(placements.get() + 1);
        if placements.get() == 2 {
            return Err(io::Error::other("the disk is full"));
        }
        fs::rename(from, to)
    };
    let failing = FsOps { rename: &failing_second, open: &real_open };
    let outcome = relocate::migrate_user_data(&fixture.user_move(&legacy, &user, false), &failing);
    let UserOutcome::Failed(error) = &outcome else { panic!("{outcome:?}") };
    assert!(error.contains("disk is full"), "{error}");
    assert!(placements.get() >= 2, "the first item was placed before the failure");
    // The new user folder holds no file of this run (folders the run created on the way may stay empty).
    assert!(no_file_under(&fixture.appdata), "a placed item was left in the new folder");
    assert!(!staging.exists(), "the staging folder is removed");
    assert!(!outcome.uses_new_place());
    assert_eq!(marker_status(&user.relocation_marker()), None, "no marker after a failed move");
    // The sources are intact and the old place is still in use.
    assert_eq!(read(&fixture.local.join("config").join("settings.toml")), b"schema_version = 6\n[settings]\nvsync = \"on\"\n");
    assert_eq!(read(&fixture.player().join("Saves").join("Active").join("BASCUS-97328GT4").join("icon.sys")), b"active save");
    assert_eq!(read(&fixture.player().join("Replays").join("BASCUS-97328R0001").join("data")), b"replay");
    // A later run with a working rename moves everything correctly and writes the marker.
    let outcome = relocate::migrate_user_data(&fixture.user_move(&legacy, &user, false), &FsOps::real());
    let UserOutcome::Moved { files, kept_both, .. } = &outcome else { panic!("{outcome:?}") };
    assert_eq!(*files, 7);
    assert!(kept_both.is_empty(), "{kept_both:?}");
    assert_eq!(read(&fixture.appdata.join("Saves").join("Active").join("BASCUS-97328GT4").join("icon.sys")), b"active save");
    assert_eq!(read(&fixture.appdata.join("Saves").join("Save 01").join("BASCUS-97328GT4").join("icon.sys")), b"slot one");
    assert_eq!(read(&fixture.appdata.join("Replays").join("BASCUS-97328R0001").join("data")), b"replay");
    assert_eq!(marker_status(&user.relocation_marker()).as_deref(), Some("migrated"));
}

// Large data --------------------------------------------------------------------------------------------------------

#[test]
fn large_data_on_the_same_volume_moves_by_rename() {
    let fixture = Fixture::new("large-same-volume");
    fixture.large_data_120();
    fixture.user_data_120();
    let checked = std::cell::Cell::new(0);
    let check = |folder: &Path, hashes: &BTreeMap<PathBuf, String>| {
        checked.set(checked.get() + 1);
        assert!(hashes.is_empty(), "a rename copies nothing");
        assert!(folder.join("prepared").join("prepare.json").exists());
        Ok(())
    };
    let outcome = relocate::migrate_large_data(&fixture.large_move(&check, false), &FsOps::real(), &mut |_| {});
    assert!(!outcome.failed(), "{outcome:?}");
    assert_eq!(checked.get(), 1);
    for (name, result) in outcome.units() {
        assert!(matches!(result, UnitResult::Moved { renamed: true, .. }), "{name}: {result:?}");
    }
    assert_eq!(read(&fixture.data.join("disc").join("SCUS-97328.iso")).len(), 3 * 1024 * 1024 + 17);
    assert_eq!(read(&fixture.data.join("gs").join("current.txt")), b"1.2.0");
    assert_eq!(read(&fixture.data.join("runtime").join("card.ps2")), vec![1u8; 1024]);
    assert_eq!(read(&fixture.data.join("Textures").join("replacements").join("a.png")), b"texture");
    for name in ["disc", "prepared", "gs", "runtime", "sessions", "logs"] {
        assert!(!fixture.local.join(name).exists(), "{name} left the 1.2.0 folder");
    }
    assert_eq!(marker_status(&fixture.data.join("migration-1.2.1.json")).as_deref(), Some("migrated"));
    assert_eq!(relocate::migrate_large_data(&fixture.large_move(&no_check, false), &FsOps::real(), &mut |_| {}), LargeOutcome::AlreadyMoved);
}

/// The copy path: what a move to another volume does.
fn check_cross_volume(fixture: &Fixture, ops: &FsOps<'_>) {
    fixture.large_data_120();
    fixture.user_data_120();
    let iso_bytes = read(&fixture.local.join("disc").join("SCUS-97328.iso"));
    let expected_sha = {
        use sha2::{Digest, Sha256};
        Sha256::digest(&iso_bytes).iter().map(|byte| format!("{byte:02x}")).collect::<String>()
    };
    let old_time = fs::metadata(fixture.local.join("disc").join("SCUS-97328.iso")).unwrap().modified().unwrap();
    let check = |folder: &Path, hashes: &BTreeMap<PathBuf, String>| {
        // The launcher compares the copied image's hash with the pinned one; here, with the source's.
        let image = Path::new("disc").join("SCUS-97328.iso");
        if hashes.get(&image) != Some(&expected_sha) {
            return Err(format!("disc hash {:?}", hashes.get(&image)));
        }
        assert!(folder.join("disc").join("SCUS-97328.iso").exists());
        Ok(())
    };
    let mut last = relocate::LargeProgress::default();
    let outcome = relocate::migrate_large_data(&fixture.large_move(&check, false), ops, &mut |progress| last = progress.clone());
    assert!(!outcome.failed(), "{outcome:?}");
    let game = &outcome.units()[0].1;
    assert!(matches!(game, UnitResult::Moved { renamed: false, .. }), "{game:?}");
    assert_eq!(read(&fixture.data.join("disc").join("SCUS-97328.iso")), iso_bytes);
    assert_eq!(fs::metadata(fixture.data.join("disc").join("SCUS-97328.iso")).unwrap().modified().unwrap(), old_time, "the modified time the receipt records is kept");
    assert!(!fixture.local.join("disc").exists(), "the source is deleted after the checked copy");
    assert!(!fixture.data.join(".game-files.migrating").exists());
    assert_eq!(read(&fixture.data.join("Textures").join("replacements").join("a.png")), b"texture");
    assert!(last.bytes_total > 0 && last.bytes_done == last.bytes_total, "{last:?}");
}

#[test]
fn large_data_on_another_volume_is_copied_checked_and_then_removed() {
    let fixture = Fixture::new("large-cross-volume");
    check_cross_volume(&fixture, &FsOps { rename: &cross_volume_rename, open: &real_open });
}

#[test]
fn large_data_crosses_a_real_second_volume_when_one_is_given() {
    let Some(second) = std::env::var_os("GT4_TEST_SECOND_VOLUME") else {
        eprintln!("GT4_TEST_SECOND_VOLUME not set; the simulated cross-volume test covers the copy path");
        return;
    };
    let base = std::env::temp_dir().join(format!("gt4l-relocate-real-volume-{}", std::process::id()));
    let data_base = PathBuf::from(second).join("relocate-real-volume-install");
    let volume = |path: &Path| path.components().next().map(|part| part.as_os_str().to_string_lossy().to_lowercase());
    if volume(&base) == volume(&data_base) {
        eprintln!("GT4_TEST_SECOND_VOLUME is on the same volume as the temporary folder; skipped");
        return;
    }
    let fixture = Fixture::under(&base, Some(&data_base));
    check_cross_volume(&fixture, &FsOps::real());
    let _ = fs::remove_dir_all(&data_base);
    let _ = fs::remove_dir_all(&base);
}

#[test]
fn a_failed_copy_keeps_the_old_place_and_the_next_start_retries() {
    let fixture = Fixture::new("large-disk-full");
    fixture.large_data_120();
    let failing = FsOps { rename: &cross_volume_rename, open: &failing_iso_open };
    let outcome = relocate::migrate_large_data(&fixture.large_move(&no_check, false), &failing, &mut |_| {});
    assert!(outcome.failed());
    let UnitResult::Failed(error) = &outcome.units()[0].1 else { panic!("{outcome:?}") };
    assert!(error.contains("not enough space"), "{error}");
    assert!(fixture.local.join("disc").join("SCUS-97328.iso").exists(), "the source stays");
    assert!(!fixture.data.join("disc").exists() && !fixture.data.join(".game-files.migrating").exists());
    assert_eq!(marker_status(&fixture.data.join("migration-1.2.1.json")), None, "no marker after a failure");
    // The other units moved; the next start moves the game files.
    assert!(fixture.data.join("gs").join("current.txt").exists());
    let outcome = relocate::migrate_large_data(&fixture.large_move(&no_check, false), &FsOps { rename: &cross_volume_rename, open: &real_open }, &mut |_| {});
    assert!(!outcome.failed(), "{outcome:?}");
    assert!(fixture.data.join("disc").join("SCUS-97328.iso").exists());
    assert_eq!(marker_status(&fixture.data.join("migration-1.2.1.json")).as_deref(), Some("migrated"));
}

#[test]
fn game_files_that_fail_their_check_are_put_back() {
    let fixture = Fixture::new("large-bad-check");
    fixture.large_data_120();
    let refuse = |_: &Path, _: &BTreeMap<PathBuf, String>| Err("the disc copy does not match its pin".to_string());
    let outcome = relocate::migrate_large_data(&fixture.large_move(&refuse, false), &FsOps::real(), &mut |_| {});
    assert!(matches!(&outcome.units()[0].1, UnitResult::Failed(error) if error.contains("pin")), "{outcome:?}");
    assert!(fixture.local.join("disc").join("SCUS-97328.iso").exists(), "the rename was undone");
    assert!(fixture.local.join("prepared").join("prepare.json").exists());
    assert!(!fixture.data.join("disc").exists());
}

#[test]
fn existing_game_files_in_the_install_folder_are_kept_and_the_old_copy_is_left() {
    let fixture = Fixture::new("large-existing");
    fixture.large_data_120();
    write(&fixture.data.join("prepared").join("prepare.json"), b"newer");
    write(&fixture.data.join("logs").join("launcher.log"), b"new log");
    let outcome = relocate::migrate_large_data(&fixture.large_move(&no_check, false), &FsOps::real(), &mut |_| {});
    assert!(matches!(&outcome.units()[0].1, UnitResult::Kept(_)), "{outcome:?}");
    assert_eq!(read(&fixture.data.join("prepared").join("prepare.json")), b"newer");
    assert!(fixture.local.join("disc").join("SCUS-97328.iso").exists());
    // Old logs go beside the new ones.
    assert_eq!(read(&fixture.data.join("logs").join("launcher.log")), b"new log");
    assert_eq!(read(&fixture.data.join("logs").join("from-1.2.0").join("logs").join("launcher.log")), b"old log");
    assert!(outcome.log_lines().iter().any(|line| line.contains("left in place")));
}

#[test]
fn large_data_is_not_moved_while_the_game_runs() {
    let fixture = Fixture::new("large-game-running");
    fixture.large_data_120();
    let outcome = relocate::migrate_large_data(&fixture.large_move(&no_check, true), &FsOps::real(), &mut |_| {});
    assert!(matches!(outcome, LargeOutcome::Deferred(_)), "{outcome:?}");
    assert!(fixture.local.join("disc").exists() && !fixture.data.join("disc").exists());
    let _ = &fixture.base;
}

// Previous install's data (a reinstall into another folder, TASK-091) ---------------------------------------------------

/// The large data an earlier install left in its data folder: the same layout as 1.2.0's large data, with the
/// textures directly under the folder.
fn previous_data(previous: &Path) {
    write(&previous.join("disc").join("SCUS-97328.iso"), &vec![0x6Bu8; 2 * 1024 * 1024 + 5]);
    write(&previous.join("prepared").join("prepare.json"), b"{\"image\":\"SCUS-97328.iso\"}");
    write(&previous.join("prepared").join("CORE.GT4"), b"core");
    write(&previous.join("gs").join("current.txt"), b"1.2.1");
    write(&previous.join("gs").join("1.2.1").join("gt4-gs.exe"), b"gs");
    write(&previous.join("runtime").join("card.ps2"), &[2u8; 512]);
    write(&previous.join("logs").join("launcher.log"), b"previous log");
    write(&previous.join("Textures").join("replacements").join("b.png"), b"previous texture");
}

/// The move of `previous` into the fixture's data folder, with its own marker.
fn previous_move<'a>(fixture: &'a Fixture, previous: &'a Path, check: &'a dyn Fn(&Path, &BTreeMap<PathBuf, String>) -> Result<(), String>) -> LargeMove<'a> {
    LargeMove {
        local: previous,
        data_root: &fixture.data,
        textures_sources: vec![previous.join("Textures")],
        marker: DataDirs::new(fixture.data.clone()).previous_data_marker(previous),
        game_running: false,
        stamp: STAMP,
        check_game_files: check,
        old_copy_folder: "from-previous-install",
    }
}

#[test]
fn a_previous_data_folder_moves_on_the_same_volume_and_is_emptied() {
    let fixture = Fixture::new("previous-same-volume");
    let previous = fixture.base.join("Old Install").join("data");
    previous_data(&previous);
    let check = |folder: &Path, hashes: &BTreeMap<PathBuf, String>| {
        assert!(hashes.is_empty(), "a rename copies nothing");
        assert!(folder.join("prepared").join("prepare.json").exists());
        Ok(())
    };
    let outcome = relocate::migrate_large_data(&previous_move(&fixture, &previous, &check), &FsOps::real(), &mut |_| {});
    assert!(!outcome.failed(), "{outcome:?}");
    assert!(outcome.moved());
    assert_eq!(read(&fixture.data.join("disc").join("SCUS-97328.iso")).len(), 2 * 1024 * 1024 + 5);
    assert_eq!(read(&fixture.data.join("gs").join("current.txt")), b"1.2.1");
    assert_eq!(read(&fixture.data.join("runtime").join("card.ps2")), vec![2u8; 512]);
    assert_eq!(read(&fixture.data.join("Textures").join("replacements").join("b.png")), b"previous texture");
    assert!(no_file_under(&previous), "the previous folder gives up every file");
    assert_eq!(marker_status(&DataDirs::new(fixture.data.clone()).previous_data_marker(&previous)).as_deref(), Some("migrated"));
    // The 1.2.0 marker is a different file: the previous install does not count as a 1.2.0 move.
    assert!(!fixture.data.join("migration-1.2.1.json").exists());
}

#[test]
fn a_previous_data_folder_moves_across_volumes_checked_and_then_removed() {
    let fixture = Fixture::new("previous-cross-volume");
    let previous = fixture.base.join("Old Install").join("data");
    previous_data(&previous);
    let iso_bytes = read(&previous.join("disc").join("SCUS-97328.iso"));
    let check = |_: &Path, hashes: &BTreeMap<PathBuf, String>| {
        if hashes.contains_key(&Path::new("disc").join("SCUS-97328.iso")) {
            Ok(())
        } else {
            Err("the disc image was not copied".to_string())
        }
    };
    let ops = FsOps { rename: &cross_volume_rename, open: &real_open };
    let outcome = relocate::migrate_large_data(&previous_move(&fixture, &previous, &check), &ops, &mut |_| {});
    assert!(!outcome.failed(), "{outcome:?}");
    assert_eq!(read(&fixture.data.join("disc").join("SCUS-97328.iso")), iso_bytes);
    assert_eq!(read(&fixture.data.join("Textures").join("replacements").join("b.png")), b"previous texture");
    assert!(no_file_under(&previous), "the source is deleted after the checked copy");
    assert!(!fixture.data.join(".game-files.migrating").exists());
}

#[test]
fn the_marker_stops_a_second_move_of_the_same_previous_folder() {
    let fixture = Fixture::new("previous-marker");
    let previous = fixture.base.join("Old Install").join("data");
    previous_data(&previous);
    let outcome = relocate::migrate_large_data(&previous_move(&fixture, &previous, &no_check), &FsOps::real(), &mut |_| {});
    assert!(!outcome.failed(), "{outcome:?}");
    // Something left behind later is not taken: the marker says this folder was done.
    write(&previous.join("logs").join("late.log"), b"late");
    let again = relocate::migrate_large_data(&previous_move(&fixture, &previous, &no_check), &FsOps::real(), &mut |_| {});
    assert_eq!(again, LargeOutcome::AlreadyMoved);
    assert!(previous.join("logs").join("late.log").exists());
    assert!(!fixture.data.join("logs").join("late.log").exists());
}

#[test]
fn a_missing_previous_folder_is_skipped_without_a_marker() {
    let fixture = Fixture::new("previous-missing");
    let previous = fixture.base.join("Never There").join("data");
    let marker = DataDirs::new(fixture.data.clone()).previous_data_marker(&previous);
    let outcome = relocate::migrate_large_data(&previous_move(&fixture, &previous, &no_check), &FsOps::real(), &mut |_| {});
    assert!(!outcome.failed());
    assert!(outcome.units().is_empty(), "{outcome:?}");
    assert!(!marker.exists(), "nothing to mark: the folder may appear later");
}

#[test]
fn game_files_already_in_the_install_folder_are_kept_and_old_logs_go_beside_them() {
    let fixture = Fixture::new("previous-existing");
    let previous = fixture.base.join("Old Install").join("data");
    previous_data(&previous);
    write(&fixture.data.join("prepared").join("prepare.json"), b"newer");
    write(&fixture.data.join("logs").join("launcher.log"), b"new log");
    let outcome = relocate::migrate_large_data(&previous_move(&fixture, &previous, &no_check), &FsOps::real(), &mut |_| {});
    assert!(!outcome.failed(), "{outcome:?}");
    assert!(matches!(&outcome.units()[0].1, UnitResult::Kept(_)), "{outcome:?}");
    assert_eq!(read(&fixture.data.join("prepared").join("prepare.json")), b"newer");
    assert!(previous.join("disc").join("SCUS-97328.iso").exists(), "the older copy is left in place");
    assert_eq!(read(&fixture.data.join("logs").join("launcher.log")), b"new log");
    assert_eq!(read(&fixture.data.join("logs").join("from-previous-install").join("logs").join("launcher.log")), b"previous log");
}
