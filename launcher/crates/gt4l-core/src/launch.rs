//! Builds the exact command line and environment for `gt4vulkan.exe`.
//!
//! Contract (from `scripts/external_runtime_probe.py::run_probe` and `gs_stream_environment`):
//! - arguments: `<elf> --ntsc --disc <iso> --frames 0 --memcard <card> --misses <session>\misses.txt --window`
//! - the working directory is the session folder;
//! - inherited `GT4_*` and `PS2_*` variables are removed, then only the variables below are set.
//!
//! Two renderers (decision 0070): CoreGS 1.1 (`GT4_RENDERER=donor`, the built-in renderer) and the PCSX2 GS
//! (`GT4_RENDERER=pcsx2`, the downloaded `gt4-gs.exe` in its own process, see [`pcsx2_environment`]).
//! Only the child process gets these variables. Nothing is written to the user or system environment.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use crate::gs_release::Installed;
use crate::gs_table;
use crate::settings::{self, Settings};

/// Inherited variables with these prefixes are removed before the game starts.
pub const SCRUBBED_PREFIXES: &[&str] = &["GT4_", "PS2_"];

/// Printed by the game when the player chose "restart with the built-in renderer" after a GS process failure.
/// The launcher then starts the game again with `GT4_RENDERER=donor` (scripts/external_runtime_probe.py,
/// `GS_RESTART_MARKER`).
pub const GS_RESTART_MARKER: &str = "[gs-ipc] restart requested: renderer donor (CoreGS 1.1)";

/// Everything needed to start one game session.
#[derive(Clone, Debug)]
pub struct LaunchInputs<'a> {
    /// `gt4vulkan.exe`.
    pub game_executable: &'a Path,
    /// The rebuilt ELF in the prepared folder. Its file name must stay `SCUS_973.28` (argv0 rule).
    pub elf: &'a Path,
    pub disc: &'a Path,
    pub card: &'a Path,
    /// A new session folder; it becomes the working directory and receives `misses.txt` and `game.log`.
    pub session_dir: &'a Path,
}

/// The planned launch. `removed_inherited` lists variables to remove from the child environment.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LaunchPlan {
    pub program: PathBuf,
    pub arguments: Vec<String>,
    pub environment: Vec<(String, String)>,
    pub removed_inherited: Vec<String>,
    pub working_directory: PathBuf,
    pub log_path: PathBuf,
    pub misses_path: PathBuf,
}

/// The files and pins of the PCSX2 GS process for one session.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Pcsx2Paths {
    /// `gt4-gs.exe` in the installed version folder.
    pub executable: PathBuf,
    /// SHA-256 of that file (`GT4_GS_PROCESS_SHA256`, checked by the game).
    pub executable_sha256: String,
    /// `gt4-gs.ini` written for this session.
    pub config: PathBuf,
    /// Shader and pipeline cache folder (`%LOCALAPPDATA%\GT4Recomp\gs\cache`).
    pub cache: PathBuf,
    /// Data folder of the GS process (`%LOCALAPPDATA%\GT4Recomp\gs\data`).
    pub data_root: PathBuf,
}

impl Pcsx2Paths {
    /// The paths of an installed renderer, with the cache and data folders under the renderer root.
    pub fn for_session(installed: &Installed, renderer_root: &Path, session_dir: &Path) -> Pcsx2Paths {
        Pcsx2Paths {
            executable: installed.executable.clone(),
            executable_sha256: installed.executable_sha256.clone(),
            config: session_dir.join("gt4-gs.ini"),
            cache: renderer_root.join("cache"),
            data_root: renderer_root.join("data"),
        }
    }
}

/// Which renderer a session starts with.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum RendererChoice {
    /// The PCSX2 GS, installed and checked.
    Pcsx2,
    /// CoreGS 1.1. `reason` is the message shown to the player when it replaces the PCSX2 GS.
    CoreGs11 { reason: Option<String> },
}

/// Chooses the renderer of a session. PCSX2 GS is used when selected and installed; otherwise CoreGS 1.1 starts
/// with a message. CoreGS 2.0 is not built, so it also starts CoreGS 1.1.
pub fn choose_renderer(settings: &Settings, installed: Option<&Installed>) -> RendererChoice {
    match settings.get("renderer") {
        settings::PCSX2_GS => match installed {
            Some(_) => RendererChoice::Pcsx2,
            None => RendererChoice::CoreGs11 {
                reason: Some("PCSX2 GS is not installed yet; this session uses CoreGS 1.1.".to_string()),
            },
        },
        _ => RendererChoice::CoreGs11 { reason: None },
    }
}

