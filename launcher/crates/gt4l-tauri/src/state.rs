//! Launcher state shared by the commands: settings, controls, the save library and the running game.
//! The state lives behind one mutex. Long jobs (setup, the game, controller capture) run on their
//! own threads and lock the state only to read or write their results.

use std::fs::{self, OpenOptions};
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
    /// Progress of the running setup (the page polls it through `app_state`).
    pub setup_run: SetupRun,
    /// How the last pick or setup run ended, until the next one starts.
    pub setup_outcome: SetupOutcome,
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
    /// True when the controls file was written by a newer launcher: it is kept as it is (never overwritten).
    pub controls_locked: bool,
}

pub type Shared = Arc<Mutex<AppState>>;

/// The step setup is on and how far it is. `step` is `copying`, `checking`, `preparing` or `renderer`.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct SetupRun {
    pub step: String,
    /// 0..1 of the current step.
    pub fraction: f32,
    /// Bytes done and total of a copy or a download (0 when not known).
    pub bytes_done: u64,
    pub bytes_total: u64,
    /// The renderer sub-step: `listing`, `downloading`, `installing`, `done` or `failed`.
    pub renderer_step: String,
}

/// The result of the last disc pick or setup run.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub enum SetupOutcome {
    #[default]
    None,
    /// The chosen file is not a supported disc (the page says "Not supported").
    Refused(String),
    /// The run stopped (cancel, no space, write error).
    Failed(String),
    /// The run finished. `renderer_note` is set when the PCSX2 GS could not be installed.
    Done { renderer_note: String },
}

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
        let pins = Pins::embedded()?;
        AppState::load_with_pins(dirs, pins)
    }

    /// [`AppState::load`] with given pins (the flow test uses synthetic pins for a synthetic disc).
    pub fn load_with_pins(dirs: DataDirs, pins: Pins) -> Result<AppState, String> {
        std::fs::create_dir_all(dirs.root()).map_err(|error| format!("data folder: {error}"))?;
        let (config, warnings) = config::load(&dirs.config_file());
        for warning in &warnings {
            log(&dirs, &format!("settings: {warning}"));
        }
        let controls_path = dirs.root().join(CONTROLS_FILE_NAME);
        let mut controls_locked = false;
        let controls = match controls_file::load(&controls_path) {
            Ok(Some(controls)) => controls,
            Ok(None) => Controls::default(),
            Err(message) => {
                // A file from a newer launcher is kept as it is. Any other damaged file gets a copy before it is
                // replaced by the defaults (docs/launcher/MIGRATIONS.md).
                let text = fs::read_to_string(&controls_path).unwrap_or_default();
                if controls_schema_is_newer(&text) {
                    controls_locked = true;
                    log(&dirs, &format!("controls: {message}; the file is from a newer launcher and is kept"));
                } else if controls_path.exists() {
                    let copy = controls_path.with_file_name(format!("{CONTROLS_FILE_NAME}.bak-corrupt-{}", unix_seconds()));
                    let _ = fs::copy(&controls_path, &copy);
                    log(&dirs, &format!("controls: {message}; copy kept at {}", copy.display()));
                } else {
                    log(&dirs, &format!("controls: {message}; built-in bindings used"));
                }
                Controls::default()
            }
        };
        let (library_root, library) = saves::open(dirs.root())?;
        Ok(AppState {
            dirs,
            pins,
            config,
            controls,
            library_root,
            library,
            setup_cancel: Arc::new(AtomicBool::new(false)),
            setup_busy: false,
            setup_run: SetupRun::default(),
            setup_outcome: SetupOutcome::None,
            game_child: None,
            capturing: None,
            capture_cancel: None,
            home_message: String::new(),
            controls_status: String::new(),
            saves_status: String::new(),
            replays_status: String::new(),
            controls_locked,
        })
    }

    /// The cheap check of the copied disc image and the prepared files (no hash; see `gt4l_disc::setup`).
    pub fn installed(&self) -> disc_setup::Installed {
        disc_setup::check_installed(&self.dirs.prepared(), &self.dirs.disc_folder(), &self.pins)
    }

    /// Home is ready when setup is not running and the copied image and prepared files pass the cheap check.
    pub fn home_ready(&self) -> bool {
        !self.setup_busy && matches!(self.installed(), disc_setup::Installed::Ready { .. })
    }

    pub fn save_config(&self) {
        if let Err(error) = config::save(&self.dirs.config_file(), &self.config) {
            log(&self.dirs, &format!("settings not saved: {error}"));
        }
    }

    /// The controls file. Commands write it through `commands::change_controls`, which refuses while
    /// `controls_locked` is set and keeps the change only when the write succeeded.
    pub fn controls_path(&self) -> PathBuf {
        self.dirs.root().join(CONTROLS_FILE_NAME)
    }

    pub fn disc_path(&self) -> Option<&Path> {
        self.config.disc_path.as_deref()
    }
}

/// Whether a controls file declares a schema above the one this launcher reads (`schema = 1`).
fn controls_schema_is_newer(text: &str) -> bool {
    text.parse::<toml::Table>()
        .ok()
        .and_then(|table| table.get("schema").and_then(|value| value.as_integer()))
        .is_some_and(|schema| schema > 1)
}

fn unix_seconds() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|value| value.as_secs()).unwrap_or(0)
}
