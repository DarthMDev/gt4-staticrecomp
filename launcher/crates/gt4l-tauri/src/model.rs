//! Pure rules of the command layer: Settings rows, Controls labels, key names and the About text.
//! Nothing here touches the disk or a window, so every rule is tested without the UI.

use gt4controls::{Controls, Curve, TriggerTuning, BUTTON_INPUTS};
use gt4l_core::gs_table;
use gt4l_core::settings::{self, Kind, Page, SettingDef, Settings, SETTINGS};
use serde::Serialize;
use serde_json::{json, Value};

// Settings table (the contract of the UI branch, ui/dev/README.md) ----------------------------------

/// The upscale limit sent until gt4-gs reports the device limit.
pub const MAX_UPSCALE_DEFAULT: u32 = 12;

/// Settings rows that are never sent to the UI: the PCSX2 renderer's own Renderer API, and the present-mode rows the
/// launcher derives from `vsync` (scripts/gs_settings.py, gs_table::render_ini callers).
const DERIVED_TABLE_IDS: &[&str] = &["renderer", "vsync", "mailbox"];

/// The rows of the PCSX2 GS table the UI may show: offered, not mods (release 1.1.0), not derived.
pub fn pcsx2_table_rows() -> Vec<&'static gs_table::Row> {
    gs_table::offered().into_iter().filter(|row| !DERIVED_TABLE_IDS.contains(&row.id.as_str())).collect()
}

/// The payload of `get_settings_table`: `{max_upscale, rows}`. Launcher rows come from `SETTINGS`, PCSX2 GS rows from
/// the table file; both use the row shape of ui/dev/README.md. Rows of the other renderer are sent with `visible_when`.
pub fn settings_table(settings: &Settings, controls: &Controls) -> Value {
    let mut rows: Vec<Value> = Vec::new();
    for definition in SETTINGS {
        if !settings.applies(definition.id) {
            continue;
        }
        rows.push(launcher_row(definition, settings));
    }
    for row in pcsx2_table_rows() {
        rows.push(gs_row(row, settings));
    }
    for trigger in ["l2", "r2"] {
        rows.push(trigger_row(trigger, controls, "deadzone"));
        rows.push(trigger_row(trigger, controls, "curve"));
    }
    json!({ "max_upscale": MAX_UPSCALE_DEFAULT, "rows": rows })
}

fn launcher_row(definition: &SettingDef, settings: &Settings) -> Value {
    let group = match definition.page {
        Page::Graphics => "graphics",
        Page::Display => "display",
        Page::Advanced => "",
    };
    let page = if definition.page == Page::Advanced { "advanced" } else { "basic" };
    let (control, values, default, current) = match definition.kind {
        Kind::Choice(choices) => (
            "dropdown",
            Value::Array(
                choices
                    .iter()
                    .map(|choice| json!({ "value": choice.value, "label": choice.label, "disabled": !choice.selectable }))
                    .collect(),
            ),
            json!(definition.default),
            json!(settings.get(definition.id)),
        ),
        Kind::Toggle => ("toggle", Value::Null, json!(definition.default == "on"), json!(settings.is_on(definition.id))),
        Kind::Percent => (
            "slider",
            json!({ "min": 0, "max": 100, "step": 1, "unit": "%" }),
            json!(definition.default.parse::<u32>().unwrap_or(0)),
            json!(settings.get(definition.id).parse::<u32>().unwrap_or(0)),
        ),
    };
    let mut row = json!({
        "id": definition.id,
        "label": definition.title,
        "help": definition.info,
        "page": page,
        "control": control,
        "values": values,
        "default": default,
        "current": current,
        "disabled": !settings.is_active(definition.id),
    });
    if !group.is_empty() {
        row["group"] = json!(group);
    }
    row
}

fn gs_row(row: &gs_table::Row, settings: &Settings) -> Value {
    let control = match row.kind.as_str() {
        "bool" => "toggle",
        "int" | "float" => "slider",
        _ => "dropdown",
    };
    let (values, default, current) = match row.kind.as_str() {
        "bool" => (
            Value::Null,
            json!(row.default == "true"),
            json!(settings.gs_value(&row.id) == "true"),
        ),
        "int" | "float" => {
            let mut bounds = json!({ "min": row.min.unwrap_or(0.0), "max": row.max.unwrap_or(0.0), "step": 1 });
            if row.id == "upscale" {
                bounds["max"] = json!(row.max.unwrap_or(0.0).min(MAX_UPSCALE_DEFAULT as f64));
                bounds["limit"] = json!("max_upscale");
            }
            let number = |text: &str| text.parse::<f64>().ok().map(|n| json!(n)).unwrap_or(Value::Null);
            (bounds, number(&row.default), number(&settings.gs_value(&row.id)))
        }
        _ => (
            Value::Array(
                row.choices
                    .iter()
                    .map(|choice| json!({ "value": choice.name, "label": choice.name, "disabled": choice.status != "ok" }))
                    .collect(),
            ),
            json!(row.default),
            json!(settings.gs_value(&row.id)),
        ),
    };
    let mut out = json!({
        "id": row.id,
        "label": row.label,
        "help": row.help,
        "page": gs_table::ui_page(row),
        "control": control,
        "values": values,
        "default": default,
        "current": current,
        "visible_when": { "id": "renderer", "equals": settings::PCSX2_GS },
    });
    match row.page.as_str() {
        "Graphics" => out["group"] = json!("graphics"),
        "Display" => out["group"] = json!("display"),
        _ => {}
    }
    out
}