/// The variables the settings require, sorted by name, for CoreGS 1.1 (`GT4_RENDERER=donor`).
/// Only connected options emit anything.
///
/// Mapping (setting -> variable):
/// - Always `GT4_FORCE_480P=1`: the game always runs at 480p (owner, 2026-10-21; there is no setting).
/// - Always `GT4_FORCE_WIDESCREEN=1`: the game's wide flag is held at 16:9 until the ultrawide update (owner,
///   2026-10-21; TASK-079 hold, there is no setting).
/// - `internal_scale` -> `PS2_SCALE=1|2|3`; `present_filter` -> `GT4_PRESENT_FILTER` (`auto` sends nothing);
///   `fsr_sharpness` -> `GT4_FSR_SHARPNESS` with the FSR filter; `deinterlacing` -> `GT4_DEINTERLACE`;
///   `anti_aliasing` -> `GT4_AA`. These are hidden in the UI (decision 0070) and keep their defaults.
/// - `fullscreen` -> `GT4_FULLSCREEN` (1/0).
/// - `vsync` -> `GT4_PRESENT_MODE` (on = fifo, fast = mailbox, off = immediate).
/// - `async_gs` off -> `GT4_GS_SYNC=1`; on leaves it unset (asynchronous is the default).
/// - `performance_report` on -> `GT4_PERF=1`.
/// - `check_vu_code` 100 / 1000 -> `GT4_VU1_VERIFY=N` and `GT4_VU0_VERIFY=N`.
pub fn game_environment(settings: &Settings) -> Vec<(String, String)> {
    let mut variables: BTreeMap<&'static str, String> = BTreeMap::new();
    variables.insert("GT4_RENDERER", "donor".to_string());
    variables.insert("PS2_GS", "gpu".to_string());
    variables.insert("PS2_SCALE", settings.get("internal_scale").to_string());

    let filter = settings.get("present_filter");
    if filter != "auto" {
        variables.insert("GT4_PRESENT_FILTER", filter.to_string());
    }
    if filter == "fsr" {
        variables.insert("GT4_FSR_SHARPNESS", settings.get("fsr_sharpness").to_string());
    }
    variables.insert("GT4_DEINTERLACE", settings.get("deinterlacing").to_string());
    variables.insert("GT4_AA", settings.get("anti_aliasing").to_string());

    add_common(&mut variables, settings);
    variables.into_iter().map(|(name, value)| (name.to_string(), value)).collect()
}

/// The variables of a PCSX2 GS session: `GT4_RENDERER=pcsx2`, the process and its pin, the session INI, the
/// folders, and the fixed 480p and 16:9 holds (see `add_common`). Readback is accurate unless the table row
/// `hw_download_mode` says otherwise; it is written into the INI, so no variable is needed.
pub fn pcsx2_environment(settings: &Settings, paths: &Pcsx2Paths) -> Vec<(String, String)> {
    let mut variables: BTreeMap<&'static str, String> = BTreeMap::new();
    variables.insert("GT4_RENDERER", "pcsx2".to_string());
    variables.insert("GT4_GS_PROCESS", paths.executable.display().to_string());
    variables.insert("GT4_GS_PROCESS_SHA256", paths.executable_sha256.clone());
    variables.insert("GT4_GS_CONFIG", paths.config.display().to_string());
    variables.insert("PS2_GS", "gpu".to_string());
    add_common(&mut variables, settings);
    variables.into_iter().map(|(name, value)| (name.to_string(), value)).collect()
}

