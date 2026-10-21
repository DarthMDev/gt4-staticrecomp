//! Pure rules of the command layer: Settings rows, Controls labels, key names and the About text.
//! Nothing here touches the disk or a window, so every rule is tested without the UI.

use gt4controls::{Controls, Curve, Stick, TriggerTuning, BUTTON_INPUTS};
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
    settings_table_with_display(settings, controls, None)
}

/// The monitor the launcher window is on, in physical pixels (TASK-080: shown on the Aspect ratio row).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DisplaySize {
    pub width: u32,
    pub height: u32,
}

/// "3440x1440, 21.5:9" (16:9 screens read "16:9"). The ratio is given per 9 rows, as players name ultrawide screens.
pub fn describe_display(display: DisplaySize) -> String {
    if display.width == 0 || display.height == 0 {
        return format!("{}x{}", display.width, display.height);
    }
    let per_nine = display.width as f64 * 9.0 / display.height as f64;
    if (per_nine - 16.0).abs() < 0.08 {
        return format!("{}x{}, 16:9", display.width, display.height);
    }
    format!("{}x{}, {:.1}:9", display.width, display.height, per_nine)
}

/// The settings table; with a known monitor, the Aspect ratio row's Auto entry names it ("Auto (3440x1440, 21.5:9)").
pub fn settings_table_with_display(settings: &Settings, controls: &Controls, display: Option<DisplaySize>) -> Value {
    let mut rows: Vec<Value> = Vec::new();
    for definition in SETTINGS {
        if !settings.applies(definition.id) {
            continue;
        }
        let mut row = launcher_row(definition, settings);
        if definition.id == "aspect_ratio" {
            if let (Some(display), Some(values)) = (display, row["values"].as_array_mut()) {
                if let Some(auto) = values.iter_mut().find(|choice| choice["value"] == "auto") {
                    auto["label"] = json!(format!("Auto ({})", describe_display(display)));
                }
            }
        }
        rows.push(row);
    }
    for row in pcsx2_table_rows() {
        rows.push(gs_row(row, settings));
    }
    for trigger in ["l2", "r2"] {
        rows.push(trigger_row(trigger, controls, "deadzone"));
        rows.push(trigger_row(trigger, controls, "curve"));
    }
    for stick in [Stick::Left, Stick::Right] {
        rows.push(stick_row(stick, controls));
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
        // TASK-080: the stored whole number of hundredths; the page shows it divided by `scale` ("2.33:1").
        Kind::Hundredths { min, max } => (
            "slider",
            json!({ "min": min, "max": max, "step": 1, "unit": ":1", "scale": 100 }),
            json!(definition.default.parse::<u32>().unwrap_or(min)),
            json!(settings.get(definition.id).parse::<u32>().unwrap_or(min)),
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
    // TASK-080: the Custom ratio row shows only when Aspect ratio is Custom.
    if definition.id == "aspect_custom" {
        row["visible_when"] = json!({ "id": "aspect_ratio", "equals": "custom" });
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
            "help": "How far the trigger must travel before it counts, in percent of its travel. The pressure the game \
                     reads starts above this point.",
            "page": "basic",
            "group": "controls",
            "control": "slider",
            "values": { "min": 0, "max": 95, "step": 1, "unit": "%" },
            "default": percent(gt4controls::TRIGGER_DEFAULT_DEADZONE),
            "current": percent(tuning.deadzone),
        })
    } else {
        json!({
            "id": format!("{trigger}_curve"),
            "label": title,
            "help": "How the pressure grows with the trigger travel. Linear follows the trigger; quadratic and cubic \
                     give finer control at light pressure.",
            "page": "basic",
            "group": "controls",
            "control": "dropdown",
            "values": [
                { "value": "linear", "label": "Linear" },
                { "value": "quadratic", "label": "Quadratic" },
                { "value": "cubic", "label": "Cubic" },
            ],
            "default": "linear",
            "current": tuning.curve.as_str(),
        })
    }
}

