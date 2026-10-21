//! Settings table and config file: validation, defaults and round trips.

use gt4l_core::config::{self, ConfigFile};
use gt4l_core::settings::{self, Settings, SettingError, SETTINGS};

#[test]
fn every_definition_id_is_unique_and_has_a_valid_default() {
    let mut seen = std::collections::BTreeSet::new();
    for definition in SETTINGS {
        assert!(seen.insert(definition.id), "duplicate id {}", definition.id);
        assert!(
            settings::validate(definition, definition.default).is_ok(),
            "default of {} is not allowed",
            definition.id
        );
    }
}

#[test]
fn renderer_defaults_to_pcsx2_gs_and_coregs_2_0_cannot_be_selected() {
    let mut settings = Settings::default();
    assert_eq!(settings.get("renderer"), "pcsx2");
    assert_eq!(
        settings.set("renderer", "coregs20"),
        Err(SettingError::InvalidValue { id: "renderer".into(), value: "coregs20".into() })
    );
    assert_eq!(settings.get("renderer"), "pcsx2", "a refused value leaves the state unchanged");
}

#[test]
fn unknown_ids_and_bad_toggle_values_are_refused() {
    let mut settings = Settings::default();
    assert!(matches!(settings.set("gamma", "2"), Err(SettingError::UnknownSetting(_))));
    assert!(settings.set("fullscreen", "yes").is_err());
    assert!(settings.set("super_sampling", "3").is_err());
    assert!(settings.set("high_resolution_output", "on").is_err());
}

#[test]
fn input_resolution_defaults_to_480p() {
    assert_eq!(Settings::default().get("input_resolution"), "480p");
}

#[test]
fn config_round_trip_keeps_values_and_paths() {
    let mut config = ConfigFile::default();
    config.settings.set("vsync", "fast").unwrap();
    config.settings.set("renderer", "coregs11").unwrap();
    config.settings.set("fsr_sharpness", "30").unwrap();
    config.settings.set("internal_scale", "3").unwrap();
    config.settings.set_gs("upscale", "2").unwrap();
    config.settings.set_gs("hw_download_mode", "unsynchronized").unwrap();
    config.disc_path = Some(r#"D:\Games\Gran Turismo 4 "quoted" (USA).iso"#.into());
    config.dev_game_dir = Some(r"C:\Antigravity\build\game".into());
    let text = config::render(&config);
    let (parsed, warnings) = config::parse(&text).unwrap();
    assert!(warnings.is_empty(), "{warnings:?}");
    assert_eq!(parsed, config);
}

#[test]
fn invalid_values_and_unknown_keys_fall_back_with_warnings() {
    let text = "schema_version = 1\n[settings]\nvsync = \"maybe\"\nflux = \"on\"\nfullscreen = \"off\"\n";
    let (config, warnings) = config::parse(text).unwrap();
    assert_eq!(config.settings.get("vsync"), "on");
    assert_eq!(config.settings.get("fullscreen"), "off");
    assert_eq!(warnings.len(), 2, "{warnings:?}");
}

#[test]
fn newer_schema_is_refused_and_syntax_errors_are_reported() {
    assert!(matches!(config::parse("schema_version = 9\n"), Err(config::ConfigError::NewerSchema(9))));
    assert!(matches!(config::parse("[settings\n"), Err(config::ConfigError::Syntax(_))));
}

#[test]
fn load_of_a_damaged_file_moves_it_aside() {
    let directory = std::env::temp_dir().join(format!("gt4l-cfg-{}", std::process::id()));
    std::fs::create_dir_all(&directory).unwrap();
    let path = directory.join("settings.toml");
    std::fs::write(&path, "[settings\n").unwrap();
    let (config, warnings) = config::load(&path);
    assert_eq!(config, ConfigFile::default());
    assert_eq!(warnings.len(), 1);
    assert!(directory.join("settings.toml.bad").exists());
    assert!(!path.exists());
    std::fs::remove_dir_all(&directory).unwrap();
}