/// The variables shared by both renderers: the fixed 480p and 16:9 holds, display mode, present mode, async GS and
/// diagnostics.
fn add_common(variables: &mut BTreeMap<&'static str, String>, settings: &Settings) {
    // Fixed (owner, 2026-10-21): the game always runs at 480p and 16:9. No setting changes these, and no stored
    // value of the retired `input_resolution` or `widescreen` settings is read (settings::RETIRED_SETTINGS).
    variables.insert("GT4_FORCE_480P", "1".to_string());
    variables.insert("GT4_FORCE_WIDESCREEN", "1".to_string());
    variables.insert("GT4_FULLSCREEN", flag(settings.is_on("fullscreen")));
    variables.insert(
        "GT4_PRESENT_MODE",
        match settings.get("vsync") {
            "fast" => "mailbox",
            "off" => "immediate",
            _ => "fifo",
        }
        .to_string(),
    );
    if !settings.is_on("async_gs") {
        variables.insert("GT4_GS_SYNC", "1".to_string());
    }
    if settings.is_on("performance_report") {
        variables.insert("GT4_PERF", "1".to_string());
    }
    match settings.get("check_vu_code") {
        "100" => {
            variables.insert("GT4_VU1_VERIFY", "100".to_string());
            variables.insert("GT4_VU0_VERIFY", "100".to_string());
        }
        "1000" => {
            variables.insert("GT4_VU1_VERIFY", "1000".to_string());
            variables.insert("GT4_VU0_VERIFY", "1000".to_string());
        }
        _ => {}
    }
}

/// The gt4-gs.ini text of a session, rendered from the table (`gs_table::render_ini`, same rules as
/// scripts/gs_settings.py). The vsync and mailbox rows follow the present mode the way the probe does:
/// fifo writes both true, mailbox writes mailbox false, immediate writes vsync false.
pub fn gs_config_text(settings: &Settings, paths: &Pcsx2Paths) -> Result<String, String> {
    let mut user: BTreeMap<String, String> = settings.gs_overrides().clone();
    let present = match settings.get("vsync") {
        "fast" => "mailbox",
        "off" => "immediate",
        _ => "fifo",
    };
    let vsync = if present == "immediate" { "false" } else { "true" };
    let mailbox = if present == "mailbox" { "false" } else { "true" };
    user.entry("vsync".to_string()).or_insert_with(|| vsync.to_string());
    user.entry("mailbox".to_string()).or_insert_with(|| mailbox.to_string());
    let mut launcher: BTreeMap<String, String> = BTreeMap::new();
    launcher.insert("cache_folder".to_string(), paths.cache.display().to_string());
    launcher.insert("data_root".to_string(), paths.data_root.display().to_string());
    gs_table::render_ini(&user, &launcher).map_err(|error| error.to_string())
}

/// Whether the game asked for a restart with the built-in renderer (its log holds the marker line).
pub fn restart_requested(log_text: &str) -> bool {
    log_text.lines().any(|line| line.contains(GS_RESTART_MARKER))
}

fn flag(on: bool) -> String {
    if on { "1" } else { "0" }.to_string()
}

/// Whether an inherited variable must be removed for the game process.
pub fn is_scrubbed(name: &str) -> bool {
    SCRUBBED_PREFIXES.iter().any(|prefix| name.starts_with(prefix))
}

/// Plans one CoreGS 1.1 launch. Pure: it creates no files and starts no process.
pub fn plan_launch(inputs: &LaunchInputs<'_>, settings: &Settings, inherited_names: &[String]) -> LaunchPlan {
    plan_with(inputs, game_environment(settings), inherited_names)
}

/// Plans one PCSX2 GS launch. Pure: the caller writes `paths.config` (see [`gs_config_text`]) before starting.
pub fn plan_pcsx2_launch(
    inputs: &LaunchInputs<'_>,
    settings: &Settings,
    paths: &Pcsx2Paths,
    inherited_names: &[String],
) -> LaunchPlan {
    plan_with(inputs, pcsx2_environment(settings, paths), inherited_names)
}

fn plan_with(inputs: &LaunchInputs<'_>, environment: Vec<(String, String)>, inherited_names: &[String]) -> LaunchPlan {
    let misses = inputs.session_dir.join("misses.txt");
    let arguments = vec![
        inputs.elf.display().to_string(),
        "--ntsc".to_string(),
        "--disc".to_string(),
        inputs.disc.display().to_string(),
        "--frames".to_string(),
        "0".to_string(),
        "--memcard".to_string(),
        inputs.card.display().to_string(),
        "--misses".to_string(),
        misses.display().to_string(),
        "--window".to_string(),
    ];
    let mut removed: Vec<String> = inherited_names.iter().filter(|name| is_scrubbed(name)).cloned().collect();
    removed.sort();
    removed.dedup();
    LaunchPlan {
        program: inputs.game_executable.to_path_buf(),
        arguments,
        environment,
        removed_inherited: removed,
        working_directory: inputs.session_dir.to_path_buf(),
        log_path: inputs.session_dir.join("game.log"),
        misses_path: misses,
    }
}
