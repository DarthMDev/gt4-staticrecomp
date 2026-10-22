//! Update checks and installs (owner rules, 2026-10-20). Background checks cover launcher releases and the selected
//! external renderer, with a short timeout, and never block Play. It offers an update; it never installs one by
//! itself. The one exception is the first gt4-gs install, which has nothing to replace.
//!
//! - gt4-gs: installed into a new version folder with the same staging rules as setup (`gs_release`), then
//!   `current.txt` and `version.json` point at it. Only when the game is not running.
//! - GT4Recomp: the setup program is downloaded to a temporary folder, its SHA-256 checked against `SHA256SUMS`,
//!   started, and the launcher exits. The installer upgrades the program files in place and keeps the data folder.
//!
//! Events to the page: `update-available` (offers), `renderer-progress` (install steps), `update-finished`.

use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::Mutex;

use gt4l_core::gs_release::{self, Outcome, Step};
use gt4l_core::paths::DataDirs;
use gt4l_core::releases::{self, GithubSource, ReleaseSource, Stream};
use gt4l_core::update::{self, Offer};
use gt4l_core::versions::{self, VersionFile};
use gt4l_core::{launch, Settings, Version, LAUNCHER_VERSION};
use serde::Serialize;
use serde_json::{json, Value};
use tauri::{AppHandle, Emitter, State};

use crate::state::{self, lock, Shared};

/// One renderer install at a time: the launch check, setup and an update must not write the same folder.
static RENDERER_INSTALL: Mutex<()> = Mutex::new(());

/// The renderer root under the data folder: `<data>\gs`.
pub fn renderer_root(dirs: &DataDirs) -> PathBuf {
    dirs.root().join("gs")
}

/// The folder of `GT4Recomp.exe` (the install folder).
pub fn install_directory() -> PathBuf {
    std::env::current_exe()
        .ok()
        .and_then(|path| path.parent().map(Path::to_path_buf))
        .unwrap_or_else(|| PathBuf::from("."))
}

/// The `version.json` location: the install folder when writable, else the data folder (logged).
pub fn version_location(dirs: &DataDirs) -> versions::Location {
    let location = versions::location(&install_directory(), dirs.root());
    if let Some(notice) = &location.notice {
        state::log(dirs, notice);
    }
    location
}

/// The launcher-facing view of one offer (the popup and the Settings check use it).
#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct OfferView {
    pub stream: &'static str,
    /// `null` when the stream is not installed yet.
    pub installed: Option<String>,
    pub latest: String,
    pub notes: String,
    pub package: String,
    pub update: bool,
}

impl OfferView {
    pub fn of(offer: &Offer) -> OfferView {
        OfferView {
            stream: offer.stream.key(),
            installed: offer.installed.map(|version| version.text()),
            latest: offer.latest.text(),
            notes: offer.notes.clone(),
            package: offer.package.clone(),
            update: offer.is_update(),
        }
    }
}

/// The result of one check of both streams.
pub struct Check {
    pub offers: Vec<Offer>,
    /// Failures of the check. They are logged; the popup shows none of them.
    pub errors: Vec<String>,
    /// The installed gt4-gs version, if any.
    pub renderer_installed: Option<Version>,
}

fn fetch(stream: Stream) -> Result<Vec<releases::Release>, String> {
    let source = GithubSource::for_stream(stream);
    releases::parse_releases(&source.releases_json()?, stream)
}

/// An explicit update check includes both streams, even when the player uses CoreGS.
/// Blocking: run it on a worker thread.
pub fn check(dirs: &DataDirs, skipped_launcher: Option<Version>, skipped_renderer: Option<Version>) -> Check {
    check_with(dirs, skipped_launcher, skipped_renderer, true, fetch)
}

/// Automatic checks keep launcher updates enabled but leave the unselected renderer alone.
fn check_for_settings(
    dirs: &DataDirs,
    settings: &Settings,
    skipped_launcher: Option<Version>,
    skipped_renderer: Option<Version>,
    fetch_releases: impl FnMut(Stream) -> Result<Vec<releases::Release>, String>,
) -> Check {
    check_with(dirs, skipped_launcher, skipped_renderer, launch::needs_external_renderer(settings), fetch_releases)
}

