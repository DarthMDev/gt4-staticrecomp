//! Launcher state shared by the commands: settings, controls, the save library and the running game.
//! The state lives behind one mutex. Long jobs (setup, the game, controller capture) run on their
//! own threads and lock the state only to read or write their results.

use std::fs::OpenOptions;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::Child;
use std::sync::atomic::AtomicBool;
use std::sync::{Arc, Mutex, MutexGuard};
use std::time::{SystemTime, UNIX_EPOCH};

use gt4controls::{self as controls_file, Controls, CONTROLS_FILE_NAME};
use gt4l_core::config::{self, ConfigFile};
use gt4l_core::paths::DataDirs;
use gt4l_disc::setup as disc_setup;
use gt4l_disc::Pins;
use gt4saves::library::Library;

use crate::saves;

pub struct AppState {
    pub dirs: DataDirs,
    pub pins: Pins,
    pub config: ConfigFile,
    pub controls: Controls,
    pub library_root: PathBuf,
    pub library: Library,
    /// Set by the Cancel button of setup; the setup thread checks it between steps.
    pub setup_cancel: Arc<AtomicBool>,
    pub setup_busy: bool,
    /// The running game's child process (shared with the watcher thread and Stop).
    pub game_child: Option<Arc<Mutex<Option<Child>>>>,
    /// Input name while a controller or key capture waits for a press.
    pub capturing: Option<String>,
    /// Cancels the XInput polling thread of the current capture.
    pub capture_cancel: Option<Arc<AtomicBool>>,
    /// Message shown on Home, e.g. why the last start failed.
    pub home_message: String,
    /// Status line of the Controls page.
    pub controls_status: String,
    /// Result of the last import, activate or delete on the Saves page.
    pub saves_status: String,
    /// Result of the last import or delete on the Replays page.
    pub replays_status: String,
}

pub type Shared = Arc<Mutex<AppState>>;

/// Locks the state; a poisoned lock still gives its data (a panicked job must not hang the UI).
pub fn lock(shared: &Shared) -> MutexGuard<'_, AppState> {
    match shared.lock() {
        Ok(guard) => guard,
        Err(poisoned) => poisoned.into_inner(),
    }
}

/// Appends one line to the launcher log in the data folder.
pub fn log(dirs: &DataDirs, message: &str) {
    let seconds = SystemTime::now().duration_since(UNIX_EPOCH).map(|value| value.as_secs()).unwrap_or(0);
    let _ = std::fs::create_dir_all(dirs.logs());
    if let Ok(mut file) = OpenOptions::new().create(true).append(true).open(dirs.logs().join("launcher.log")) {
        let _ = writeln!(file, "[{seconds}] {message}");
    }
}

impl AppState {
    /// Loads the settings, controls and save library from the data folder.
    pub fn load(dirs: DataDirs) -> Result<AppState, String> {
        std::fs::create_dir_all(dirs.root()).map_err(|error| format!("data folder: {error}"))?;
        let (config, warnings) = config::load(&dirs.config_file());
        for warning in &warnings {
            log(&dirs, &format!("settings: {warning}"));
        }
        let controls = match controls_file::load(&dirs.root().join(CONTROLS_FILE_NAME)) {
            Ok(Some(controls)) => controls,
            Ok(None) => Controls::default(),
            Err(message) => {
                log(&dirs, &format!("controls: {message}; built-in bindings used"));
                Controls::default()
            }
        };
        let (library_root, library) = saves::open(dirs.root())?;
        let pins = Pins::embedded()?;
        Ok(AppState {
            dirs,
            pins,
            config,
            controls,
            library_root,
            library,
            setup_cancel: Arc::new(AtomicBool::new(false)),
            setup_busy: false,
            game_child: None,
            capturing: None,
            capture_cancel: None,
            home_message: String::new(),
            controls_status: String::new(),
            saves_status: String::new(),
            replays_status: String::new(),
        })
    }

    /// Home is ready when the chosen disc still exists and the prepared game files verify.
    pub fn home_ready(&self) -> bool {
        self.config.disc_path.as_ref().is_some_and(|disc| disc.exists())
            && disc_setup::verify_prepared(&self.dirs.prepared(), &self.pins).is_ok()
    }

    pub fn save_config(&self) {
        if let Err(error) = config::save(&self.dirs.config_file(), &self.config) {
            log(&self.dirs, &format!("settings not saved: {error}"));
        }
    }

    pub fn save_controls(&self) {
        let path = self.dirs.root().join(CONTROLS_FILE_NAME);
        if let Err(error) = controls_file::save(&path, &self.controls) {
            log(&self.dirs, &format!("controls not saved: {error}"));
        }
    }

    pub fn controls_path(&self) -> PathBuf {
        self.dirs.root().join(CONTROLS_FILE_NAME)
    }

    pub fn disc_path(&self) -> Option<&Path> {
        self.config.disc_path.as_deref()
    }
}
