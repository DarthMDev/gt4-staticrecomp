//! Where the launcher keeps its files. The program folder is replaced by updates; the data folder
//! is never touched by an install or uninstall unless the user asks for it.
//!
//! The player's own files (the save library and replays) live in `Documents\Gran Turismo 4` (owner, 2026-10-21):
//! visible, easy to back up, and kept when the program or AppData is removed. Everything else (the disc copy,
//! prepared files, renderer, settings, logs, sessions and the live `runtime\card.ps2`) stays in the data folder.

use std::ffi::OsString;
use std::path::{Path, PathBuf};

/// Environment variable that moves the data folder (used by tests and portable experiments).
pub const DATA_DIR_OVERRIDE_VAR: &str = "GT4RECOMP_DATA_DIR";

/// Development and tests only: stands in for the Documents folder (the folder that gets `Gran Turismo 4`), so a
/// test run never touches the real Documents. Not for players.
pub const DOCUMENTS_DIR_OVERRIDE_VAR: &str = "GT4RECOMP_DOCUMENTS_DIR";

/// Name of the player's folder inside Documents.
pub const USER_FOLDER_NAME: &str = "Gran Turismo 4";

/// Name of the game executable inside the game folder.
pub const GAME_EXECUTABLE: &str = "gt4vulkan.exe";

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DataDirs {
    root: PathBuf,
}

impl DataDirs {
    pub fn new(root: impl Into<PathBuf>) -> Self {
        DataDirs { root: root.into() }
    }

    /// `GT4RECOMP_DATA_DIR` if set, otherwise `%LOCALAPPDATA%\GT4Recomp`.
    pub fn from_environment() -> Option<Self> {
        if let Some(root) = std::env::var_os(DATA_DIR_OVERRIDE_VAR) {
            return Some(DataDirs::new(root));
        }
        let local = std::env::var_os("LOCALAPPDATA")?;
        Some(DataDirs::new(PathBuf::from(local).join("GT4Recomp")))
    }

    pub fn root(&self) -> &Path {
        &self.root
    }

    pub fn config_file(&self) -> PathBuf {
        self.root.join("config").join("settings.toml")
    }

    /// Prepared game files: `CORE.GT4`, `SCUS_973.28` (the rebuilt ELF) and `prepare.json`.
    pub fn prepared(&self) -> PathBuf {
        self.root.join("prepared")
    }

    /// The copy of the player's disc image made by setup (`disc\SCUS-97328.iso`). The game reads it at run time,
    /// so the file the player picked is needed only once. Protected user data (see `guard`).
    pub fn disc_folder(&self) -> PathBuf {
        self.root.join("disc")
    }

    /// Setup writes here first and renames the folder into place at the end.
    pub fn prepared_staging(&self) -> PathBuf {
        self.root.join("prepared.tmp")
    }

    /// The memory card image that the game reads. A missing file is a blank card (see `sio2.cpp`).
    pub fn runtime_card(&self) -> PathBuf {
        self.root.join("runtime").join("card.ps2")
    }

    pub fn sessions(&self) -> PathBuf {
        self.root.join("sessions")
    }

    pub fn session(&self, name: &str) -> PathBuf {
        self.sessions().join(name)
    }

    pub fn logs(&self) -> PathBuf {
        self.root.join("logs")
    }

    /// The library folder of launchers before 1.0.0 (`library\memcard`, `library\saves`). Only `library\backups`
    /// is still written: card images kept before the runtime card is replaced.
    pub fn legacy_library(&self) -> PathBuf {
        self.root.join("library")
    }

    /// Card images kept before `runtime\card.ps2` is replaced (not player-facing; stays in AppData).
    pub fn card_backups(&self) -> PathBuf {
        self.legacy_library().join("backups")
    }

    /// Marker of the one-time move of the library to Documents. Not part of the settings file.
    pub fn library_migration_marker(&self) -> PathBuf {
        self.root.join("config").join("library-migration.json")
    }

    /// Where the player's folder goes when Documents cannot be found: `<data>\Gran Turismo 4`.
    pub fn fallback_user_root(&self) -> PathBuf {
        self.root.join(USER_FOLDER_NAME)
    }
}

/// How the player's folder was found.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum UserRootSource {
    /// `GT4RECOMP_DOCUMENTS_DIR` (development and tests).
    Override,
    /// The Documents known folder (SHGetKnownFolderPath, FOLDERID_Documents; follows OneDrive and other redirection).
    Documents,
    /// Documents was not available; the folder is in the data folder. The text says why (for the log).
    Fallback(String),
}

/// The player's folder: `<Documents>\Gran Turismo 4` with `Saves` and `Replays` (made on demand by the library).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct UserDirs {
    root: PathBuf,
    source: UserRootSource,
}

impl UserDirs {
    pub fn new(root: impl Into<PathBuf>, source: UserRootSource) -> Self {
        UserDirs { root: root.into(), source }
    }

