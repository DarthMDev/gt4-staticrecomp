//! Pure rules of the command layer: Settings rows, Controls labels, key names and the About text.
//! Nothing here touches the disk or a window, so every rule is tested without the UI.

use gt4controls::{Controls, Curve, TriggerTuning, BUTTON_INPUTS};
use gt4l_core::settings::{self, Kind, Page, Settings, SETTINGS};
use serde::Serialize;

/// Settings tabs in display order (the page keys the UI sends back).
pub const SETTINGS_PAGES: &[(&str, &str)] = &[("graphics", "Graphics"), ("display", "Display"), ("advanced", "Advanced")];

/// The page a settings tab key stands for, or `None` for an unknown key.
pub fn page_for_key(key: &str) -> Option<Page> {
    match key {
        "graphics" => Some(Page::Graphics),
        "display" => Some(Page::Display),
        "advanced" => Some(Page::Advanced),
        _ => None,
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ChoiceDto {
    pub value: String,
    pub label: String,
    /// `false` = shown greyed out ("coming soon") and never sent.
    pub selectable: bool,
}

/// One Settings row, generated from the `gt4l-core` table. The page never hard-codes an option.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RowDto {
    pub id: String,
    /// Group caption; the page prints it above the first row of each group.
    pub group: String,
    pub title: String,
    pub info: String,
    /// `choice`, `toggle` or `percent`.
    pub kind: String,
    pub choices: Vec<ChoiceDto>,
    /// Stored value: a choice value, `on`/`off` for a toggle, or the percentage as text.
    pub value: String,
    /// `false` when the option cannot take effect now (not connected or locked).
    pub enabled: bool,
}

/// Rows of one settings tab, in table order. Rows that do not apply to the selected renderer are
/// left out, so only options the game reads are shown.
pub fn rows_for_page(settings: &Settings, page: Page) -> Vec<RowDto> {
    SETTINGS
        .iter()
        .filter(|definition| definition.page == page && settings.applies(definition.id))
        .map(|definition| {
            let (kind, choices) = match definition.kind {
                Kind::Choice(choices) => (
                    "choice",
                    choices
                        .iter()
                        .map(|choice| ChoiceDto {
                            value: choice.value.to_string(),
                            label: choice.label.to_string(),
                            selectable: choice.selectable,
                        })
                        .collect(),
                ),
                Kind::Toggle => ("toggle", Vec::new()),
                Kind::Percent => ("percent", Vec::new()),
            };
            RowDto {
                id: definition.id.to_string(),
                group: definition.group.to_string(),
                title: definition.title.to_string(),
                info: definition.info.to_string(),
                kind: kind.to_string(),
                choices,
                value: settings.get(definition.id).to_string(),
                enabled: definition.connected && settings.is_active(definition.id),
            }
        })
        .collect()
}

/// The Controls-page inputs in display order: the 16 digital inputs of `docs/CONTROLS.md`.
pub const CONTROL_ORDER: [&str; 16] = [
    "dpad_up", "dpad_right", "dpad_down", "dpad_left", "cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2",
    "l3", "r3", "select", "start",
];

/// The PS2 name of a Controls-page input, as shown on the page.
pub fn ps2_name(input: &str) -> &'static str {
    match input {
        "dpad_up" => "D-pad up",
        "dpad_right" => "D-pad right",
        "dpad_down" => "D-pad down",
        "dpad_left" => "D-pad left",
        "cross" => "Cross",
        "circle" => "Circle",
        "square" => "Square",
        "triangle" => "Triangle",
        "l1" => "L1",
        "r1" => "R1",
        "l2" => "L2 (from LT)",
        "r2" => "R2 (from RT)",
        "l3" => "L3 (left stick click)",
        "r3" => "R3 (right stick click)",
        "select" => "Select",
        _ => "Start",
    }
}

/// A binding as the user reads it: `Controller: A`, `Keyboard: Z`, or the raw text otherwise.
pub fn describe_source(source: &str) -> String {
    let base = source.split(" | ").next().unwrap_or(source);
    match base.split_once(':') {
        Some(("xinput", name)) => format!("Controller: {}", xinput_name(name)),
        Some(("key", name)) => format!("Keyboard: {name}"),
        _ => base.to_string(),
    }
}

fn xinput_name(name: &str) -> &str {
    match name {
        "LThumb" => "left stick click",
        "RThumb" => "right stick click",
        other => other,
    }
}