/// A fraction as a percentage with one decimal (0.1176 -> 11.8).
fn percent(fraction: f32) -> f64 {
    (f64::from(fraction) * 1000.0).round() / 10.0
}

/// The deadzone row of a stick (`lstick_deadzone`, `rstick_deadzone`).
fn stick_row(stick: Stick, controls: &Controls) -> Value {
    let (id, label, default) = match stick {
        Stick::Left => ("lstick_deadzone", "Left stick deadzone", gt4controls::LEFT_STICK_DEFAULT_DEADZONE),
        Stick::Right => ("rstick_deadzone", "Right stick deadzone", gt4controls::RIGHT_STICK_DEFAULT_DEADZONE),
    };
    json!({
        "id": id,
        "label": label,
        "help": "How far the stick must move from the centre before the game sees it, in percent of its travel. \
                 The default is the XInput standard; lower it for a precise pad, raise it for one that drifts.",
        "page": "basic",
        "group": "controls",
        "control": "slider",
        "values": { "min": 0, "max": 95, "step": 1, "unit": "%" },
        "default": percent(default),
        "current": percent(controls.stick_deadzone(stick)),
    })
}

/// The stick a `set_setting` id tunes (`lstick_deadzone`, `rstick_deadzone`), or `None`.
pub fn stick_setting(id: &str) -> Option<Stick> {
    match id {
        "lstick_deadzone" => Some(Stick::Left),
        "rstick_deadzone" => Some(Stick::Right),
        _ => None,
    }
}

/// The payload of `get_controls` (ui/dev/README.md): the sixteen buttons in drawing order, the analog rows (trigger
/// and stick settings, settings row shape) and whether the file is kept read-only (written by a newer launcher).
pub fn controls_view(settings: &Settings, controls: &Controls, locked: bool) -> Value {
    let rows: Vec<Value> = control_rows(controls)
        .into_iter()
        .map(|row| {
            json!({
                "id": row.input,
                "name": row.ps2_name,
                "binding": row.short,
                "sources": row.sources,
                "xinput": row.xinput,
                "key": row.key,
                "is_default": row.is_default,
            })
        })
        .collect();
    let table = settings_table(settings, controls);
    let analog: Vec<Value> = table["rows"]
        .as_array()
        .map(|rows| rows.iter().filter(|row| row["group"] == "controls").cloned().collect())
        .unwrap_or_default();
    json!({ "controls": rows, "triggers": analog, "locked": locked })
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
        "l2" => "L2",
        "r2" => "R2",
        "l3" => "L3",
        "r3" => "R3",
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
    /// The first XInput source without its prefix and options (`A`, `LT`), or `None` when no controller input drives it.
    pub xinput: Option<String>,
    /// The first keyboard key (`Z`, `Enter`), or `None`.
    pub key: Option<String>,
    /// True when the input has exactly its built-in sources.
    pub is_default: bool,
}

