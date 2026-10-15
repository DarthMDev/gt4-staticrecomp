//! Builds the exact command line and environment for `gt4vulkan.exe`.
//!
//! Contract (from `scripts/external_runtime_probe.py::run_probe` for the `--mode main` profile):
//! - arguments: `<elf> --ntsc --disc <iso> --frames 0 --memcard <card> --misses <session>\misses.txt --window`
//! - the working directory is the session folder;
//! - inherited `GT4_*` and `PS2_*` variables are removed, then only the variables below are set.
//!
//! Only the child process gets these variables. Nothing is written to the user or system environment.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use crate::settings::{self, Settings};

/// Inherited variables with these prefixes are removed before the game starts.
pub const SCRUBBED_PREFIXES: &[&str] = &["GT4_", "PS2_"];

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

/// The variables the settings require, sorted by name. Only connected options emit anything.
///
/// Mapping (setting -> variable):
/// - `renderer`: `GT4_RENDERER=donor` (CoreGS 1.1) or `parallel` (paraLLEl-GS), always sent.
/// - `input_resolution`: 480p -> `GT4_FORCE_480P=1`; Native 480i -> `GT4_FORCE_480P=0`;
///   1080i -> `GT4_FORCE_480P=0` and `GT4_RACE_DISPLAY=1080i` (the current build does not read it yet).
/// CoreGS 1.1 only (the other renderers get `PS2_SCALE=1` and none of these):
/// - `internal_scale` -> `PS2_SCALE=1|2|3` (internal resolution).
/// - `present_filter` -> `GT4_PRESENT_FILTER=nearest|linear|fsr`; `auto` sends nothing.
/// - `fsr_sharpness` -> `GT4_FSR_SHARPNESS=<stops>`, only with the FSR filter.
/// - `deinterlacing` -> `GT4_DEINTERLACE=auto|off|weave|bob|blend|adaptive`.
/// - `anti_aliasing` -> `GT4_AA=off|fxaa`.
/// - `fullscreen` -> `GT4_FULLSCREEN` (1/0).
/// - `vsync` -> `GT4_PRESENT_MODE` (on = fifo, fast = mailbox, off = immediate).
/// - `async_gs` off -> `GT4_GS_SYNC=1`; on leaves it unset (asynchronous is the default).
/// - `performance_report` on -> `GT4_PERF=1`.
/// - `check_vu_code` 100 / 1000 -> `GT4_VU1_VERIFY=N` and `GT4_VU0_VERIFY=N`.
/// - paraLLEl-GS: `GT4_PGS_SSAA=<factor>` and `GT4_PGS_HIRES=1|0` (high-resolution output, only at 4x and up).
///
/// Options marked `connected: false` in the table emit nothing.
pub fn game_environment(settings: &Settings) -> Vec<(String, String)> {
    let mut variables: BTreeMap<&'static str, String> = BTreeMap::new();
    let renderer = settings.get("renderer");
    variables.insert(
        "GT4_RENDERER",
        if renderer == settings::PARALLEL_GS { "parallel" } else { "donor" }.to_string(),
    );
    variables.insert("PS2_GS", "gpu".to_string());
    let core = renderer == settings::CORE_GS_11;
    variables.insert("PS2_SCALE", if core { settings.get("internal_scale") } else { "1" }.to_string());

    match settings.get("input_resolution") {
        "480p" => {
            variables.insert("GT4_FORCE_480P", "1".to_string());
        }
        "1080i" => {
            variables.insert("GT4_FORCE_480P", "0".to_string());
            variables.insert("GT4_RACE_DISPLAY", "1080i".to_string());
        }
        _ => {
            variables.insert("GT4_FORCE_480P", "0".to_string());
        }
    }

    if core {
        let filter = settings.get("present_filter");
        if filter != "auto" {
            variables.insert("GT4_PRESENT_FILTER", filter.to_string());
        }
        if filter == "fsr" {
            variables.insert("GT4_FSR_SHARPNESS", settings.get("fsr_sharpness").to_string());
        }
        variables.insert("GT4_DEINTERLACE", settings.get("deinterlacing").to_string());
        variables.insert("GT4_AA", settings.get("anti_aliasing").to_string());
    }

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

    if renderer == settings::PARALLEL_GS {
        variables.insert("GT4_PGS_SSAA", settings.super_sampling_factor().to_string());
        if settings.is_active("high_resolution_output") && settings.is_on("high_resolution_output") {
            variables.insert("GT4_PGS_HIRES", "1".to_string());
        }
    }

    variables.into_iter().map(|(name, value)| (name.to_string(), value)).collect()
}

fn flag(on: bool) -> String {
    if on { "1" } else { "0" }.to_string()
}

/// Whether an inherited variable must be removed for the game process.
pub fn is_scrubbed(name: &str) -> bool {
    SCRUBBED_PREFIXES.iter().any(|prefix| name.starts_with(prefix))
}

/// Plans one launch. Pure: it creates no files and starts no process.
pub fn plan_launch(inputs: &LaunchInputs<'_>, settings: &Settings, inherited_names: &[String]) -> LaunchPlan {
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
        environment: game_environment(settings),
        removed_inherited: removed,
        working_directory: inputs.session_dir.to_path_buf(),
        log_path: inputs.session_dir.join("game.log"),
        misses_path: misses,
    }
}