/// The short caption on a control of the drawing.
pub fn short_label(sources: &[String]) -> String {
    let Some(first) = sources.first() else {
        return "-".to_string();
    };
    let name = first.split_once(':').map_or(first.as_str(), |(_, name)| name);
    match name {
        "LThumb" => "L3".into(),
        "RThumb" => "R3".into(),
        "Back" => "Select".into(),
        "DpadUp" => "Up".into(),
        "DpadDown" => "Down".into(),
        "DpadLeft" => "Left".into(),
        "DpadRight" => "Right".into(),
        "Backspace" => "Bksp".into(),
        other => other.into(),
    }
}

/// Maps the key text the page sends to a key name the controls file accepts, or `None`.
pub fn key_name(text: &str) -> Option<String> {
    let mut characters = text.chars();
    if let (Some(character), None) = (characters.next(), characters.next()) {
        if character.is_ascii_alphanumeric() {
            return Some(character.to_ascii_uppercase().to_string());
        }
    }
    match text {
        "Enter" | "Backspace" | "Tab" | "Space" | "Up" | "Down" | "Left" | "Right" => Some(text.to_string()),
        _ => None,
    }
}

#[derive(Clone, Debug, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ControlDto {
    pub input: String,
    pub ps2_name: String,
    /// Caption printed on the drawing.
    pub short: String,
    /// Raw sources, e.g. `xinput:A`, `key:Z`.
    pub sources: Vec<String>,
    /// Sources as the user reads them.
    pub described: Vec<String>,
}

/// Controls-page rows in display order.
pub fn control_rows(controls: &Controls) -> Vec<ControlDto> {
    CONTROL_ORDER
        .iter()
        .map(|input| {
            let sources: Vec<String> = controls.sources(input).unwrap_or(&[]).to_vec();
            ControlDto {
                input: input.to_string(),
                ps2_name: ps2_name(input).to_string(),
                short: short_label(&sources),
                described: sources.iter().map(|source| describe_source(source)).collect(),
                sources,
            }
        })
        .collect()
}

#[derive(Clone, Debug, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TriggerDto {
    /// `l2` or `r2`.
    pub id: String,
    /// Deadzone in percent of travel, 0-100.
    pub deadzone: f32,
    /// `linear`, `quadratic` or `cubic`.
    pub curve: String,
}

pub fn trigger_rows(controls: &Controls) -> Vec<TriggerDto> {
    ["l2", "r2"]
        .iter()
        .map(|id| {
            let tuning = controls.trigger_tuning(id);
            TriggerDto {
                id: id.to_string(),
                deadzone: (tuning.deadzone * 100.0 * 10.0).round() / 10.0,
                curve: tuning.curve.as_str().to_string(),
            }
        })
        .collect()
}

/// Parses the curve name the page sends.
pub fn curve_from_name(name: &str) -> Option<Curve> {
    match name {
        "linear" => Some(Curve::Linear),
        "quadratic" => Some(Curve::Quadratic),
        "cubic" => Some(Curve::Cubic),
        _ => None,
    }
}

/// Trigger tuning from a percentage and a curve name. Percentages outside 0-100 are refused.
pub fn trigger_tuning(deadzone_percent: f32, curve: &str) -> Result<TriggerTuning, String> {
    if !(0.0..=100.0).contains(&deadzone_percent) {
        return Err("the deadzone must be between 0 and 100 percent".into());
    }
    let curve = curve_from_name(curve).ok_or_else(|| format!("unknown curve '{curve}'"))?;
    Ok(TriggerTuning { deadzone: deadzone_percent / 100.0, curve })
}

/// Whether an input is one the page may rebind (the digital inputs of the controls file).
pub fn bindable(input: &str) -> bool {
    BUTTON_INPUTS.contains(&input)
}

/// Text of the About page. The trademark notice matches NOTICE.md and the installer.
pub fn about_text(version: &str) -> String {
    format!(
        "GT4Recomp {version}\n\n\
         GT4Recomp is an unofficial fan project. It is not affiliated with, endorsed by or sponsored by \
         Sony Interactive Entertainment Inc. or Polyphony Digital Inc.\n\n\
         \"Gran Turismo\", \"GT\", the Gran Turismo 4 logo, the GT emblem and the Gran Turismo 4 cover \
         artwork are trademarks or copyrighted works of Sony Interactive Entertainment Inc. and/or Polyphony \
         Digital Inc. They are used here only to identify the game. No game data is included in the source \
         repository. You need your own copy of Gran Turismo 4 (USA, v2.00) to play."
    )
}

