//! The launcher's commands other than setup: the snapshot, Settings, Play, Controls, Saves and
//! Replays, and About. Each command is a thin wrapper over a crate; the rules live in `model`,
//! `saves` and the `gt4l-*` crates.

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use gt4controls::{self as controls_file, default_bindings};
use gt4l_core::exit::{self, Outcome};
use gt4l_core::gs_release;
use gt4l_core::launch;
use gt4l_core::paths::{self, DataDirs, GAME_EXECUTABLE};
use gt4l_core::settings;
use gt4l_core::{Settings, LAUNCHER_VERSION};
use gt4l_disc::setup as disc_setup;
use gt4l_disc::Pins;
use gt4l_platform::{game, pad};
use gt4saves::library::Report;
use serde::Serialize;
use serde_json::{json, Value};
use tauri::{AppHandle, Emitter, State};
use tauri_plugin_dialog::DialogExt;

use crate::model;
use crate::saves::{self, Item};
use crate::state::{self, lock, AppState, Shared};

/// Everything the page needs to pick the screen and show the header of Home.
#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Snapshot {
    pub version: String,
    /// `setup` or `home`.
    pub screen: String,
    pub disc_name: String,
    pub home_message: String,
    pub game_running: bool,
    pub setup_busy: bool,
    /// Development builds only (`visual-check`): the page to show first.
    pub visual_page: Option<String>,
}

fn snapshot_of(shared: State<'_, Shared>) -> Result<Snapshot, String> {
    let state = lock(shared.inner());
    let ready = state.home_ready();
    let disc_name = state
        .disc_path()
        .and_then(|path| path.file_name())
        .map(|name| name.to_string_lossy().into_owned())
        .unwrap_or_default();
    Ok(Snapshot {
        version: LAUNCHER_VERSION.to_string(),
        screen: if ready || visual_ready() { "home" } else { "setup" }.to_string(),
        disc_name,
        home_message: state.home_message.clone(),
        game_running: state.game_child.is_some(),
        setup_busy: state.setup_busy,
        visual_page: visual_page(),
    })
}

/// `app_state()`: `{setup: {state, step, progress, disc_name, problem}, game_running}` (ui/dev/README.md).
/// Progress is 0 until the setup pipeline reports it (open item).
#[tauri::command]
pub fn app_state(shared: State<'_, Shared>) -> Result<Value, String> {
    let snapshot = snapshot_of(shared)?;
    let (state, step) = if snapshot.screen == "home" {
        ("done", "checking")
    } else if snapshot.setup_busy {
        ("checking", "preparing")
    } else {
        ("needed", "checking")
    };
    Ok(json!({
        "setup": {
            "state": state,
            "step": step,
            "progress": 0.0,
            "disc_name": snapshot.disc_name,
            "problem": snapshot.home_message,
        },
        "game_running": snapshot.game_running,
    }))
}

#[cfg(feature = "visual-check")]
fn visual_page() -> Option<String> {
    std::env::var("GT4RECOMP_VISUAL_PAGE").ok()
}

#[cfg(not(feature = "visual-check"))]
fn visual_page() -> Option<String> {
    None
}

#[cfg(feature = "visual-check")]
fn visual_ready() -> bool {
    std::env::var("GT4RECOMP_VISUAL_READY").is_ok_and(|value| value == "1")
}

#[cfg(not(feature = "visual-check"))]
fn visual_ready() -> bool {
    false
}

// Settings ------------------------------------------------------------------------------------

/// `get_settings_table()`: `{max_upscale, rows}`, the rows of ui/dev/README.md (see `model::settings_table`).
#[tauri::command]
pub fn get_settings_table(shared: State<'_, Shared>) -> Result<Value, String> {
    let state = lock(shared.inner());
    Ok(model::settings_table(&state.config.settings, &state.controls))
}