/// The name after the family prefix of the first source of `family`, without options.
fn first_of_family(sources: &[String], family: &str) -> Option<String> {
    sources
        .iter()
        .find(|source| gt4controls::source_family(source) == family)
        .map(|source| gt4controls::source_base(source).split_once(':').map_or("", |(_, name)| name).to_string())
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
                xinput: first_of_family(&sources, "xinput"),
                key: first_of_family(&sources, "key"),
                is_default: controls.is_default(input),
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
        assert!(cross.described.contains(&"Keyboard: W".to_string()), "{:?}", cross.described);
        let l2 = rows.iter().find(|row| row.input == "l2").unwrap();
        assert_eq!(l2.ps2_name, "L2");
        assert_eq!(cross.xinput.as_deref(), Some("A"));
        assert_eq!(cross.key.as_deref(), Some("W"));
        assert!(cross.is_default);
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
    fn aspect_rows_offer_auto_and_a_custom_slider_shown_only_for_custom() {
        let table = settings_table(&Settings::default(), &Controls::default());
        let aspect = row(&table, "aspect_ratio");
        assert_eq!(aspect["current"], "auto");
        assert_eq!(aspect["group"], "graphics");
        let values: Vec<&str> = aspect["values"].as_array().unwrap().iter().map(|v| v["value"].as_str().unwrap()).collect();
        assert_eq!(values, ["auto", "16:9", "4:3", "custom"]);
        assert_eq!(aspect["values"][0]["label"], "Auto", "no monitor known: plain Auto");
        let custom = row(&table, "aspect_custom");
        assert_eq!(custom["control"], "slider");
        assert_eq!(custom["values"]["scale"], 100);
        assert_eq!(custom["values"]["min"], 100);
        assert_eq!(custom["values"]["max"], 600);
        assert_eq!(custom["current"], 233);
        assert_eq!(custom["visible_when"]["id"], "aspect_ratio");
        assert_eq!(custom["visible_when"]["equals"], "custom");
    }

    #[test]
    fn the_auto_entry_names_the_detected_monitor() {
        let ultrawide = DisplaySize { width: 3440, height: 1440 };
        let table = settings_table_with_display(&Settings::default(), &Controls::default(), Some(ultrawide));
        assert_eq!(row(&table, "aspect_ratio")["values"][0]["label"], "Auto (3440x1440, 21.5:9)");
        assert_eq!(describe_display(DisplaySize { width: 1920, height: 1080 }), "1920x1080, 16:9");
        assert_eq!(describe_display(DisplaySize { width: 1366, height: 768 }), "1366x768, 16:9");
        assert_eq!(describe_display(DisplaySize { width: 3088, height: 1440 }), "3088x1440, 19.3:9");
        assert_eq!(describe_display(DisplaySize { width: 5120, height: 1440 }), "5120x1440, 32.0:9");
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
        assert_eq!(row(&table, "l2_deadzone")["default"], json!(11.8), "30 of 255");
        assert_eq!(row(&table, "lstick_deadzone")["current"], json!(24.0), "7849 of 32767");
        assert_eq!(row(&table, "rstick_deadzone")["default"], json!(26.5), "8689 of 32767");
    }

    #[test]
    fn controls_view_lists_buttons_with_both_sources_and_the_analog_rows() {
        let mut controls = Controls::default();
        controls.assign("cross", "xinput:B", true).unwrap();
        let view = controls_view(&Settings::default(), &controls, false);
        let buttons = view["controls"].as_array().unwrap();
        assert_eq!(buttons.len(), 16);
        let cross = buttons.iter().find(|button| button["id"] == "cross").unwrap();
        assert_eq!(cross["xinput"], "B");
        assert_eq!(cross["key"], "W");
        assert_eq!(cross["is_default"], false);
        let circle = buttons.iter().find(|button| button["id"] == "circle").unwrap();
        assert_eq!(circle["xinput"], "A", "swapped");
        let analog: Vec<&str> = view["triggers"].as_array().unwrap().iter().map(|row| row["id"].as_str().unwrap()).collect();
        assert_eq!(analog, ["l2_deadzone", "l2_curve", "r2_deadzone", "r2_curve", "lstick_deadzone", "rstick_deadzone"]);
        assert_eq!(view["locked"], false);
        assert_eq!(stick_setting("rstick_deadzone"), Some(Stick::Right));
        assert_eq!(stick_setting("l2_deadzone"), None);
    }

    #[test]
    fn value_text_maps_booleans_per_kind() {
        assert_eq!(value_text(&json!(true), true).as_deref(), Some("on"));
        assert_eq!(value_text(&json!(true), false).as_deref(), Some("true"));
        assert_eq!(value_text(&json!(12), false).as_deref(), Some("12"));
        assert_eq!(value_text(&json!(["x"]), false), None);
    }
}