/// The injected fetcher lets tests verify which network requests an automatic check actually makes.
fn check_with(
    dirs: &DataDirs,
    skipped_launcher: Option<Version>,
    skipped_renderer: Option<Version>,
    include_renderer: bool,
    mut fetch_releases: impl FnMut(Stream) -> Result<Vec<releases::Release>, String>,
) -> Check {
    let mut offers = Vec::new();
    let mut errors = Vec::new();
    let renderer_installed = gs_release::installed(&renderer_root(dirs)).map(|installed| installed.version);
    let launcher_installed = Version::parse(LAUNCHER_VERSION);
    let streams = [
        (Stream::Launcher, launcher_installed, skipped_launcher),
        (Stream::Renderer, renderer_installed, skipped_renderer),
    ];
    for (stream, installed, skipped) in streams {
        if stream == Stream::Renderer && !include_renderer {
            continue;
        }
        match fetch_releases(stream) {
            Ok(list) => {
                if let Some(offer) = update::decide(stream, installed, &list, skipped) {
                    offers.push(offer);
                }
            }
            Err(message) => errors.push(format!("{}: {message}", stream.key())),
        }
    }
    for error in &errors {
        state::log(dirs, &format!("update check: {error}"));
    }
    Check { offers, errors, renderer_installed }
}

/// Installs the newest gt4-gs (first install or update). Records the version in `version.json`.
pub fn install_renderer(dirs: &DataDirs, progress: &mut dyn FnMut(Step)) -> Result<Outcome, String> {
    let _serial = RENDERER_INSTALL.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
    let root = renderer_root(dirs);
    let source = GithubSource::for_stream(Stream::Renderer);
    let outcome = gs_release::install_latest_reporting(&source as &dyn ReleaseSource, &root, progress)?;
    let location = version_location(dirs);
    if let Err(message) = versions::record_renderer(
        &location.path,
        location.path.parent().unwrap_or(dirs.root()),
        outcome.installed().version,
        LAUNCHER_VERSION,
    ) {
        state::log(dirs, &format!("version.json not updated: {message}"));
    }
    state::log(dirs, &format!("gt4-gs {} ready", outcome.installed().version.text()));
    Ok(outcome)
}

pub fn step_name(step: Step) -> &'static str {
    match step {
        Step::Listing => "listing",
        Step::Downloading | Step::Transfer { .. } => "downloading",
        Step::Installing => "installing",
    }
}

/// Emits a step change; the per-chunk byte counts are not sent from here (the setup page polls them).
fn emit_step(app: &AppHandle, step: Step) {
    if !matches!(step, Step::Transfer { .. }) {
        emit_progress(app, step_name(step));
    }
}

pub fn emit_progress(app: &AppHandle, step: &str) {
    let _ = app.emit("renderer-progress", json!({ "step": step }));
}