/// `set_setting({id, value})`: a launcher row, a PCSX2 GS table row or a trigger row (`l2_deadzone`, `r2_curve`, ...).
/// A refused value leaves the setting unchanged and returns the reason.
#[tauri::command]
pub fn set_setting(id: String, value: Value, shared: State<'_, Shared>) -> Result<(), String> {
    let mut state = lock(shared.inner());
    if let Some(definition) = settings::definition(&id) {
        let text = model::value_text(&value, definition.kind == settings::Kind::Toggle)
            .ok_or_else(|| format!("'{id}' needs a plain value"))?;
        state.config.settings.set(&id, &text).map_err(|error| error.to_string())?;
    } else if let Some((trigger, field)) = id.split_once('_').filter(|(name, _)| ["l2", "r2"].contains(name)) {
        let current = state.controls.trigger_tuning(trigger);
        let (deadzone, curve) = match field {
            "deadzone" => (
                value.as_f64().ok_or_else(|| format!("'{id}' needs a number"))? as f32,
                current.curve.as_str().to_string(),
            ),
            "curve" => (
                current.deadzone * 100.0,
                value.as_str().ok_or_else(|| format!("'{id}' needs a curve name"))?.to_string(),
            ),
            _ => return Err(format!("unknown setting '{id}'")),
        };
        let tuning = model::trigger_tuning(deadzone, &curve)?;
        state.controls.set_trigger_tuning(trigger, tuning).map_err(|error| error.to_string())?;
        state.save_controls();
        return Ok(());
    } else if model::pcsx2_table_rows().iter().any(|row| row.id == id) {
        let text = model::value_text(&value, false).ok_or_else(|| format!("'{id}' needs a plain value"))?;
        state.config.settings.set_gs(&id, &text).map_err(|error| error.to_string())?;
    } else {
        return Err(format!("unknown setting '{id}'"));
    }
    state.save_config();
    Ok(())
}

/// `reset_gs_settings_to_defaults()`: every PCSX2 GS table row back to its default. Returns the new table.
#[tauri::command]
pub fn reset_gs_settings_to_defaults(shared: State<'_, Shared>) -> Result<Value, String> {
    let mut state = lock(shared.inner());
    state.config.settings.clear_gs();
    state.save_config();
    Ok(model::settings_table(&state.config.settings, &state.controls))
}

// Play ------------------------------------------------------------------------------------------

/// Starts the game with the chosen settings. Returns once the process is running; the page hears
/// about the end through the `game-state` event.
#[tauri::command]
pub fn play(app: AppHandle, shared: State<'_, Shared>) -> Result<(), String> {
    let shared: Shared = shared.inner().clone();
    let (disc, pins, dirs, program_dir, dev_dir, settings) = {
        let state = lock(&shared);
        if state.game_child.is_some() {
            return Err("The game is already running.".into());
        }
        (
            state.config.disc_path.clone(),
            state.pins.clone(),
            state.dirs.clone(),
            program_directory(),
            state.config.dev_game_dir.clone(),
            state.config.settings.clone(),
        )
    };
    let Some(disc) = disc.filter(|path| disc_present(path, &pins)) else {
        lock(&shared).home_message = "Disc image not found or changed. Choose it again in setup.".into();
        return Err("disc".into());
    };
    if let Err(message) = disc_setup::verify_prepared(&dirs.prepared(), &pins) {
        return Err(format!("Game files are not ready ({message}). Prepare them again in setup."));
    }
    let game_dir = paths::game_directory(&program_dir, dev_dir.as_deref());
    let executable = game_dir.join(GAME_EXECUTABLE);
    if !executable.exists() {
        return Err("The game files are missing. Reinstall GT4Recomp.".into());
    }
    let mut extra: Vec<(String, String)> = Vec::new();
    let controls_path = dirs.root().join(gt4controls::CONTROLS_FILE_NAME);
    if controls_path.exists() {
        extra.push(("GT4_CONTROLS_FILE".into(), controls_path.display().to_string()));
    }
    let card = dirs.runtime_card();
    saves::prepare_card(&lock(&shared).library, &card)?;
    // Decision 0070: PCSX2 GS when it is installed and selected; otherwise CoreGS 1.1 with a message.
    let renderer_root = dirs.root().join("gs");
    let installed = gs_release::installed(&renderer_root);
    let (pcsx2, notice) = match launch::choose_renderer(&settings, installed.as_ref()) {
        launch::RendererChoice::Pcsx2 => (installed.clone(), None),
        launch::RendererChoice::CoreGs11 { reason } => (None, reason),
    };
    let elf = dirs.prepared().join(disc_setup::ELF_FILE);
    let running = game::start(&dirs, &executable, &elf, &disc, &settings, pcsx2.as_ref(), &extra)?;
    {
        let mut state = lock(&shared);
        state.game_child = Some(running.child.clone());
        state.home_message = notice.unwrap_or_default();
    }
    state::log(&dirs, &format!("game started, session {}", running.session_dir.display()));
    let _ = app.emit("game-state", json!({ "running": true, "message": "" }));

    let mut pcsx2_used = pcsx2.is_some();
    std::thread::spawn(move || {
        let mut running = running;
        let outcome = loop {
            let outcome = game::wait_for_exit(&running);
            let log_text = fs::read_to_string(&running.log_path).unwrap_or_default();
            if !(pcsx2_used && launch::restart_requested(&log_text)) {
                break outcome;
            }
            // The player chose "restart with the built-in renderer" after a PCSX2 GS failure (TASK-038).
            state::log(&dirs, "PCSX2 GS asked for a restart; starting CoreGS 1.1");
            drop(running);
            match game::start(&dirs, &executable, &elf, &disc, &settings, None, &extra) {
                Ok(next) => {
                    lock(&shared).game_child = Some(next.child.clone());
                    running = next;
                    pcsx2_used = false;
                }
                Err(message) => {
                    lock(&shared).game_child = None;
                    lock(&shared).home_message = message.clone();
                    let _ = app.emit("game-state", json!({ "running": false, "message": message }));
                    return;
                }
            }
        };
        match saves::harvest_card(&lock(&shared).library, &card) {
            Ok(report) => state::log(&dirs, &format!("card harvested: {report:?}")),
            Err(error) => state::log(&dirs, &format!("card not harvested: {error}")),
        }
        let log_path = running.log_path.clone();
        drop(running);
        lock(&shared).game_child = None;
        state::log(&dirs, &format!("game ended: {outcome:?} (log {})", log_path.display()));
        let message = match &outcome {
            Outcome::NormalClose => String::new(),
            Outcome::Crashed { .. } => exit::user_message(&outcome),
        };
        lock(&shared).home_message = message.clone();
        let _ = app.emit("game-state", json!({ "running": false, "message": message }));
    });
    Ok(())
}

