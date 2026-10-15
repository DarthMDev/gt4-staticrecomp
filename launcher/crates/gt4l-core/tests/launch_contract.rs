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

use std::path::Path;

use gt4l_core::launch::{self, LaunchInputs};
use gt4l_core::settings::Settings;

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
fn parallel_gs_options_only_when_that_renderer_is_selected() {
    let mut settings = Settings::default();
    settings.set("super_sampling", "4").unwrap();
    settings.set("high_resolution_output", "on").unwrap();
    // CoreGS 1.1 (default): none of the paraLLEl-GS variables are sent.
    assert!(!env_map(&launch::game_environment(&settings)).keys().any(|name| name.starts_with("GT4_PGS_")));

    settings.set("renderer", "parallel").unwrap();
    let environment = env_map(&launch::game_environment(&settings));
    assert_eq!(environment["GT4_RENDERER"], "parallel");
    assert_eq!(environment["GT4_PGS_SSAA"], "4");
    assert_eq!(environment["GT4_PGS_HIRES"], "1");
    let pgs: Vec<&String> = environment.keys().filter(|name| name.starts_with("GT4_PGS_")).collect();
    assert_eq!(pgs.len(), 2, "only SSAA and HIRES exist in the paraLLEl-GS build: {pgs:?}");
}

#[test]
fn high_resolution_output_is_not_sent_below_four_times_super_sampling() {
    let mut settings = Settings::default();
    settings.set("renderer", "parallel").unwrap();
    settings.set("super_sampling", "2").unwrap();
    settings.set("high_resolution_output", "on").unwrap();
    let environment = env_map(&launch::game_environment(&settings));
    assert_eq!(environment["GT4_PGS_SSAA"], "2");
    assert!(!environment.contains_key("GT4_PGS_HIRES"));
    assert!(!settings.is_active("high_resolution_output"));
}

#[test]
fn coregs11_defaults_send_the_documented_variables() {
    let environment = env_map(&launch::game_environment(&Settings::default()));
    assert_eq!(environment["PS2_SCALE"], "1");
    assert!(!environment.contains_key("GT4_PRESENT_FILTER"), "automatic sends no filter");
    assert!(!environment.contains_key("GT4_FSR_SHARPNESS"));
    assert_eq!(environment["GT4_DEINTERLACE"], "auto");
    assert_eq!(environment["GT4_AA"], "off");
}

#[test]
fn internal_scale_maps_to_ps2_scale_for_coregs11_only() {
    let mut settings = Settings::default();
    settings.set("internal_scale", "3").unwrap();
    assert_eq!(env_map(&launch::game_environment(&settings))["PS2_SCALE"], "3");
    assert!(settings.is_active("internal_scale"));
    settings.set("renderer", "parallel").unwrap();
    assert_eq!(env_map(&launch::game_environment(&settings))["PS2_SCALE"], "1");
    assert!(!settings.is_active("internal_scale"));
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
fn fsr_sharpness_is_sent_only_with_the_fsr_filter() {
    let mut settings = Settings::default();
    settings.set("fsr_sharpness", "0").unwrap();
    assert!(!env_map(&launch::game_environment(&settings)).contains_key("GT4_FSR_SHARPNESS"));
    assert!(!settings.applies("fsr_sharpness"));
    settings.set("present_filter", "fsr").unwrap();
    assert!(settings.is_active("fsr_sharpness"));
    assert_eq!(env_map(&launch::game_environment(&settings))["GT4_FSR_SHARPNESS"], "0");
    settings.set("fsr_sharpness", "0.2").unwrap();
    assert_eq!(env_map(&launch::game_environment(&settings))["GT4_FSR_SHARPNESS"], "0.2");
    assert!(settings.set("fsr_sharpness", "3").is_err());
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
