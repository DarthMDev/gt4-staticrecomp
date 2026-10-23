//! Setup commands: pick the disc image, then copy it, check it, prepare the game files and install the optional
//! PCSX2 GS renderer when selected (docs/launcher/FLOW-AUDIT.md). The work runs on a thread. The page polls
//! `app_state` (which carries the step, the fraction and the bytes) and also gets `setup-progress` and `setup-finished` events.
//!
//! The command bodies are plain functions over [`Shared`] (`choose_disc_path`, `start_with`) so the flow test
//! (`flow_audit.rs`) drives exactly the code the page drives, without a window.

use std::path::{Path, PathBuf};
use std::sync::atomic::Ordering;
use std::sync::Arc;

use gt4l_core::games::{self, Game, GT4, SPEC2};
use gt4l_core::gs_release::Step;
use gt4l_core::paths::DataDirs;
use gt4l_core::LAUNCHER_VERSION;
use gt4l_disc::identify::{identify, DiscGame};
use gt4l_disc::setup::{self as disc_setup, SetupError, SetupRequest, Stage};
use gt4l_disc::spec2::{self as disc_spec2, Spec2Request};
use serde::Serialize;
use serde_json::{json, Value};
use tauri::{AppHandle, Emitter, State};
use tauri_plugin_dialog::DialogExt;

use crate::state::{self, lock, SetupOutcome, SetupRun, Shared};
use crate::updates;

/// Sends an event to the page (`Emitter::emit` in the app, a recorder in tests).
pub type Notify = Arc<dyn Fn(&str, Value) + Send + Sync>;

/// Installs the newest gt4-gs, reporting steps; returns the installed version text.
pub type RendererInstaller = Arc<dyn Fn(&DataDirs, &mut dyn FnMut(Step)) -> Result<String, String> + Send + Sync>;

/// The page's event sink for an app handle.
pub fn notifier(app: &AppHandle) -> Notify {
    let app = app.clone();
    Arc::new(move |name: &str, payload: Value| {
        let _ = app.emit(name, payload);
    })
}

/// The real renderer install: GitHub releases of moesuito/gt4recomp-gs (see `updates::install_renderer`).
pub fn github_renderer() -> RendererInstaller {
    Arc::new(|dirs: &DataDirs, progress: &mut dyn FnMut(Step)| {
        updates::install_renderer(dirs, progress).map(|outcome| outcome.installed().version.text())
    })
}

/// What the page shows after a disc was accepted.
#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DiscInfo {
    pub path: String,
    pub serial: String,
    pub version: String,
    /// `Gran Turismo 4 · USA · v2.00`, or `Gran Turismo 4 Spec II · 1.10`.
    pub name: String,
    /// The game the disc is (`gt4`, `spec2`): the setup that follows installs this game.
    pub game: String,
    pub game_name: String,
    /// The page asked for another game (`requested`) than the disc is: it offers to install the disc as the game it is,
    /// or to pick another file. Nothing is installed until the page calls `setup_start`.
    pub mismatch: bool,
    pub requested: Option<String>,
}

/// The display name of an accepted Gran Turismo 4 disc.
pub fn disc_display_name(version: &str) -> String {
    format!("Gran Turismo 4 \u{b7} USA \u{b7} v{version}")
}

/// The display name of an accepted disc of `game` (`image_name` is the known image, for Spec II).
pub fn game_disc_name(game: &Game, version: &str, image_name: &str) -> String {
    match DiscGame::from_id(game.id) {
        Some(DiscGame::Spec2) if !image_name.is_empty() => format!("{} \u{b7} {image_name}", game.display_name),
        Some(DiscGame::Spec2) => format!("{} \u{b7} {version}", game.display_name),
        _ => disc_display_name(version),
    }
}

