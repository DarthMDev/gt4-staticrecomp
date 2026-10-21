//! Where the launcher keeps its files. The program folder is replaced by updates; the data folder
//! is never touched by an install or uninstall unless the user asks for it.

use std::path::{Path, PathBuf};

/// Environment variable that moves the data folder (used by tests and portable experiments).
pub const DATA_DIR_OVERRIDE_VAR: &str = "GT4RECOMP_DATA_DIR";

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
}

/// Folder that holds `gt4vulkan.exe`: `<program folder>\game` for an installed copy, or the
/// `[dev] game_dir` override from the config file.
pub fn game_directory(program_directory: &Path, dev_override: Option<&Path>) -> PathBuf {
    match dev_override {
        Some(path) => path.to_path_buf(),
        None => program_directory.join("game"),
    }
}