/// Makes sure `version.json` exists, is readable and names what is installed. A damaged file is replaced by a fresh
/// one (the damaged copy is kept as `version.json.bad`). The launcher version stays the Cargo version; a file that
/// disagrees (after an upgrade the installer writes a null renderer) is rewritten with the Cargo version and the checked
/// gt4-gs install (`versions::reconciled`).
pub fn ensure_version_file(dirs: &DataDirs) {
    let location = version_location(dirs);
    match versions::read(&location.path) {
        Ok(Some(file)) => {
            let (_, warning) = versions::reconcile_launcher_version(Some(&file), LAUNCHER_VERSION);
            if let Some(warning) = warning {
                state::log(dirs, &warning);
            }
            let renderer = gs_release::installed(&renderer_root(dirs)).map(|installed| installed.version);
            if let Some(next) = versions::reconciled(&file, LAUNCHER_VERSION, renderer) {
                if let Err(message) = versions::write(&location.path, location.path.parent().unwrap_or(dirs.root()), &next) {
                    state::log(dirs, &format!("version.json not updated: {message}"));
                }
            }
        }
        Ok(None) => {
            let file = VersionFile::for_install(LAUNCHER_VERSION, gt4l_core::config::SCHEMA_VERSION);
            if let Err(message) = versions::write(&location.path, location.path.parent().unwrap_or(dirs.root()), &file) {
                state::log(dirs, &format!("version.json not written: {message}"));
            }
        }
        Err(message) => {
            state::log(dirs, &format!("{message}; kept as version.json.bad and replaced"));
            let _ = fs::rename(&location.path, location.path.with_extension("json.bad"));
            let file = VersionFile::for_install(LAUNCHER_VERSION, gt4l_core::config::SCHEMA_VERSION);
            let _ = versions::write(&location.path, location.path.parent().unwrap_or(dirs.root()), &file);
        }
    }
}

/// The startup repair applies only to a prepared game using the external renderer.
fn needs_renderer_install(dirs: &DataDirs, settings: &Settings, set_up: bool) -> bool {
    set_up && launch::needs_external_renderer(settings) && gs_release::needs_install(&renderer_root(dirs))
}

/// The background work at every launch: version file, the selected external renderer install when none exists or
/// the installed one is below the minimum (gt4-gs 1.0.1 under launcher 1.1.0 is replaced by the newest stable release),
/// and the update check. Failures are logged only. Emits `update-available` when there is something to offer.
pub fn check_on_launch(app: AppHandle, shared: Shared) {
    let (dirs, config, set_up) = {
        let state = lock(&shared);
        (state.dirs.clone(), state.config.clone(), state.home_ready())
    };
    ensure_version_file(&dirs);
    // Before the first setup the renderer is installed by setup itself, with its progress on the setup page. Here it
    // is the retry after a setup that was offline, or the update of a version below the minimum.
    if needs_renderer_install(&dirs, &config.settings, set_up) {
        let progress_app = app.clone();
        match install_renderer(&dirs, &mut |step| emit_step(&progress_app, step)) {
            Ok(_) => emit_progress(&app, "done"),
            Err(message) => state::log(&dirs, &format!("gt4-gs not installed at launch: {message}")),
        }
    }
    let result = check_for_settings(&dirs, &config.settings, config.skipped_launcher, config.skipped_renderer, fetch);
    let offers: Vec<OfferView> = result.offers.iter().filter(|offer| offer.is_update()).map(OfferView::of).collect();
    if !offers.is_empty() {
        let _ = app.emit("update-available", json!({ "offers": offers }));
    }
}

/// `check_updates()`: `{offers: [OfferView], errors: [string], upToDate: bool, launcher, renderer}`.
/// Used by the Settings "Check for updates" button and the About page.
#[tauri::command]
pub async fn check_updates(shared: State<'_, Shared>) -> Result<Value, String> {
    let shared: Shared = shared.inner().clone();
    tauri::async_runtime::spawn_blocking(move || {
        let dirs = lock(&shared).dirs.clone();
        // The button ignores "Skip this version": the player asked for the answer now.
        let result = check(&dirs, None, None);
        let offers: Vec<OfferView> = result.offers.iter().filter(|offer| offer.is_update()).map(OfferView::of).collect();
        let up_to_date = offers.is_empty() && result.errors.is_empty();
        Ok(json!({
            "offers": offers,
            "errors": result.errors,
            "upToDate": up_to_date,
            "launcher": LAUNCHER_VERSION,
            "renderer": result.renderer_installed.map(|version| version.text()),
        }))
    })
    .await
    .map_err(|error| error.to_string())?
}