/// Open-source notices of the launcher's user interface. Full texts are installed with the program.
pub fn licenses_text() -> &'static str {
    "GT4Recomp is MIT licensed.\n\n\
     The launcher interface is built with Tauri 2 (MIT or Apache-2.0) and runs in Microsoft Edge WebView2, \
     the Windows system web view (Microsoft WebView2 terms).\n\n\
     Typefaces: Roboto and Roboto Condensed (Apache-2.0), bundled in the launcher.\n\n\
     The launcher uses the Rust crates listed in launcher/Cargo.lock. paraLLEl-GS (LGPL-3.0-or-later) is \
     a separate library and is not part of the launcher's interface."
}

#[cfg(test)]
mod tests {
    use super::*;
    use gt4l_core::launch;
    use std::collections::BTreeMap;

    fn env(settings: &Settings) -> BTreeMap<String, String> {
        launch::game_environment(settings).into_iter().collect()
    }

    #[test]
    fn graphics_rows_are_generated_with_groups_in_table_order() {
        let rows = rows_for_page(&Settings::default(), Page::Graphics);
        let groups: Vec<&str> = rows.iter().map(|row| row.group.as_str()).collect();
        assert_eq!(groups.first(), Some(&"Renderer"));
        assert!(groups.contains(&"CoreGS 1.1"));
        assert!(!groups.contains(&"paraLLEl-GS"), "paraLLEl-GS rows are hidden under CoreGS 1.1");
    }

    #[test]
    fn coming_soon_renderer_is_listed_but_cannot_be_chosen() {
        let rows = rows_for_page(&Settings::default(), Page::Graphics);
        let renderer = rows.iter().find(|row| row.id == "renderer").unwrap();
        assert_eq!(renderer.value, "coregs11");
        let soon = renderer.choices.iter().find(|choice| choice.value == "coregs20").unwrap();
        assert!(!soon.selectable);
        assert!(soon.label.contains("coming soon"));
    }

    #[test]
    fn each_renderer_shows_only_its_own_rows() {
        let mut settings = Settings::default();
        let core: Vec<String> = rows_for_page(&settings, Page::Graphics).into_iter().map(|row| row.id).collect();
        assert!(core.contains(&"internal_scale".to_string()));
        assert!(!core.contains(&"super_sampling".to_string()));
        settings.set("renderer", "parallel").unwrap();
        let parallel: Vec<String> = rows_for_page(&settings, Page::Graphics).into_iter().map(|row| row.id).collect();
        assert!(parallel.contains(&"super_sampling".to_string()));
        assert!(parallel.contains(&"high_resolution_output".to_string()));
        assert!(!parallel.contains(&"internal_scale".to_string()));
    }

    #[test]
    fn fsr_sharpness_row_is_a_percent_slider_shown_only_with_fsr() {
        let mut settings = Settings::default();
        assert!(!rows_for_page(&settings, Page::Graphics).iter().any(|row| row.id == "fsr_sharpness"));
        settings.set("present_filter", "fsr").unwrap();
        let row = rows_for_page(&settings, Page::Graphics).into_iter().find(|row| row.id == "fsr_sharpness").unwrap();
        assert_eq!(row.kind, "percent");
        assert_eq!(row.value, "50", "default is 50 percent");
        assert!(row.enabled);
    }

    #[test]
    fn fsr_percent_reaches_the_game_unchanged() {
        let mut settings = Settings::default();
        settings.set("present_filter", "fsr").unwrap();
        for percent in ["0", "50", "100"] {
            settings.set("fsr_sharpness", percent).unwrap();
            assert_eq!(env(&settings)["GT4_FSR_SHARPNESS"], percent);
        }
        assert!(settings.set("fsr_sharpness", "101").is_err());
        assert!(settings.set("fsr_sharpness", "-1").is_err());
        assert!(settings.set("fsr_sharpness", "half").is_err());
    }

    #[test]
    fn high_resolution_output_is_a_choice_and_only_for_paraLLEl_gs() {
        let mut settings = Settings::default();
        assert!(!rows_for_page(&settings, Page::Graphics).iter().any(|row| row.id == "high_resolution_output"));
        settings.set("renderer", "parallel").unwrap();
        let row = rows_for_page(&settings, Page::Graphics)
            .into_iter()
            .find(|row| row.id == "high_resolution_output")
            .unwrap();
        assert_eq!(row.kind, "choice");
        let values: Vec<&str> = row.choices.iter().map(|choice| choice.value.as_str()).collect();
        assert_eq!(values, vec!["off", "2", "4"]);
        assert_eq!(row.value, "off");
    }

