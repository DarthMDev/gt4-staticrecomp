//! Switching the renderer in Settings (TASK-089 extension, release 1.2.0). Choosing GSdx when it is not installed
//! installs it at once, with the progress of the renderer install (`renderer-progress`). A failed install puts the
//! choice back to CoreGS 1.1, so the Settings page, the saved setting and Play agree, and the notice says so.
//! Setup installs GSdx itself (`setup.rs`), so a choice made during setup does not start a second install.

use gt4l_core::{settings, Settings};
use serde_json::json;
use tauri::{AppHandle, Emitter};

use crate::{state, updates};
use crate::state::{lock, Shared};

/// Shown after a successful switch.
pub const INSTALLED_NOTICE: &str = "GSdx installed successfully";

/// Whether choosing the renderer asks for the GSdx install: GSdx is chosen, the game is set up (setup installs it
/// otherwise) and GSdx is missing or too old.
pub fn needs_install(renderer: &str, home_ready: bool, installed: bool) -> bool {
    renderer == settings::PCSX2_GS && home_ready && !installed
}

/// What the switch did.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Outcome {
    NotNeeded,
    Installed,
    Failed(String),
}

/// The notice after a failed install: the reason and what stays selected.
pub fn failure_notice(message: &str) -> String {
    format!("GSdx could not be installed ({message}). CoreGS 1.1 stays selected; choose GSdx again to retry.")
}

/// Applies a choice to the settings. `install` runs exactly once when an install is needed; on failure the choice
/// goes back to CoreGS 1.1 in `settings`.
pub fn run(settings: &mut Settings, home_ready: bool, installed: bool, install: &mut dyn FnMut() -> Result<(), String>) -> Outcome {
    if !needs_install(settings.get("renderer"), home_ready, installed) {
        return Outcome::NotNeeded;
    }
    match install() {
        Ok(()) => Outcome::Installed,
        Err(message) => {
            let _ = settings.set("renderer", settings::CORE_GS_11);
            Outcome::Failed(message)
        }
    }
}

/// Starts the switch on a worker thread after a `renderer` setting was saved; Settings is never blocked.
pub fn start(app: AppHandle, shared: Shared) {
    let (dirs, mut settings, home_ready, installed) = {
        let state = lock(&shared);
        let installed = !gt4l_core::gs_release::needs_install(&state.dirs.renderer_root());
        (state.dirs.clone(), state.config.settings.clone(), state.home_ready(), installed)
    };
    if !needs_install(settings.get("renderer"), home_ready, installed) {
        return;
    }
    std::thread::spawn(move || {
        let progress_app = app.clone();
        let outcome = run(&mut settings, home_ready, installed, &mut || {
            updates::install_renderer(&dirs, &mut |step| updates::emit_step(&progress_app, step)).map(|_| ())
        });
        match outcome {
            Outcome::NotNeeded => {}
            Outcome::Installed => {
                let _ = app.emit("renderer-switched", json!({ "ok": true, "renderer": settings::PCSX2_GS, "message": INSTALLED_NOTICE }));
            }
            Outcome::Failed(message) => {
                {
                    let mut state = lock(&shared);
                    let _ = state.config.settings.set("renderer", settings::CORE_GS_11);
                    state.save_config();
                }
                state::log(&dirs, &format!("GSdx not installed after the switch in Settings: {message}"));
                let notice = failure_notice(&message);
                let _ = app.emit("renderer-switched", json!({ "ok": false, "renderer": settings::CORE_GS_11, "message": notice }));
            }
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn choosing_gsdx_triggers_exactly_one_install_when_it_is_missing() {
        let mut settings = Settings::default();
        settings.set("renderer", settings::PCSX2_GS).unwrap();
        let mut calls = 0;
        let outcome = run(&mut settings, true, false, &mut || {
            calls += 1;
            Ok(())
        });
        assert_eq!(outcome, Outcome::Installed);
        assert_eq!(calls, 1, "exactly one install");
        assert_eq!(settings.get("renderer"), settings::PCSX2_GS, "the choice stays GSdx");
    }

    #[test]
    fn no_install_when_gsdx_is_present_when_coregs_is_chosen_or_before_setup_is_done() {
        let mut gsdx = Settings::default();
        gsdx.set("renderer", settings::PCSX2_GS).unwrap();
        let mut calls = 0;
        assert_eq!(run(&mut gsdx.clone(), true, true, &mut || { calls += 1; Ok(()) }), Outcome::NotNeeded);
        assert_eq!(run(&mut gsdx.clone(), false, false, &mut || { calls += 1; Ok(()) }), Outcome::NotNeeded, "setup installs it");
        let mut coregs = Settings::default();
        coregs.set("renderer", settings::CORE_GS_11).unwrap();
        assert_eq!(run(&mut coregs, true, false, &mut || { calls += 1; Ok(()) }), Outcome::NotNeeded);
        assert_eq!(calls, 0, "no install was requested");
    }

    #[test]
    fn a_failed_install_leaves_coregs_selected_and_says_why() {
        let mut settings = Settings::default();
        settings.set("renderer", settings::PCSX2_GS).unwrap();
        let mut calls = 0;
        let outcome = run(&mut settings, true, false, &mut || {
            calls += 1;
            Err("offline".to_string())
        });
        assert_eq!(outcome, Outcome::Failed("offline".to_string()));
        assert_eq!(calls, 1, "one attempt, no retry loop");
        assert_eq!(settings.get("renderer"), settings::CORE_GS_11, "the choice is consistent with what runs");
        assert!(failure_notice("offline").contains("CoreGS 1.1 stays selected"));
        assert_eq!(INSTALLED_NOTICE, "GSdx installed successfully");
    }
}