/// Stops the game process. The card is harvested by the watcher thread after the exit.
#[tauri::command]
pub fn stop_game(shared: State<'_, Shared>) -> Result<(), String> {
    let child = lock(shared.inner()).game_child.clone();
    if let Some(child) = child {
        let mut guard = match child.lock() {
            Ok(guard) => guard,
            Err(poisoned) => poisoned.into_inner(),
        };
        if let Some(process) = guard.as_mut() {
            process.kill().map_err(|error| format!("could not stop the game: {error}"))?;
        }
    }
    Ok(())
}

fn program_directory() -> PathBuf {
    std::env::current_exe()
        .ok()
        .and_then(|path| path.parent().map(Path::to_path_buf))
        .unwrap_or_else(|| PathBuf::from("."))
}

/// Cheap check before Play: the file exists and has the pinned size (the full hash ran at setup).
fn disc_present(path: &Path, pins: &Pins) -> bool {
    fs::metadata(path).map(|meta| meta.len() == pins.disc.size_bytes).unwrap_or(false)
}

// Controls ----------------------------------------------------------------------------------------

// Controls ---------------------------------------------------------------------------------------

/// `get_controls()`: `{controls: [{id, name, binding}], triggers: [row]}`. Triggers use the settings row shape.
#[tauri::command]
pub fn get_controls(shared: State<'_, Shared>) -> Result<Value, String> {
    let state = lock(shared.inner());
    let controls: Vec<Value> = model::control_rows(&state.controls)
        .into_iter()
        .map(|row| json!({ "id": row.input, "name": row.ps2_name, "binding": row.short }))
        .collect();
    let table = model::settings_table(&state.config.settings, &state.controls);
    let triggers: Vec<Value> = table["rows"]
        .as_array()
        .map(|rows| {
            rows.iter()
                .filter(|row| row["id"].as_str().is_some_and(|id| id.starts_with("l2_") || id.starts_with("r2_")))
                .cloned()
                .collect()
        })
        .unwrap_or_default();
    Ok(json!({ "controls": controls, "triggers": triggers }))
}

/// Replaces the sources of one kind (`key:` or `xinput:`) of an input and keeps the other kind.
fn bind_source(state: &mut AppState, input: &str, prefix: &str, source: String) -> Result<(), String> {
    let mut sources: Vec<String> = state
        .controls
        .sources(input)
        .unwrap_or(&[])
        .iter()
        .filter(|existing| !existing.starts_with(prefix))
        .cloned()
        .collect();
    sources.push(source.clone());
    state.controls.set_sources(input, sources).map_err(|error| error.to_string())?;
    state.save_controls();
    state.controls_status = format!("{} is now bound to {}.", model::ps2_name(input), model::describe_source(&source));
    Ok(())
}

