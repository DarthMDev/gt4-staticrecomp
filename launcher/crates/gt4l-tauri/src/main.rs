//! GT4Recomp launcher (Tauri 2). Builds `GT4Recomp.exe`. The page is `ui/` (plain HTML, CSS and JS,
//! bundled; no remote content). The Rust side holds all state and talks to the game through the
//! `gt4l-*` crates. See `docs/launcher/LAUNCHER-PLAN.md` section 15.

#![cfg_attr(all(not(debug_assertions), windows), windows_subsystem = "windows")]

mod commands;
mod model;
mod saves;
mod setup;
mod state;
#[cfg(feature = "visual-check")]
mod visual;

use std::sync::{Arc, Mutex};

use gt4l_core::paths::DataDirs;
use gt4l_core::LAUNCHER_VERSION;

fn main() {
    // A second launcher brings the first window forward instead of starting.
    if !gt4l_platform::process::claim_single_instance() {
        return;
    }
    let Some(dirs) = DataDirs::from_environment() else {
        eprintln!("LOCALAPPDATA is not set and GT4RECOMP_DATA_DIR is not given");
        return;
    };
    let _ = std::fs::create_dir_all(dirs.root());
    state::log(&dirs, &format!("GT4Recomp {LAUNCHER_VERSION} starting"));
    if let Err(message) = gt4l_platform::process::join_kill_on_close_job() {
        state::log(&dirs, &format!("the game would not end with the launcher: {message}"));
    }
    let app_state = match state::AppState::load(dirs.clone()) {
        Ok(app_state) => app_state,
        Err(message) => {
            state::log(&dirs, &format!("the launcher could not start: {message}"));
            return;
        }
    };
    #[cfg(feature = "visual-check")]
    visual::seed(&app_state);

    let shared: state::Shared = Arc::new(Mutex::new(app_state));
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(shared)
        .invoke_handler(tauri::generate_handler![
            commands::app_state,
            commands::settings_rows,
            commands::settings_set,
            setup::setup_choose_disc,
            setup::setup_start,
            setup::setup_cancel,
            commands::play,
            commands::stop_game,
            commands::saves_list,
            commands::saves_import,
            commands::saves_activate,
            commands::saves_delete,
            commands::replays_list,
            commands::replays_import,
            commands::replays_delete,
            commands::controls_list,
            commands::controls_capture_start,
            commands::controls_capture_cancel,
            commands::controls_bind_key,
            commands::controls_reset,
            commands::controls_set_trigger,
            commands::about_info,
            commands::open_log_folder,
        ])
        .run(tauri::generate_context!())
        .expect("error while running GT4Recomp");
}