    /// Picks the player's folder. `documents_override` is the value of `GT4RECOMP_DOCUMENTS_DIR`, `known_documents`
    /// the Documents known folder as Windows reports it (`None` when the call failed). The path is never built by
    /// hand from the user name. Documents must be an existing absolute folder; otherwise the data folder is used.
    pub fn resolve(documents_override: Option<OsString>, known_documents: Option<PathBuf>, data: &DataDirs) -> UserDirs {
        if let Some(value) = documents_override.filter(|value| !value.is_empty()) {
            return UserDirs::new(PathBuf::from(value).join(USER_FOLDER_NAME), UserRootSource::Override);
        }
        let reason = match known_documents {
            None => "Windows did not report a Documents folder".to_string(),
            Some(path) if !path.is_absolute() => format!("the Documents folder {} is not an absolute path", path.display()),
            Some(path) if !path.is_dir() => format!("the Documents folder {} is not available", path.display()),
            Some(path) => return UserDirs::new(path.join(USER_FOLDER_NAME), UserRootSource::Documents),
        };
        UserDirs::new(data.fallback_user_root(), UserRootSource::Fallback(reason))
    }

    pub fn root(&self) -> &Path {
        &self.root
    }

    pub fn source(&self) -> &UserRootSource {
        &self.source
    }

    /// `Saves`: the active save (`Active`), inactive slots (`Save NN`) and harvest copies (`.backup`).
    pub fn saves(&self) -> PathBuf {
        self.root.join("Saves")
    }

    /// `Replays`: one folder per replay.
    pub fn replays(&self) -> PathBuf {
        self.root.join("Replays")
    }

    /// `Textures`: the custom texture folder (TASK-088). The texture pack goes in its `replacements` subfolder; gt4-gs
    /// reads it there with no game-serial folder.
    pub fn textures(&self) -> PathBuf {
        self.root.join(TEXTURES_FOLDER_NAME)
    }
}

/// Name of the custom texture folder inside the player's folder (TASK-088).
pub const TEXTURES_FOLDER_NAME: &str = "Textures";

/// Creates the custom texture folders on demand: `Textures` and `Textures\replacements` (empty when new). The
/// launcher never writes the player's texture files; it only makes these two folders so a pack can be dropped in.
pub fn ensure_texture_folders(textures: &Path) -> std::io::Result<()> {
    std::fs::create_dir_all(textures.join("replacements"))
}

/// Folder that holds `gt4vulkan.exe`: `<program folder>\game` for an installed copy, or the
/// `[dev] game_dir` override from the config file.
pub fn game_directory(program_directory: &Path, dev_override: Option<&Path>) -> PathBuf {
    match dev_override {
        Some(path) => path.to_path_buf(),
        None => program_directory.join("game"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scratch(name: &str) -> PathBuf {
        let path = std::env::temp_dir().join(format!("gt4l-paths-{}-{name}", std::process::id()));
        let _ = std::fs::remove_dir_all(&path);
        std::fs::create_dir_all(&path).unwrap();
        path
    }

    #[test]
    fn the_known_documents_folder_gets_the_gran_turismo_4_folder() {
        let documents = scratch("known").join("OneDrive").join("Documents");
        std::fs::create_dir_all(&documents).unwrap();
        let data = DataDirs::new(scratch("known-data"));
        let user = UserDirs::resolve(None, Some(documents.clone()), &data);
        assert_eq!(user.source(), &UserRootSource::Documents);
        assert_eq!(user.root(), documents.join("Gran Turismo 4"));
        assert_eq!(user.saves(), documents.join("Gran Turismo 4").join("Saves"));
        assert_eq!(user.replays(), documents.join("Gran Turismo 4").join("Replays"));
        assert!(!user.root().exists(), "resolving creates nothing");
    }

    #[test]
    fn the_override_wins_over_the_known_folder() {
        let fake = scratch("override");
        let data = DataDirs::new(scratch("override-data"));
        let user = UserDirs::resolve(Some(fake.clone().into_os_string()), Some(scratch("override-real")), &data);
        assert_eq!(user.source(), &UserRootSource::Override);
        assert_eq!(user.root(), fake.join(USER_FOLDER_NAME));
        // An empty value is ignored.
        let documents = scratch("override-empty");
        let user = UserDirs::resolve(Some(OsString::new()), Some(documents.clone()), &data);
        assert_eq!(user.root(), documents.join(USER_FOLDER_NAME));
    }

    #[test]
    fn a_missing_or_relative_documents_folder_falls_back_to_the_data_folder() {
        let data = DataDirs::new(scratch("fallback-data"));
        for known in [None, Some(PathBuf::from("Documents")), Some(scratch("fallback").join("gone"))] {
            let user = UserDirs::resolve(None, known.clone(), &data);
            assert!(matches!(user.source(), UserRootSource::Fallback(_)), "{known:?}");
            assert_eq!(user.root(), data.root().join(USER_FOLDER_NAME));
        }
    }

    #[test]
    fn appdata_keeps_the_runtime_card_the_card_backups_and_the_marker() {
        let data = DataDirs::new("C:/data/GT4Recomp");
        assert_eq!(data.runtime_card(), Path::new("C:/data/GT4Recomp/runtime/card.ps2"));
        assert_eq!(data.card_backups(), Path::new("C:/data/GT4Recomp/library/backups"));
        assert_eq!(data.library_migration_marker(), Path::new("C:/data/GT4Recomp/config/library-migration.json"));
    }
}