/// `skip_update({stream, version})`: no offer again for exactly this version. Saved in `settings.toml`.
#[tauri::command]
pub fn skip_update(stream: String, version: String, shared: State<'_, Shared>) -> Result<(), String> {
    let parsed = Version::parse(&version).ok_or_else(|| format!("'{version}' is not a version"))?;
    let mut state = lock(shared.inner());
    match stream.as_str() {
        "launcher" => state.config.skipped_launcher = Some(parsed),
        "renderer" => state.config.skipped_renderer = Some(parsed),
        other => return Err(format!("unknown update stream '{other}'")),
    }
    state.save_config();
    Ok(())
}

/// `apply_update({stream})`: installs the newest stable version of the stream the player accepted.
/// The renderer is installed in place; the launcher starts its setup program and exits.
#[tauri::command]
pub async fn apply_update(app: AppHandle, stream: String, shared: State<'_, Shared>) -> Result<Value, String> {
    let shared: Shared = shared.inner().clone();
    tauri::async_runtime::spawn_blocking(move || {
        let (dirs, game_running) = {
            let state = lock(&shared);
            (state.dirs.clone(), state.game_child.is_some())
        };
        match stream.as_str() {
            "renderer" => {
                if game_running {
                    return Err("Close the game before updating the renderer.".to_string());
                }
                let progress_app = app.clone();
                let outcome = install_renderer(&dirs, &mut |step| emit_step(&progress_app, step));
                let (ok, message) = match outcome {
                    Ok(outcome) => (true, format!("gt4-gs {} is ready.", outcome.installed().version.text())),
                    Err(message) => (false, message),
                };
                let _ = app.emit("update-finished", json!({ "stream": "renderer", "ok": ok, "message": message }));
                if ok {
                    Ok(json!({ "ok": true, "message": message }))
                } else {
                    Err(message)
                }
            }
            "launcher" => {
                // Updating ends the launcher, and the game ends with it (kill-on-close job).
                if game_running {
                    return Err("Close the game before updating GT4Recomp.".to_string());
                }
                let message = start_launcher_setup(&app, &dirs)?;
                Ok(json!({ "ok": true, "message": message }))
            }
            other => Err(format!("unknown update stream '{other}'")),
        }
    })
    .await
    .map_err(|error| error.to_string())?
}