fn trigger_row(trigger: &str, controls: &Controls, field: &str) -> Value {
    let tuning = controls.trigger_tuning(trigger);
    let label = if field == "deadzone" { "deadzone" } else { "curve" };
    let title = format!("{} {}", trigger.to_uppercase(), label);
    if field == "deadzone" {
        json!({
            "id": format!("{trigger}_deadzone"),
            "label": title,
            "help": "How far the trigger must be pressed before it counts, in percent of its travel.",
            "page": "basic",
            "group": "controls",
            "control": "slider",
            "values": { "min": 0, "max": 100, "step": 0.5, "unit": "%" },
            "default": 0,
            "current": ((tuning.deadzone * 100.0 * 10.0).round() / 10.0),
        })
    } else {
        json!({
            "id": format!("{trigger}_curve"),
            "label": title,
            "help": "How the trigger reading grows with the press.",
            "page": "basic",
            "group": "controls",
            "control": "dropdown",
            "values": [
                { "value": "linear", "label": "linear" },
                { "value": "quadratic", "label": "quadratic" },
                { "value": "cubic", "label": "cubic" },
            ],
            "default": "linear",
            "current": tuning.curve.as_str(),
        })
    }
}

/// The text a `set_setting` value stands for: booleans as `on`/`off` for launcher toggles and `true`/`false` for the
/// table, numbers as their decimal text, strings as they are. Other JSON shapes are refused.
pub fn value_text(value: &Value, launcher_toggle: bool) -> Option<String> {
    match value {
        Value::Bool(flag) if launcher_toggle => Some(if *flag { "on" } else { "off" }.to_string()),
        Value::Bool(flag) => Some(flag.to_string()),
        Value::Number(number) => Some(number.to_string()),
        Value::String(text) => Some(text.clone()),
        _ => None,
    }
}

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

}

#[cfg(test)]
mod table_tests {
    use super::*;

    fn ids(table: &Value) -> Vec<String> {
        table["rows"].as_array().unwrap().iter().map(|row| row["id"].as_str().unwrap().to_string()).collect()
    }

    fn row<'a>(table: &'a Value, id: &str) -> &'a Value {
        table["rows"].as_array().unwrap().iter().find(|row| row["id"] == id).unwrap()
    }

    #[test]
    fn renderer_dropdown_offers_pcsx2_and_coregs11_and_greys_coregs20() {
        let table = settings_table(&Settings::default(), &Controls::default());
        let renderer = row(&table, "renderer");
        assert_eq!(renderer["current"], "pcsx2");
        assert_eq!(renderer["control"], "dropdown");
        let values = renderer["values"].as_array().unwrap();
        assert_eq!(values.len(), 3);
        assert_eq!(values[2]["value"], "coregs20");
        assert_eq!(values[2]["disabled"], true);
    }

    #[test]
    fn coregs11_hides_its_upscale_fsr_aa_and_deinterlace_options() {
        let mut settings = Settings::default();
        settings.set("renderer", "coregs11").unwrap();
        let listed = ids(&settings_table(&settings, &Controls::default()));
        for hidden in ["internal_scale", "present_filter", "fsr_sharpness", "deinterlacing", "anti_aliasing"] {
            assert!(!listed.contains(&hidden.to_string()), "{hidden} must stay hidden");
        }
    }

    #[test]
    fn mods_rows_are_never_offered_in_release_1_0_0() {
        let table = settings_table(&Settings::default(), &Controls::default());
        for row in table["rows"].as_array().unwrap() {
            assert_ne!(row["page"], "mods");
        }
        assert!(pcsx2_table_rows().iter().all(|row| row.page != "mods"));
    }

    #[test]
    fn pcsx2_rows_show_only_for_the_pcsx2_renderer_and_the_upscale_is_limited() {
        let table = settings_table(&Settings::default(), &Controls::default());
        let upscale = row(&table, "upscale");
        // TASK-049 curated the table: upscale is a preset dropdown (480p to 8K), not a slider. The old slider
        // expectation (max 8, limit max_upscale) encoded the older table. Greying presets above the device limit
        // is not implemented yet (follow-up): the limit is still MAX_UPSCALE_DEFAULT here.
        assert_eq!(upscale["control"], "dropdown");
        let presets: Vec<&str> = upscale["values"].as_array().unwrap().iter().map(|choice| choice["value"].as_str().unwrap()).collect();
        assert!(presets.contains(&"480p") && presets.contains(&"8K"), "presets: {presets:?}");
        assert_eq!(upscale["visible_when"]["equals"], "pcsx2");
        assert_eq!(table["max_upscale"], MAX_UPSCALE_DEFAULT);
    }

    #[test]
    fn derived_rows_and_the_renderer_api_are_not_offered_as_pcsx2_rows() {
        let listed = ids(&settings_table(&Settings::default(), &Controls::default()));
        assert_eq!(listed.iter().filter(|id| *id == "vsync").count(), 1, "vsync is the launcher row only");
        assert!(!pcsx2_table_rows().iter().any(|row| row.id == "mailbox"));
    }

    #[test]
    fn triggers_use_the_row_shape_with_their_ids() {
        let table = settings_table(&Settings::default(), &Controls::default());
        assert_eq!(row(&table, "l2_deadzone")["control"], "slider");
        assert_eq!(row(&table, "r2_curve")["control"], "dropdown");
    }

    #[test]
    fn value_text_maps_booleans_per_kind() {
        assert_eq!(value_text(&json!(true), true).as_deref(), Some("on"));
        assert_eq!(value_text(&json!(true), false).as_deref(), Some("true"));
        assert_eq!(value_text(&json!(12), false).as_deref(), Some("12"));
        assert_eq!(value_text(&json!(["x"]), false), None);
    }
}
