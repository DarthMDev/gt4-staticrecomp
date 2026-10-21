//! GT4Recomp launcher (Tauri 2). Builds `GT4Recomp.exe`. The page is `ui/` (plain HTML, CSS and JS,
//! bundled; no remote content). The Rust side holds all state and talks to the game through the
//! `gt4l-*` crates. See `docs/launcher/LAUNCHER-PLAN.md` section 15.

#![cfg_attr(all(not(debug_assertions), windows), windows_subsystem = "windows")]

mod commands;
#[cfg(test)]
mod contract;
#[cfg(test)]
mod flow_audit;
mod model;
mod pad_nav;
mod pad_service;
mod saves;
mod setup;
mod state;
mod tray;
mod updates;
#[cfg(feature = "visual-check")]
mod visual;

use std::sync::{Arc, Mutex};

use tauri::Manager;

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
        .setup(|app| {
            tray::install(app)?;
            // The gamepad service (SDL3) runs on its own thread for the whole session.
            let pads = pad_service::spawn(app.handle().clone());
            app.manage(pads);
            // Every launch: version file, first gt4-gs install if needed, and the update check (never blocks Play).
            let launch_shared = app.state::<state::Shared>().inner().clone();
            let launch_app = app.handle().clone();
            std::thread::spawn(move || updates::check_on_launch(launch_app, launch_shared));
            Ok(())
        })
        .on_window_event(|window, event| {
            // The close button ends the launcher, or asks first while the game runs. It never goes to the tray.
            if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                if window.label() == "main" {
                    api.prevent_close();
                    tray::handle(window.app_handle(), tray::Event::CloseRequested);
                }
            }
        })
        .invoke_handler(tauri::generate_handler![
            commands::app_state,
            commands::get_settings_table,
            commands::set_setting,
            commands::reset_gs_settings_to_defaults,
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
            commands::get_controls,
            commands::pad_status,
            commands::capture_binding,
            commands::cancel_capture,
            commands::reset_binding,
            commands::assign_binding,
            commands::reset_all_bindings,
            commands::about_info,
            commands::open_log_folder,
            commands::open_game_files_folder,
            updates::check_updates,
            updates::apply_update,
            updates::skip_update,
            tray::confirm_close,
        ])
        .run(tauri::generate_context!())
        .expect("error while running GT4Recomp");
}