/// Downloads the GT4Recomp setup program of the newest stable release, checks it, starts it, and ends the launcher.
fn start_launcher_setup(app: &AppHandle, dirs: &DataDirs) -> Result<String, String> {
    let list = fetch(Stream::Launcher)?;
    let latest = releases::select_latest(&list, Stream::Launcher.minimum()).ok_or("no stable GT4Recomp release")?;
    let version = latest.version.ok_or("the release has no version")?;
    if Version::parse(LAUNCHER_VERSION).is_some_and(|current| current >= version) {
        return Err("GT4Recomp is up to date.".to_string());
    }
    let package = Stream::Launcher.package_name(version);
    let package_asset = latest.asset(&package).ok_or_else(|| format!("release {} has no {package}", latest.tag))?;
    let sums_asset = latest
        .asset(releases::CHECKSUM_ASSET)
        .ok_or_else(|| format!("release {} has no {}", latest.tag, releases::CHECKSUM_ASSET))?;
    let sums = GithubSource::for_stream(Stream::Launcher).download(&sums_asset.url)?;
    let sums_text = String::from_utf8_lossy(&sums).into_owned();

    let folder = std::env::temp_dir().join("GT4Recomp-update");
    let _ = fs::remove_dir_all(&folder);
    fs::create_dir_all(&folder).map_err(|error| error.to_string())?;
    let path = folder.join(&package);
    emit_progress(app, "downloading-launcher");
    let actual = releases::download_to_file(&package_asset.url, &path, releases::MAX_SETUP_BYTES)?;
    releases::check_sum(&package, &actual, &sums_text).inspect_err(|_| {
        let _ = fs::remove_file(&path);
    })?;
    state::log(dirs, &format!("GT4Recomp {} setup verified; starting it", version.text()));
    // The setup must outlive the launcher: it leaves the launcher's kill-on-close job (gt4l_platform::process).
    // A job that the launcher itself was started in may forbid that; then the Windows shell (the running Explorer,
    // outside every job of ours) starts it, and the launcher waits a moment so the hand-over completes first.
    if let Err(error) = gt4l_platform::process::spawn_outside_job(&mut Command::new(&path)) {
        state::log(dirs, &format!("setup could not leave the launcher job ({error}); asking Explorer to start it"));
        Command::new("explorer.exe")
            .arg(&path)
            .spawn()
            .map_err(|error| format!("could not start the setup: {error}"))?;
        std::thread::sleep(std::time::Duration::from_secs(3));
    }
    let _ = app.emit("update-finished", json!({ "stream": "launcher", "ok": true, "message": "Setup started." }));
    app.exit(0);
    Ok(format!("GT4Recomp {} setup started.", version.text()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_offer_view_names_both_versions_and_the_update_flag() {
        let offer = Offer {
            stream: Stream::Renderer,
            installed: Some(Version(1, 0, 0)),
            latest: Version(1, 0, 1),
            notes: "Notes.".into(),
            package: "gt4-gs-1.0.1-windows-x64.zip".into(),
        };
        let view = OfferView::of(&offer);
        assert_eq!(view.stream, "renderer");
        assert_eq!(view.installed.as_deref(), Some("1.0.0"));
        assert_eq!(view.latest, "1.0.1");
        assert!(view.update);
    }

    #[test]
    fn the_renderer_folder_lives_under_the_data_folder() {
        let dirs = DataDirs::new("C:/data/GT4Recomp");
        assert_eq!(renderer_root(&dirs), PathBuf::from("C:/data/GT4Recomp").join("gs"));
    }

    #[test]
    fn coregs_startup_checks_the_launcher_without_requesting_renderer_releases() {
        let root = std::env::temp_dir().join(format!("gt4l-coregs-check-{}", std::process::id()));
        let dirs = DataDirs::new(&root);
        let mut settings = Settings::default();
        settings.set("renderer", gt4l_core::settings::CORE_GS_11).unwrap();
        let mut requested = Vec::new();
        let result = check_for_settings(&dirs, &settings, None, None, |stream| {
            requested.push(stream);
            assert_eq!(stream, Stream::Launcher, "CoreGS must not request the external renderer's release list");
            releases::parse_releases(r#"[{"tag_name":"v99.0.0","draft":false,"prerelease":false,"assets":[]}]"#, stream)
        });
        assert_eq!(requested, vec![Stream::Launcher]);
        assert!(result.errors.is_empty());
        assert_eq!(result.offers.len(), 1);
        assert_eq!(result.offers[0].stream, Stream::Launcher);
        assert!(!needs_renderer_install(&dirs, &settings, true), "missing gt4-gs does not require installation");
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn pcsx2_startup_still_repairs_missing_packages_and_checks_both_streams() {
        let root = std::env::temp_dir().join(format!("gt4l-pcsx2-check-{}", std::process::id()));
        let dirs = DataDirs::new(&root);
        let settings = Settings::default();
        assert!(!needs_renderer_install(&dirs, &settings, false), "setup owns the first install");
        assert!(needs_renderer_install(&dirs, &settings, true), "a prepared game repairs its missing package");
        let mut requested = Vec::new();
        let result = check_for_settings(&dirs, &settings, None, None, |stream| {
            requested.push(stream);
            Ok(Vec::new())
        });
        assert_eq!(requested, vec![Stream::Launcher, Stream::Renderer]);
        assert!(result.errors.is_empty());
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn an_explicit_check_can_still_query_the_optional_renderer() {
        let root = std::env::temp_dir().join(format!("gt4l-explicit-check-{}", std::process::id()));
        let dirs = DataDirs::new(&root);
        let mut requested = Vec::new();
        let result = check_with(&dirs, None, None, true, |stream| {
            requested.push(stream);
            Ok(Vec::new())
        });
        assert_eq!(requested, vec![Stream::Launcher, Stream::Renderer]);
        assert!(result.errors.is_empty());
        let _ = fs::remove_dir_all(root);
    }
}
