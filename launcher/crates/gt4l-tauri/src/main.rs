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
mod relocation;
mod renderer_switch;
mod saves;
mod setup;
mod state;
mod tray;
mod updates;
#[cfg(feature = "visual-check")]
mod visual;

use std::sync::{Arc, Mutex};

use tauri::Manager;

use gt4l_core::LAUNCHER_VERSION;

fn main() {
    // A second launcher brings the first window forward instead of starting.
    if !gt4l_platform::process::claim_single_instance() {
        return;
    }
    // TASK-091: large data in `<exe dir>\data` (portable), user data in `%APPDATA%\GT4Recomp`. The 1.2.0 user
    // data moves here, before the window; the large data moves in the background below.
    let exe_dir = updates::install_directory();
    let startup = match relocation::prepare(&exe_dir) {
        Ok(startup) => startup,
        Err(message) => {
            eprintln!("GT4Recomp cannot find its folders: {message}");
            return;
        }
    };
    let dirs = startup.layout.data.clone();
    let _ = std::fs::create_dir_all(dirs.root());
    state::log(&dirs, &format!("GT4Recomp {LAUNCHER_VERSION} starting from {}", exe_dir.display()));
    for line in &startup.log {
        state::log(&dirs, line);
    }
    for line in relocation::clean_old_install(&exe_dir) {
        state::log(&dirs, &line);
    }
    if let Err(message) = gt4l_platform::process::join_kill_on_close_job() {
        state::log(&dirs, &format!("the game would not end with the launcher: {message}"));
    }
    let mut app_state = match state::AppState::load(dirs.clone(), startup.user.clone(), startup.layout.legacy.local.clone()) {
        Ok(app_state) => app_state,
        Err(message) => {
            state::log(&dirs, &format!("the launcher could not start: {message}"));
            return;
        }
    };
    if let Some((notice, problem)) = startup.notice.clone() {
        app_state.migration.notice = notice;
        app_state.migration.problem = problem;
    }
    let legacy = startup.layout.legacy.clone();
    #[cfg(feature = "visual-check")]
    visual::seed(&app_state);

    let shared: state::Shared = Arc::new(Mutex::new(app_state));
    let dirs_for_setup = dirs.clone();
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(shared)
        .setup(move |app| {
            tray::install(app)?;
            // The gamepad service (SDL3) runs on its own thread for the whole session.
            let pads = pad_service::spawn(app.handle().clone());
            app.manage(pads);
            // Every launch: version file, first gt4-gs install if needed, and the update check (never blocks Play).
            let launch_shared = app.state::<state::Shared>().inner().clone();
            let launch_app = app.handle().clone();
            // The 1.2.0 large data moves first (Play and setup wait for it), then the version file and update check.
            std::thread::spawn(move || {
                relocation::move_large_data(&launch_shared, &legacy);
                updates::check_on_launch(launch_app, launch_shared);
            });
            // TASK-081: probe the processor and Vulkan once in the background, so Play does not wait for the driver
            // to load; a problem is logged here and shown when the player starts setup or Play.
            let check_dirs = dirs_for_setup.clone();
            std::thread::spawn(move || match gt4l_platform::system::requirements_problem() {
                Some(problem) => state::log(&check_dirs, &format!("system requirement not met: {problem}")),
                None => state::log(&check_dirs, "system requirements: processor and Vulkan OK"),
            });
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
            commands::open_player_folder,
            commands::open_saves_folder,
            commands::open_replays_folder,
            commands::open_textures_folder,
            updates::check_updates,
            updates::apply_update,
            updates::skip_update,
            tray::confirm_close,
        ])
        .run(tauri::generate_context!())
        .expect("error while running GT4Recomp");
}