/// Opens the file picker for a plain `.iso` and checks it. `None` means the picker was cancelled.
/// A disc that is not accepted gives its plain-language reason as the error (and `app_state` says `wrong_disc`).
#[tauri::command]
pub async fn setup_choose_disc(app: AppHandle, game: Option<String>, shared: State<'_, Shared>) -> Result<Option<DiscInfo>, String> {
    let shared: Shared = shared.inner().clone();
    tauri::async_runtime::spawn_blocking(move || {
        if lock(&shared).setup_busy {
            return Err("Setup is already running.".to_string());
        }
        refuse_unsupported_system(&shared)?;
        let picked = app
            .dialog()
            .file()
            .set_title(match game.as_deref().and_then(games::by_id) {
                Some(requested) => format!("Choose your {} disc image", requested.display_name),
                None => "Choose your Gran Turismo 4 or Gran Turismo 4 Spec II disc image".to_string(),
            })
            .add_filter("PlayStation 2 disc image", &["iso"])
            .blocking_pick_file();
        let Some(picked) = picked else {
            return Ok(None);
        };
        let path: PathBuf = picked.into_path().map_err(|error| error.to_string())?;
        choose_disc_path_for(&shared, &path, game.as_deref()).map(Some)
    })
    .await
    .map_err(|error| error.to_string())?
}

/// The check behind `setup_choose_disc` for a path (the picker's result, or a path from the flow test).
pub fn choose_disc_path(shared: &Shared, path: &Path) -> Result<DiscInfo, String> {
    choose_disc_path_for(shared, path, None)
}

/// [`choose_disc_path`] when the page asked for a game (`requested`). The disc decides which game is installed; when it is
/// another game than the one asked for, the answer says so (`mismatch`) and the page offers to install it as that game.
pub fn choose_disc_path_for(shared: &Shared, path: &Path, requested: Option<&str>) -> Result<DiscInfo, String> {
    let (pins, spec2_pins) = {
        let state = lock(shared);
        if state.setup_busy {
            return Err("Setup is already running.".into());
        }
        if state.migration.running {
            return Err("GT4Recomp is moving its game files to the install folder. Wait until it has finished.".into());
        }
        (state.pins.clone(), state.spec2_pins.clone())
    };
    match identify(path, &pins, &spec2_pins) {
        Ok(found) => {
            let game: &'static Game = match found.game {
                DiscGame::Gt4 => &GT4,
                DiscGame::Spec2 => &SPEC2,
            };
            let summary = found.summary;
            let mut state = lock(shared);
            state.config.disc_path = Some(path.to_path_buf());
            state.setup_outcome = SetupOutcome::None;
            state.setup_game = Some(game);
            state.save_config();
            state::log(
                &state.dirs,
                &format!("disc accepted as {}: {} v{} ({})", game.id, summary.serial, summary.version, path.display()),
            );
            let requested_game = requested.and_then(games::by_id);
            Ok(DiscInfo {
                path: path.display().to_string(),
                name: game_disc_name(game, &summary.version, ""),
                serial: summary.serial,
                version: summary.version,
                game: game.id.to_string(),
                game_name: game.display_name.to_string(),
                mismatch: requested_game.is_some_and(|asked| asked != game),
                requested: requested_game.map(|asked| asked.id.to_string()),
            })
        }
        Err(error) => {
            let message = error.user_message();
            let mut state = lock(shared);
            state::log(&state.dirs, &format!("disc rejected ({}): {error}", path.display()));
            state.setup_outcome = if error.is_disc_refusal() {
                SetupOutcome::Refused(message.clone())
            } else {
                SetupOutcome::Failed(message.clone())
            };
            Err(message)
        }
    }
}

/// Starts the setup thread for the chosen disc. Returns at once; progress arrives through `app_state` and events.
#[tauri::command]
pub fn setup_start(app: AppHandle, shared: State<'_, Shared>) -> Result<(), String> {
    refuse_unsupported_system(shared.inner())?;
    start_with(shared.inner(), notifier(&app), github_renderer()).map(|_| ())
}

/// TASK-081: the processor and Vulkan checks before setup (`gt4l_core::system_check`). A problem is shown on the
/// setup page as a failed setup with its message, and logged.
fn refuse_unsupported_system(shared: &Shared) -> Result<(), String> {
    let Some(problem) = gt4l_platform::system::requirements_problem() else {
        return Ok(());
    };
    let mut state = lock(shared);
    state::log(&state.dirs, &format!("system requirement not met: {problem}"));
    state.setup_outcome = SetupOutcome::Failed(problem.clone());
    Err(problem)
}