/// `capture_binding({control})`: waits for the next newly pressed controller button and binds it. Resolves with
/// `{control, binding}`. `cancel_capture` ends the wait with an error. A button held when the wait starts does not bind.
#[tauri::command]
pub async fn capture_binding(control: String, shared: State<'_, Shared>) -> Result<Value, String> {
    if !model::bindable(&control) {
        return Err(format!("'{control}' cannot be rebound"));
    }
    let shared: Shared = shared.inner().clone();
    let cancel = Arc::new(AtomicBool::new(false));
    {
        let mut state = lock(&shared);
        if let Some(previous) = state.capture_cancel.take() {
            previous.store(true, Ordering::Relaxed);
        }
        state.capturing = Some(control.clone());
        state.capture_cancel = Some(cancel.clone());
        state.controls_status = format!("Press a controller button for {}. Esc cancels.", model::ps2_name(&control));
    }
    tauri::async_runtime::spawn_blocking(move || {
        let baseline = pad::read_first_connected().unwrap_or_default();
        loop {
            if cancel.load(Ordering::Relaxed) {
                return Err("capture cancelled".to_string());
            }
            std::thread::sleep(Duration::from_millis(15));
            let Some(now) = pad::read_first_connected() else {
                continue;
            };
            let Some(source) = pad::captured_source(&baseline, &now, pad::Target::Button) else {
                continue;
            };
            let mut state = lock(&shared);
            if cancel.load(Ordering::Relaxed) || state.capturing.as_deref() != Some(control.as_str()) {
                return Err("capture cancelled".to_string());
            }
            state.capturing = None;
            state.capture_cancel = None;
            bind_source(&mut state, &control, "xinput:", source)?;
            let binding = model::control_rows(&state.controls)
                .into_iter()
                .find(|row| row.input == control)
                .map(|row| row.short)
                .unwrap_or_default();
            return Ok(json!({ "control": control, "binding": binding }));
        }
    })
    .await
    .map_err(|error| error.to_string())?
}

/// `cancel_capture()`: ends a running capture without binding anything.
#[tauri::command]
pub fn cancel_capture(shared: State<'_, Shared>) -> Result<(), String> {
    let mut state = lock(shared.inner());
    if let Some(cancel) = state.capture_cancel.take() {
        cancel.store(true, Ordering::Relaxed);
    }
    state.capturing = None;
    state.controls_status.clear();
    Ok(())
}

/// `reset_binding({control})`: restores the built-in sources of one input.
#[tauri::command]
pub fn reset_binding(control: String, shared: State<'_, Shared>) -> Result<(), String> {
    let mut state = lock(shared.inner());
    let defaults = default_bindings();
    let sources = defaults.get(&control).cloned().ok_or_else(|| format!("unknown input '{control}'"))?;
    state.controls.set_sources(&control, sources).map_err(|error| error.to_string())?;
    state.save_controls();
    state.controls_status = format!("{} is back to its built-in binding.", model::ps2_name(&control));
    Ok(())
}

// Saves and replays --------------------------------------------------------------------------------

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LibraryView {
    pub items: Vec<Item>,
    pub status: String,
    /// Replays only: the library folder, shown so a user can find the files.
    pub folder: String,
}

fn saves_view(state: &AppState) -> Result<LibraryView, String> {
    Ok(LibraryView {
        items: saves::saves(&state.library)?,
        status: state.saves_status.clone(),
        folder: state.library_root.display().to_string(),
    })
}

fn replays_view(state: &AppState) -> Result<LibraryView, String> {
    Ok(LibraryView {
        items: saves::replays(&state.library)?,
        status: state.replays_status.clone(),
        folder: state.library_root.display().to_string(),
    })
}

#[tauri::command]
pub fn saves_list(shared: State<'_, Shared>) -> Result<LibraryView, String> {
    saves_view(&lock(shared.inner()))
}

#[tauri::command]
pub fn replays_list(shared: State<'_, Shared>) -> Result<LibraryView, String> {
    replays_view(&lock(shared.inner()))
}

/// Picks a save file or card image and imports it into the library. Imports never overwrite.
#[tauri::command]
pub async fn saves_import(app: AppHandle, shared: State<'_, Shared>) -> Result<Option<LibraryView>, String> {
    import_with_picker(app, shared.inner().clone(), Kind::Saves).await
}

#[tauri::command]
pub async fn replays_import(app: AppHandle, shared: State<'_, Shared>) -> Result<Option<LibraryView>, String> {
    import_with_picker(app, shared.inner().clone(), Kind::Replays).await
}

#[derive(Clone, Copy)]
enum Kind {
    Saves,
    Replays,
}