    #[test]
    fn hires_option_maps_to_the_build_environment_for_each_factor() {
        let mut settings = Settings::default();
        settings.set("renderer", "parallel").unwrap();
        settings.set("super_sampling", "2").unwrap();
        let off = env(&settings);
        assert_eq!(off["GT4_RENDERER"], "parallel");
        assert_eq!(off["GT4_PGS_SSAA"], "2");
        assert!(!off.contains_key("GT4_PGS_HIRES"));
        for factor in ["2", "4"] {
            settings.set("high_resolution_output", factor).unwrap();
            let on = env(&settings);
            assert_eq!(on["GT4_PGS_HIRES"], factor);
            assert_eq!(on["GT4_PGS_SSAA"], "16");
        }
    }

    #[test]
    fn launch_environment_for_each_renderer_sends_only_its_variables() {
        let mut settings = Settings::default();
        let core = env(&settings);
        assert_eq!(core["GT4_RENDERER"], "donor");
        assert_eq!(core["PS2_SCALE"], "1");
        assert!(core.contains_key("GT4_DEINTERLACE") && core.contains_key("GT4_AA"));
        assert!(!core.keys().any(|name| name.starts_with("GT4_PGS_")));

        settings.set("renderer", "parallel").unwrap();
        let parallel = env(&settings);
        assert_eq!(parallel["GT4_RENDERER"], "parallel");
        assert_eq!(parallel["PS2_SCALE"], "1", "internal resolution is CoreGS 1.1 only");
        assert!(!parallel.contains_key("GT4_DEINTERLACE"));
        assert!(parallel.keys().any(|name| name.starts_with("GT4_PGS_")));
    }

    #[test]
    fn controls_rows_cover_the_sixteen_digital_inputs_with_names_and_bindings() {
        let rows = control_rows(&Controls::default());
        assert_eq!(rows.len(), 16);
        for input in BUTTON_INPUTS {
            assert!(CONTROL_ORDER.contains(input), "{input} is missing from the page");
        }
        let cross = rows.iter().find(|row| row.input == "cross").unwrap();
        assert_eq!(cross.ps2_name, "Cross");
        assert_eq!(cross.short, "A");
        assert!(cross.described.contains(&"Controller: A".to_string()));
        assert!(cross.described.contains(&"Keyboard: Z".to_string()));
        let l2 = rows.iter().find(|row| row.input == "l2").unwrap();
        assert_eq!(l2.ps2_name, "L2 (from LT)");
    }

    #[test]
    fn trigger_tuning_checks_the_percentage_and_the_curve() {
        assert!(trigger_tuning(-1.0, "linear").is_err());
        assert!(trigger_tuning(101.0, "linear").is_err());
        assert!(trigger_tuning(10.0, "exponential").is_err());
        let tuning = trigger_tuning(20.0, "cubic").unwrap();
        assert!((tuning.deadzone - 0.2).abs() < 1e-6);
        assert_eq!(tuning.curve, Curve::Cubic);
        let rows = trigger_rows(&Controls::default());
        assert_eq!(rows[0].id, "l2");
        assert!((rows[0].deadzone - 11.8).abs() < 0.05, "default 30 of 255 is 11.8 percent");
    }

    #[test]
    fn key_names_follow_the_controls_file() {
        assert_eq!(key_name("z").as_deref(), Some("Z"));
        assert_eq!(key_name("7").as_deref(), Some("7"));
        assert_eq!(key_name("Enter").as_deref(), Some("Enter"));
        assert_eq!(key_name("F5"), None, "function keys are not offered for binding");
    }

    #[test]
    fn short_labels_and_descriptions_are_readable() {
        assert_eq!(short_label(&["xinput:LThumb".to_string()]), "L3");
        assert_eq!(short_label(&[]), "-");
        assert_eq!(describe_source("xinput:LThumb"), "Controller: left stick click");
        assert_eq!(describe_source("key:Enter | repeat"), "Keyboard: Enter");
    }

    #[test]
    fn about_text_carries_the_trademark_notice_and_the_ui_licenses() {
        let text = about_text("1.0.0");
        assert!(text.starts_with("GT4Recomp 1.0.0"));
        assert!(text.contains("not affiliated with, endorsed by or sponsored by"));
        assert!(licenses_text().contains("Tauri 2") && !licenses_text().contains("Slint"));
    }

    #[test]
    fn settings_pages_map_to_their_tables() {
        for (key, _) in SETTINGS_PAGES {
            assert!(page_for_key(key).is_some());
        }
        assert!(page_for_key("controls").is_none(), "Controls is not a settings table page");
    }
}
