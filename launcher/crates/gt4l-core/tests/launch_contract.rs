//! Launch contract tests: the argument and environment construction is compared with what
//! `scripts/play_performance.py --mode main --force-480p` sends through `external_runtime_probe.run_probe`.
//!
//! Python reference (read from the scripts, not run): `run_probe` builds
//!   arguments = [exe, <session>/SCUS_973.28, --ntsc, --disc, <iso>, --frames, 0,
//!                --memcard, <card>, --misses, <session>/misses.txt, --window]
//! and, for performance profiles, `GT4_PERF=1 PS2_SCALE=1 PS2_GS=gpu`; `--force-480p` adds
//! `GT4_FORCE_480P=1`. The launcher matches that except where the approved design says otherwise:
//! `GT4_PERF` is only set when the Performance report is on (default off), and `GT4_FULLSCREEN` and
//! `GT4_PRESENT_MODE` are always stated. Those two differences are asserted explicitly below.

use std::path::{Path, PathBuf};

use gt4l_core::launch::{self, LaunchInputs};
use gt4l_core::settings::{self, Settings};

fn env_map(pairs: &[(String, String)]) -> std::collections::BTreeMap<String, String> {
    pairs.iter().cloned().collect()
}

#[test]
fn default_settings_send_the_main_profile_variables_plus_the_launcher_defaults() {
    let environment = env_map(&launch::game_environment(&Settings::default()));
    // Same as `--mode main --force-480p`: the platform variables and the 480p hold.
    assert_eq!(environment["PS2_GS"], "gpu");
    assert_eq!(environment["PS2_SCALE"], "1");
    assert_eq!(environment["GT4_FORCE_480P"], "1");
    assert_eq!(environment["GT4_RENDERER"], "donor");
    // Differences with the Python launcher, approved in LAUNCHER-PLAN section 6:
    assert!(!environment.contains_key("GT4_PERF"), "Performance report is off by default");
    assert_eq!(environment["GT4_FULLSCREEN"], "1");
    assert_eq!(environment["GT4_PRESENT_MODE"], "fifo");
    // Asynchronous GS is the default, so GT4_GS_SYNC stays unset.
    assert!(!environment.contains_key("GT4_GS_SYNC"));
    // Nothing paraLLEl-GS specific for CoreGS 1.1.
    assert!(!environment.keys().any(|name| name.starts_with("GT4_PGS_")));
    // CoreGS 1.1 defaults also send deinterlacing (auto) and anti-aliasing (off).
    assert_eq!(environment["GT4_DEINTERLACE"], "auto");
    assert_eq!(environment["GT4_AA"], "off");
    assert_eq!(environment.len(), 8);
}

#[test]
fn argument_list_matches_run_probe_order() {
    let session = Path::new(r"C:\Users\me\AppData\Local\GT4Recomp\sessions\session-1");
    let inputs = LaunchInputs {
        game_executable: Path::new(r"C:\Program Files\GT4Recomp\game\gt4vulkan.exe"),
        elf: Path::new(r"C:\Users\me\AppData\Local\GT4Recomp\prepared\SCUS_973.28"),
        disc: Path::new(r"D:\Games\GT4 (USA) (v2.00).iso"),
        card: Path::new(r"C:\Users\me\AppData\Local\GT4Recomp\runtime\card.ps2"),
        session_dir: session,
    };
    let plan = launch::plan_launch(&inputs, &Settings::default(), &[]);
    assert_eq!(
        plan.arguments,
        vec![
            r"C:\Users\me\AppData\Local\GT4Recomp\prepared\SCUS_973.28",
            "--ntsc",
            "--disc",
            r"D:\Games\GT4 (USA) (v2.00).iso",
            "--frames",
            "0",
            "--memcard",
            r"C:\Users\me\AppData\Local\GT4Recomp\runtime\card.ps2",
            "--misses",
            r"C:\Users\me\AppData\Local\GT4Recomp\sessions\session-1\misses.txt",
            "--window",
        ]
    );
    assert_eq!(plan.working_directory, session);
    assert_eq!(plan.log_path, session.join("game.log"));
    assert_eq!(plan.program, Path::new(r"C:\Program Files\GT4Recomp\game\gt4vulkan.exe"));
}