/// The body of `setup_start`. Returns the thread so tests can wait for it.
pub fn start_with(
    shared: &Shared,
    notify: Notify,
    renderer: RendererInstaller,
) -> Result<std::thread::JoinHandle<()>, String> {
    let shared: Shared = shared.clone();
    let (disc, pins, spec2_pins, game, installed_before, dirs, cancel) = {
        let mut state = lock(&shared);
        if state.setup_busy {
            return Err("Setup is already running.".into());
        }
        if state.migration.running {
            return Err("GT4Recomp is moving its game files to the install folder. Wait until it has finished.".into());
        }
        let Some(disc) = state.config.disc_path.clone() else {
            return Err("Choose your disc image first.".into());
        };
        let Some(game) = state.setup_game else {
            return Err("Choose your disc image first.".into());
        };
        let installed_before = state.installed_games();
        state.setup_busy = true;
        state.setup_outcome = SetupOutcome::None;
        state.setup_run = SetupRun { step: "copying".into(), ..SetupRun::default() };
        state.setup_cancel.store(false, Ordering::Relaxed);
        (disc, state.pins.clone(), state.spec2_pins.clone(), game, installed_before, state.game_dirs(game), state.setup_cancel.clone())
    };
    let handle = std::thread::spawn(move || {
        // Setup always writes the data folder's root, even when older game files are read from another folder.
        let prepared = dirs.setup_prepared();
        let staging = dirs.prepared_staging();
        let disc_folder = dirs.setup_disc_folder();
        let total_bytes = std::fs::metadata(&disc).map(|meta| meta.len()).unwrap_or(0);
        let mut last: (Stage, i32) = (Stage::Copying, -1);
        let progress_shared = shared.clone();
        let progress_notify = notify.clone();
        let mut report = |stage: Stage, fraction: f32| {
            let permille = (fraction * 1000.0) as i32;
            if (stage, permille) == last {
                return;
            }
            last = (stage, permille);
            let step = match stage {
                Stage::Copying => "copying",
                Stage::Checking => "checking",
                Stage::Preparing => "preparing",
            };
            let bytes_done = if stage == Stage::Copying { (fraction as f64 * total_bytes as f64) as u64 } else { 0 };
            {
                let mut state = lock(&progress_shared);
                state.setup_run.step = step.into();
                state.setup_run.fraction = fraction;
                state.setup_run.bytes_done = bytes_done;
                state.setup_run.bytes_total = if stage == Stage::Copying { total_bytes } else { 0 };
            }
            if permille % 10 == 0 || fraction >= 1.0 {
                progress_notify(
                    "setup-progress",
                    json!({ "step": step, "percent": permille / 10, "bytesDone": bytes_done, "bytesTotal": total_bytes }),
                );
            }
        };
        let result: Result<disc_setup::Receipt, SetupError> = match DiscGame::from_id(game.id) {
            Some(DiscGame::Spec2) => disc_spec2::run(
                &Spec2Request {
                    disc: &disc,
                    disc_folder: &disc_folder,
                    prepared: &prepared,
                    staging: &staging,
                    pins: &spec2_pins,
                    launcher_version: LAUNCHER_VERSION,
                    cancel: &cancel,
                    free_space: &gt4l_platform::disk::free_space,
                },
                &mut report,
            ),
            _ => disc_setup::run(
                &SetupRequest {
                    disc: &disc,
                    disc_folder: &disc_folder,
                    prepared: &prepared,
                    staging: &staging,
                    pins: &pins,
                    launcher_version: LAUNCHER_VERSION,
                    cancel: &cancel,
                    free_space: &gt4l_platform::disk::free_space,
                },
                &mut report,
            ),
        };
        let receipt = match result {
            Ok(receipt) => receipt,
            Err(error) => {
                state::log(&dirs, &format!("setup stopped: {error}"));
                let message = error.user_message();
                let mut state = lock(&shared);
                state.setup_busy = false;
                state.setup_outcome = if error.is_disc_refusal() {
                    SetupOutcome::Refused(message.clone())
                } else {
                    SetupOutcome::Failed(message.clone())
                };
                drop(state);
                notify("setup-finished", json!({ "ok": false, "message": message }));
                return;
            }
        };
        state::log(
            &dirs,
            &format!(
                "setup complete ({}): {} v{} image {} ({}; known image: {}) elf {}",
                game.id,
                receipt.disc.serial,
                receipt.disc.version,
                receipt.image.as_ref().map(|image| image.file.as_str()).unwrap_or("?"),
                if receipt.image_name.is_empty() { "-" } else { receipt.image_name.as_str() },
                receipt.qualified_dump,
                receipt.elf.sha256
            ),
        );
        if game.id == SPEC2.id && !receipt.qualified_dump {
            state::log(&dirs, "disc is accepted as a Spec II 1.10 variant: its executable is the known one but the image hash is not pinned");
        }

        let renderer_note = prepare_renderer(&shared, &dirs, &notify, &renderer);
        {
            let mut state = lock(&shared);
            state.setup_busy = false;
            state.setup_run.renderer_step = if renderer_note.is_empty() { "done" } else { "failed" }.into();
            state.home_message = renderer_note.clone();
            state.setup_outcome = SetupOutcome::Done { renderer_note };
            // The first game installed is the stored choice, and the page stays on the game just set up.
            state.config.selected_game = games::after_install(state.config.selected_game.as_deref(), &installed_before, game);
            state.selected = game;
            state.save_config();
        }
        notify("setup-finished", json!({ "ok": true, "message": "" }));
    });
    Ok(handle)
}

