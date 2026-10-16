//! Setup commands: pick the disc image, prepare the game files (with progress and cancel events).
//! The work runs on a thread; the page hears about it through the `setup-progress` and
//! `setup-finished` events.

use std::path::PathBuf;
use std::sync::atomic::Ordering;

use gt4l_core::gs_release;
use gt4l_core::LAUNCHER_VERSION;
use gt4l_disc::setup::{self as disc_setup, SetupRequest, Stage};
use serde::Serialize;
use serde_json::json;
use tauri::{AppHandle, Emitter, State};
use tauri_plugin_dialog::DialogExt;

use crate::state::{self, lock, Shared};

/// What the page shows after a disc was accepted.
#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DiscInfo {
    pub path: String,
    pub serial: String,
    pub version: String,
}

/// Opens the file picker for a plain `.iso` and checks it. `None` means the picker was cancelled.
/// A disc that is not accepted gives its plain-language reason as the error.
#[tauri::command]
pub async fn setup_choose_disc(app: AppHandle, shared: State<'_, Shared>) -> Result<Option<DiscInfo>, String> {
    let shared: Shared = shared.inner().clone();
    tauri::async_runtime::spawn_blocking(move || {
        let picked = app
            .dialog()
            .file()
            .set_title("Choose your Gran Turismo 4 disc image")
            .add_filter("PlayStation 2 disc image", &["iso"])
            .blocking_pick_file();
        let Some(picked) = picked else {
            return Ok(None);
        };
        let path: PathBuf = picked.into_path().map_err(|error| error.to_string())?;
        let pins = lock(&shared).pins.clone();
        match disc_setup::precheck(&path, &pins) {
            Ok(summary) => {
                let mut state = lock(&shared);
                state.config.disc_path = Some(path.clone());
                state.save_config();
                Ok(Some(DiscInfo {
                    path: path.display().to_string(),
                    serial: summary.serial,
                    version: summary.version,
                }))
            }
            Err(error) => {
                state::log(&lock(&shared).dirs, &format!("disc rejected: {error}"));
                Err(error.user_message())
            }
        }
    })
    .await
    .map_err(|error| error.to_string())?
}

/// Starts the setup thread for the chosen disc. Returns at once; progress arrives as events.
#[tauri::command]
pub fn setup_start(app: AppHandle, shared: State<'_, Shared>) -> Result<(), String> {
    let shared: Shared = shared.inner().clone();
    let (disc, pins, dirs, cancel) = {
        let mut state = lock(&shared);
        if state.setup_busy {
            return Err("Setup is already running.".into());
        }
        let Some(disc) = state.config.disc_path.clone() else {
            return Err("Choose your disc image first.".into());
        };
        state.setup_busy = true;
        state.setup_cancel.store(false, Ordering::Relaxed);
        (disc, state.pins.clone(), state.dirs.clone(), state.setup_cancel.clone())
    };
    // TASK-043: the PCSX2 GS renderer is downloaded with the setup (decision 0070). A failure is logged and the
    // home message says CoreGS 1.1 is used instead; the disc setup is not affected.
    let gs_root = dirs.root().join("gs");
    let gs_dirs = dirs.clone();
    let gs_shared = shared.clone();
    std::thread::spawn(move || match gs_release::install_latest(&gs_release::GithubSource, &gs_root) {
        Ok(outcome) => state::log(&gs_dirs, &format!("PCSX2 GS ready: {outcome:?}")),
        Err(message) => {
            state::log(&gs_dirs, &format!("PCSX2 GS not installed: {message}"));
            lock(&gs_shared).home_message = format!("PCSX2 GS could not be installed ({message}). CoreGS 1.1 will be used.");
        }
    });
    std::thread::spawn(move || {
        let prepared = dirs.prepared();
        let staging = dirs.prepared_staging();
        let mut last_percent = -1;
        let progress_app = app.clone();
        let result = disc_setup::run(
            &SetupRequest {
                disc: &disc,
                prepared: &prepared,
                staging: &staging,
                pins: &pins,
                launcher_version: LAUNCHER_VERSION,
                cancel: &cancel,
            },
            &mut |stage, fraction| {
                let percent = (fraction * 100.0) as i32;
                if stage == Stage::Checking && percent == last_percent {
                    return;
                }
                last_percent = percent;
                let stage_name = match stage {
                    Stage::Checking => "checking",
                    Stage::Preparing => "preparing",
                };
                let _ = progress_app.emit("setup-progress", json!({ "stage": stage_name, "percent": percent }));
            },
        );
        let (ok, message) = match result {
            Ok(receipt) => {
                state::log(&dirs, &format!("setup complete: {} elf {}", receipt.disc.serial, receipt.elf.sha256));
                (true, String::new())
            }
            Err(error) => {
                state::log(&dirs, &format!("setup stopped: {error}"));
                (false, error.user_message())
            }
        };
        lock(&shared).setup_busy = false;
        let _ = app.emit("setup-finished", json!({ "ok": ok, "message": message }));
    });
    Ok(())
}

/// Asks the setup thread to stop after its current step. Nothing is changed on disk.
#[tauri::command]
pub fn setup_cancel(shared: State<'_, Shared>) -> Result<(), String> {
    lock(shared.inner()).setup_cancel.store(true, Ordering::Relaxed);
    Ok(())
}