#[test]
fn inherited_gt4_and_ps2_variables_are_scrubbed_and_others_kept() {
    let inputs = LaunchInputs {
        game_executable: Path::new("gt4vulkan.exe"),
        elf: Path::new("SCUS_973.28"),
        disc: Path::new("disc.iso"),
        card: Path::new("card.ps2"),
        session_dir: Path::new("session"),
    };
    let inherited = vec![
        "GT4_PACING".to_string(),
        "PS2_DUMP_AUDIO".to_string(),
        "PATH".to_string(),
        "VK_LAYER_PATH".to_string(),
    ];
    let plan = launch::plan_launch(&inputs, &Settings::default(), &inherited);
    assert_eq!(plan.removed_inherited, vec!["GT4_PACING", "PS2_DUMP_AUDIO"]);
    assert!(!plan.removed_inherited.iter().any(|name| name == "PATH" || name == "VK_LAYER_PATH"));
}

#[test]
fn settings_map_to_their_variables() {
    let mut settings = Settings::default();
    settings.set("input_resolution", "480i").unwrap();
    settings.set("vsync", "fast").unwrap();
    settings.set("async_gs", "off").unwrap();
    settings.set("performance_report", "on").unwrap();
    settings.set("check_vu_code", "1000").unwrap();
    let environment = env_map(&launch::game_environment(&settings));
    assert_eq!(environment["GT4_FORCE_480P"], "0");
    assert_eq!(environment["GT4_PRESENT_MODE"], "mailbox");
    assert_eq!(environment["GT4_GS_SYNC"], "1");
    assert_eq!(environment["GT4_PERF"], "1");
    assert_eq!(environment["GT4_VU1_VERIFY"], "1000");
    assert_eq!(environment["GT4_VU0_VERIFY"], "1000");

    settings.set("vsync", "off").unwrap();
    let environment = env_map(&launch::game_environment(&settings));
    assert_eq!(environment["GT4_PRESENT_MODE"], "immediate");
}

#[test]
fn one_hundred_and_1080i_map_as_documented() {
    let mut settings = Settings::default();
    settings.set("input_resolution", "1080i").unwrap();
    settings.set("check_vu_code", "100").unwrap();
    let environment = env_map(&launch::game_environment(&settings));
    assert_eq!(environment["GT4_RACE_DISPLAY"], "1080i");
    assert_eq!(environment["GT4_FORCE_480P"], "0");
    assert_eq!(environment["GT4_VU1_VERIFY"], "100");
}

#[test]
fn removed_parallel_gs_renderer_and_its_options_are_refused() {
    let mut settings = Settings::default();
    assert!(settings.set("renderer", "parallel").is_err(), "paraLLEl-GS is dropped (decision 0070)");
    assert!(settings::definition("super_sampling").is_none());
    assert!(settings::definition("high_resolution_output").is_none());
    assert!(!env_map(&launch::game_environment(&settings)).keys().any(|name| name.starts_with("GT4_PGS_")));
}

#[test]
fn coregs11_defaults_send_the_documented_variables() {
    let environment = env_map(&launch::game_environment(&Settings::default()));
    assert_eq!(environment["GT4_RENDERER"], "donor");
    assert_eq!(environment["PS2_SCALE"], "1");
    assert!(!environment.contains_key("GT4_PRESENT_FILTER"), "automatic sends no filter");
    assert!(!environment.contains_key("GT4_FSR_SHARPNESS"));
    assert_eq!(environment["GT4_DEINTERLACE"], "auto");
    assert_eq!(environment["GT4_AA"], "off");
}

#[test]
fn coregs11_upscale_fsr_aa_and_deinterlace_options_are_hidden() {
    let settings = Settings::default();
    for id in ["internal_scale", "present_filter", "fsr_sharpness", "deinterlacing", "anti_aliasing"] {
        assert!(!settings.applies(id), "{id} is hidden (decision 0070)");
        assert!(!settings.is_active(id), "{id} cannot be changed");
    }
    assert!(settings.applies("vsync"));
}

#[test]
fn internal_scale_still_maps_to_ps2_scale_as_a_default() {
    let mut settings = Settings::default();
    settings.set("internal_scale", "3").unwrap();
    assert_eq!(env_map(&launch::game_environment(&settings))["PS2_SCALE"], "3");
}