/// Installs GSdx, the only renderer (TASK-096), unless it is already there. The returned text is the setup note (empty
/// when there is nothing to report).
fn prepare_renderer(shared: &Shared, dirs: &DataDirs, notify: &Notify, renderer: &RendererInstaller) -> String {
    // TASK-088 (1.2.0): a GSdx that is installed and meets the minimum (for example a local package) is kept, with no
    // download attempt and no misleading note.
    if !gt4l_core::gs_release::needs_install(&dirs.renderer_root()) {
        state::log(dirs, "GSdx already installed; setup keeps it");
        return String::new();
    }
    {
        let mut state = lock(shared);
        state.setup_run = SetupRun { step: "renderer".into(), renderer_step: "listing".into(), ..SetupRun::default() };
    }
    let renderer_shared = shared.clone();
    let renderer_notify = notify.clone();
    let mut last_percent = -1i64;
    let result = renderer(dirs, &mut |step| {
        let mut state = lock(&renderer_shared);
        match step {
            Step::Transfer { done, total } => {
                state.setup_run.renderer_step = "downloading".into();
                state.setup_run.bytes_done = done;
                state.setup_run.bytes_total = total.unwrap_or(0);
                state.setup_run.fraction = total.filter(|total| *total > 0).map(|total| done as f32 / total as f32).unwrap_or(0.0);
                let percent = (state.setup_run.fraction * 100.0) as i64;
                if percent != last_percent {
                    last_percent = percent;
                    renderer_notify(
                        "renderer-progress",
                        json!({ "step": "downloading", "percent": percent, "bytesDone": done, "bytesTotal": total }),
                    );
                }
            }
            other => {
                let name = updates::step_name(other);
                state.setup_run.renderer_step = name.into();
                if other == Step::Installing {
                    state.setup_run.fraction = 1.0;
                }
                renderer_notify("renderer-progress", json!({ "step": name }));
            }
        }
    });
    match result {
        Ok(version) => {
            notify("renderer-progress", json!({ "step": "done", "version": version }));
            String::new()
        }
        Err(message) => {
            state::log(dirs, &format!("GSdx not installed during setup: {message}"));
            notify("renderer-progress", json!({ "step": "failed", "message": message }));
            "GSdx could not be downloaded, so the game cannot start yet. GT4Recomp tries again at the next start."
                .to_string()
        }
    }
}

/// Asks the setup thread to stop after its current chunk. The copy and the staging folder are removed.
#[tauri::command]
pub fn setup_cancel(shared: State<'_, Shared>) -> Result<(), String> {
    cancel_with(shared.inner());
    Ok(())
}

pub fn cancel_with(shared: &Shared) {
    lock(shared).setup_cancel.store(true, Ordering::Relaxed);
}
