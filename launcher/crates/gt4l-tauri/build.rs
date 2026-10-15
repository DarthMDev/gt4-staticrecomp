//! Build script: generates the permissions of this app's own commands (see `capabilities/`), so the
//! page can call only the commands listed here. Nothing else in Tauri is enabled for the page.

/// Every command the page may call. Must match the `#[tauri::command]` functions in `src/`.
const COMMANDS: &[&str] = &[
    "app_state",
    "settings_rows",
    "settings_set",
    "setup_choose_disc",
    "setup_start",
    "setup_cancel",
    "play",
    "stop_game",
    "saves_list",
    "saves_import",
    "saves_activate",
    "saves_delete",
    "replays_list",
    "replays_import",
    "replays_delete",
    "controls_list",
    "controls_capture_start",
    "controls_capture_cancel",
    "controls_bind_key",
    "controls_reset",
    "controls_set_trigger",
    "about_info",
    "open_log_folder",
];

fn main() {
    let manifest = tauri_build::AppManifest::new().commands(COMMANDS);
    tauri_build::try_build(tauri_build::Attributes::new().app_manifest(manifest))
        .expect("Tauri build script failed");
}