#[test]
fn present_filter_maps_nearest_linear_and_fsr() {
    let mut settings = Settings::default();
    settings.set("present_filter", "nearest").unwrap();
    assert_eq!(env_map(&launch::game_environment(&settings))["GT4_PRESENT_FILTER"], "nearest");
    settings.set("present_filter", "linear").unwrap();
    assert_eq!(env_map(&launch::game_environment(&settings))["GT4_PRESENT_FILTER"], "linear");
    settings.set("present_filter", "fsr").unwrap();
    assert_eq!(env_map(&launch::game_environment(&settings))["GT4_PRESENT_FILTER"], "fsr");
    assert!(settings.set("present_filter", "bicubic").is_err());
}

#[test]
fn fsr_sharpness_is_a_percentage_sent_only_with_the_fsr_filter() {
    let mut settings = Settings::default();
    assert_eq!(settings.get("fsr_sharpness"), "50", "default 50 %");
    settings.set("fsr_sharpness", "0").unwrap();
    assert!(!env_map(&launch::game_environment(&settings)).contains_key("GT4_FSR_SHARPNESS"));
    settings.set("present_filter", "fsr").unwrap();
    assert_eq!(env_map(&launch::game_environment(&settings))["GT4_FSR_SHARPNESS"], "0");
    settings.set("fsr_sharpness", "100").unwrap();
    assert_eq!(env_map(&launch::game_environment(&settings))["GT4_FSR_SHARPNESS"], "100");
    assert!(settings.set("fsr_sharpness", "101").is_err());
    assert!(settings.set("fsr_sharpness", "0.2").is_err(), "stops are no longer accepted");
}

#[test]
fn deinterlacing_maps_every_documented_value() {
    let mut settings = Settings::default();
    for value in ["off", "weave", "bob", "blend", "adaptive", "auto"] {
        settings.set("deinterlacing", value).unwrap();
        assert_eq!(env_map(&launch::game_environment(&settings))["GT4_DEINTERLACE"], value);
    }
    assert!(settings.set("deinterlacing", "yadif").is_err());
}

#[test]
fn anti_aliasing_maps_off_and_fxaa() {
    let mut settings = Settings::default();
    settings.set("anti_aliasing", "fxaa").unwrap();
    assert_eq!(env_map(&launch::game_environment(&settings))["GT4_AA"], "fxaa");
    assert!(settings.set("anti_aliasing", "smaa").is_err());
}

#[test]
fn renderer_choice_is_pcsx2_when_installed_and_coregs11_otherwise() {
    let mut settings = Settings::default();
    assert_eq!(settings.get("renderer"), "pcsx2", "PCSX2 GS is the default when installed");
    assert_eq!(
        launch::choose_renderer(&settings, None),
        launch::RendererChoice::CoreGs11 {
            reason: Some("PCSX2 GS is not installed yet; this session uses CoreGS 1.1.".to_string())
        }
    );
    let installed = installed_fixture();
    assert_eq!(launch::choose_renderer(&settings, Some(&installed)), launch::RendererChoice::Pcsx2);
    settings.set("renderer", "coregs11").unwrap();
    assert_eq!(launch::choose_renderer(&settings, Some(&installed)), launch::RendererChoice::CoreGs11 { reason: None });
    assert!(settings.set("renderer", "coregs20").is_err(), "coming soon is not selectable");
}

#[test]
fn pcsx2_environment_carries_the_process_pin_config_and_480p() {
    let paths = pcsx2_paths();
    let environment = env_map(&launch::pcsx2_environment(&Settings::default(), &paths));
    assert_eq!(environment["GT4_RENDERER"], "pcsx2");
    assert_eq!(environment["GT4_GS_PROCESS"], paths.executable.display().to_string());
    assert_eq!(environment["GT4_GS_PROCESS_SHA256"], paths.executable_sha256);
    assert_eq!(environment["GT4_GS_CONFIG"], paths.config.display().to_string());
    assert_eq!(environment["GT4_FORCE_480P"], "1", "480p by default");
    assert_eq!(environment["GT4_PRESENT_MODE"], "fifo");
    assert!(!environment.contains_key("GT4_GS_READBACK"), "readback is written in the INI");
    assert!(!environment.contains_key("PS2_SCALE"), "CoreGS 1.1 variables are not sent to PCSX2 GS");
}