async fn import_with_picker(app: AppHandle, shared: Shared, kind: Kind) -> Result<Option<LibraryView>, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let picked = app
            .dialog()
            .file()
            .set_title("Import from a memory card or save file")
            .add_filter("Save or card", &["psu", "ps2", "mcd", "bin", "card"])
            .blocking_pick_file();
        let Some(picked) = picked else {
            return Ok(None);
        };
        let path = picked.into_path().map_err(|error| error.to_string())?;
        let mut state = lock(&shared);
        let summary = match saves::import_path(&state.library, &path) {
            Ok(report) => summary_of(&report),
            Err(message) => message,
        };
        match kind {
            Kind::Saves => {
                state.saves_status = summary;
                saves_view(&state).map(Some)
            }
            Kind::Replays => {
                state.replays_status = summary;
                replays_view(&state).map(Some)
            }
        }
    })
    .await
    .map_err(|error| error.to_string())?
}

fn summary_of(report: &Report) -> String {
    saves::import_summary(report)
}

/// Makes a save the one the next start uses. The save that was active moves to a new slot.
#[tauri::command]
pub fn saves_activate(id: String, shared: State<'_, Shared>) -> Result<LibraryView, String> {
    let name = id;
    let mut state = lock(shared.inner());
    let slot = saves::saves(&state.library)?
        .into_iter()
        .find(|item| item.name == name)
        .map(|item| item.label)
        .ok_or_else(|| "That save is not in the library any more.".to_string())?;
    if slot == "Active save" {
        return saves_view(&state);
    }
    saves::activate(&state.library, &name)?;
    state.saves_status = format!("{} will be used the next time you play.", name);
    saves_view(&state)
}

/// Sends a save or replay to the Recycle Bin.
#[tauri::command]
pub fn saves_delete(id: String, shared: State<'_, Shared>) -> Result<LibraryView, String> {
    let name = id;
    delete_item(&name, shared.inner(), Kind::Saves)
}

#[tauri::command]
pub fn replays_delete(id: String, shared: State<'_, Shared>) -> Result<LibraryView, String> {
    let name = id;
    delete_item(&name, shared.inner(), Kind::Replays)
}

fn delete_item(name: &str, shared: &Shared, kind: Kind) -> Result<LibraryView, String> {
    let mut state = lock(shared);
    let items = saves::saves(&state.library)?.into_iter().chain(saves::replays(&state.library)?);
    let item = items
        .into_iter()
        .find(|item| item.name == name)
        .ok_or_else(|| "That item is not in the library any more.".to_string())?;
    let result = saves::delete(&state.library_root, &item);
    let status = match result {
        Ok(()) => format!("{} moved to the Recycle Bin.", item.label),
        Err(message) => message,
    };
    match kind {
        Kind::Saves => {
            state.saves_status = status;
            saves_view(&state)
        }
        Kind::Replays => {
            state.replays_status = status;
            replays_view(&state)
        }
    }
}

// About -----------------------------------------------------------------------------------------

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AboutInfo {
    pub text: String,
    pub licenses: String,
    pub disc_path: String,
    pub version: String,
}

#[tauri::command]
pub fn about_info(shared: State<'_, Shared>) -> Result<AboutInfo, String> {
    let state = lock(shared.inner());
    Ok(AboutInfo {
        text: model::about_text(LAUNCHER_VERSION),
        licenses: model::licenses_text().to_string(),
        disc_path: state.disc_path().map(|path| path.display().to_string()).unwrap_or_default(),
        version: LAUNCHER_VERSION.to_string(),
    })
}

/// Opens the log folder in Explorer. The path is fixed by the launcher; nothing from the page runs.
#[tauri::command]
pub fn open_log_folder(shared: State<'_, Shared>) -> Result<(), String> {
    let logs = lock(shared.inner()).dirs.logs();
    fs::create_dir_all(&logs).map_err(|error| error.to_string())?;
    #[cfg(windows)]
    {
        std::process::Command::new("explorer.exe")
            .arg(&logs)
            .spawn()
            .map(|_| ())
            .map_err(|error| format!("could not open {}: {error}", logs.display()))
    }
    #[cfg(not(windows))]
    {
        let _ = logs;
        Ok(())
    }
}

/// Keeps the setup helpers in one place for tests of the command layer.
pub fn data_dirs_for(root: &Path) -> DataDirs {
    DataDirs::new(root)
}

/// The settings a fresh launcher starts with (used by the command tests).
pub fn default_settings() -> Settings {
    Settings::default()
}

/// Controls file path for a data folder (used by the command tests).
pub fn controls_file_for(root: &Path) -> PathBuf {
    root.join(controls_file::CONTROLS_FILE_NAME)
}