#[test]
fn pcsx2_plan_keeps_the_probe_argument_list_and_scrubs_inherited_names() {
    let paths = pcsx2_paths();
    let inherited = vec!["GT4_GS_PROCESS".to_string(), "PATH".to_string(), "PS2_SCALE".to_string()];
    let plan = launch::plan_pcsx2_launch(&inputs(), &Settings::default(), &paths, &inherited);
    assert_eq!(plan.arguments.last().unwrap(), "--window");
    assert_eq!(plan.arguments[0], "C:/work/SCUS_973.28");
    assert_eq!(plan.removed_inherited, vec!["GT4_GS_PROCESS".to_string(), "PS2_SCALE".to_string()]);
}

#[test]
fn session_ini_matches_the_python_writer_for_the_default_settings() {
    let paths = pcsx2_paths();
    let text = launch::gs_config_text(&Settings::default(), &paths).unwrap();
    let bytes = gt4l_core::gs_table::ini_file_bytes(&text);
    let expected = include_bytes!("fixtures/gs/default-session.ini");
    assert_eq!(bytes.as_slice(), expected.as_slice(), "Rust gt4-gs.ini differs from scripts/gs_settings.py");
}

#[test]
fn restart_marker_is_detected_in_the_game_log() {
    assert!(launch::restart_requested(&format!("boot\n{}\nmore\n", launch::GS_RESTART_MARKER)));
    assert!(!launch::restart_requested("[gs-ipc] restart requested: renderer pcsx2\n"));
}

fn installed_fixture() -> gt4l_core::gs_release::Installed {
    gt4l_core::gs_release::Installed {
        version: gt4l_core::gs_release::Version(0, 3, 2),
        executable: PathBuf::from("C:/Users/x/AppData/Local/GT4Recomp/gs/0.3.2/gt4-gs.exe"),
        executable_sha256: "f".repeat(64),
    }
}

fn pcsx2_paths() -> launch::Pcsx2Paths {
    launch::Pcsx2Paths {
        executable: PathBuf::from("C:/gs/0.3.2/gt4-gs.exe"),
        executable_sha256: "a".repeat(64),
        config: PathBuf::from("C:/session/gt4-gs.ini"),
        cache: PathBuf::from("C:/gs/cache"),
        data_root: PathBuf::from("C:/gs/data"),
    }
}

fn inputs() -> LaunchInputs<'static> {
    LaunchInputs {
        game_executable: Path::new("C:/game/gt4vulkan.exe"),
        elf: Path::new("C:/work/SCUS_973.28"),
        disc: Path::new("C:/disc/game.iso"),
        card: Path::new("C:/cards/card.ps2"),
        session_dir: Path::new("C:/session"),
    }
}

/// Both window modes start the game window (`--window` is required for any window). Only GT4_FULLSCREEN changes,
/// and the native game reads it at start: 1 starts borderless fullscreen, 0 starts windowed.
#[test]
fn both_window_modes_pass_window_and_send_gt4_fullscreen_to_the_game_and_the_gs_child() {
    let mut fullscreen = Settings::default();
    fullscreen.set("fullscreen", "on").unwrap();
    let mut windowed = Settings::default();
    windowed.set("fullscreen", "off").unwrap();

    for (settings, expected) in [(&fullscreen, "1"), (&windowed, "0")] {
        let plan = launch::plan_launch(&inputs(), settings, &[]);
        assert_eq!(plan.arguments.last().unwrap(), "--window", "a window is always started");
        assert!(!plan.arguments.iter().any(|argument| argument == "--fullscreen"));
        assert_eq!(env_map(&plan.environment)["GT4_FULLSCREEN"], expected);

        let gs_plan = launch::plan_pcsx2_launch(&inputs(), settings, &pcsx2_paths(), &[]);
        assert_eq!(gs_plan.arguments.last().unwrap(), "--window");
        assert_eq!(env_map(&gs_plan.environment)["GT4_FULLSCREEN"], expected, "the PCSX2 GS path gets the same mode");
    }
}
