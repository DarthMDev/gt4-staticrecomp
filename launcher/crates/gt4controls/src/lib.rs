//! Controls file for the game (`GT4_CONTROLS_FILE`), schema 1, as specified in W3's `docs/CONTROLS.md`
//! (task/029-input-mapping). The launcher writes the file and validates it before Play; the game's C++
//! reader applies the full rules. Changes here must follow that document.
//!
//! The mirrored tables below follow `docs/CONTROLS.md`. `tests/docs_match.rs` fails when they drift from
//! the document or from `docs/controls/*.toml`.

use std::collections::BTreeMap;
use std::fmt;
use std::path::Path;

pub const SCHEMA_VERSION: u32 = 1;
pub const CONTROLS_FILE_NAME: &str = "controls.toml";

/// Digital inputs (pressure-sensitive ones take analog sources when available).
pub const BUTTON_INPUTS: &[&str] = &[
    "dpad_up", "dpad_right", "dpad_down", "dpad_left", "cross", "circle", "square", "triangle", "l1", "r1",
    "l2", "r2", "l3", "r3", "select", "start",
];
/// Stick axes.
pub const STICK_INPUTS: &[&str] = &["rstick_x", "rstick_y", "lstick_x", "lstick_y"];

const XINPUT_BUTTONS: &[&str] = &[
    "A", "B", "X", "Y", "LB", "RB", "Back", "Start", "LThumb", "RThumb", "DpadUp", "DpadDown", "DpadLeft",
    "DpadRight",
];
const XINPUT_TRIGGERS: &[&str] = &["LT", "RT"];
const XINPUT_AXES: &[&str] = &["LX", "LY", "RX", "RY"];
const RESERVED_KEYS: &[&str] = &["F7", "F8", "F9", "F11"];

/// Built-in mapping: each input and its default sources (the game's built-in map).
pub fn default_bindings() -> BTreeMap<String, Vec<String>> {
    let entries: &[(&str, &[&str])] = &[
        ("dpad_up", &["xinput:DpadUp", "key:Up"]),
        ("dpad_right", &["xinput:DpadRight", "key:Right"]),
        ("dpad_down", &["xinput:DpadDown", "key:Down"]),
        ("dpad_left", &["xinput:DpadLeft", "key:Left"]),
        ("cross", &["xinput:A", "key:Z"]),
        ("circle", &["xinput:B", "key:X"]),
        ("square", &["xinput:X", "key:A"]),
        ("triangle", &["xinput:Y", "key:S"]),
        ("l1", &["xinput:LB", "key:Q"]),
        ("r1", &["xinput:RB", "key:W"]),
        ("l2", &["xinput:LT", "key:1"]),
        ("r2", &["xinput:RT", "key:2"]),
        ("l3", &["xinput:LThumb", "key:C"]),
        ("r3", &["xinput:RThumb", "key:V"]),
        ("select", &["xinput:Back", "key:Backspace"]),
        ("start", &["xinput:Start", "key:Enter"]),
        ("rstick_x", &["xinput:RX"]),
        ("rstick_y", &["xinput:RY | invert=true"]),
        ("lstick_x", &["xinput:LX"]),
        ("lstick_y", &["xinput:LY | invert=true"]),
    ];
    entries
        .iter()
        .map(|(name, sources)| (name.to_string(), sources.iter().map(|source| source.to_string()).collect()))
        .collect()
}

#[derive(Clone, Debug, PartialEq)]
pub struct Controls {
    bindings: BTreeMap<String, Vec<String>>,
}

impl Default for Controls {
    fn default() -> Self {
        Controls { bindings: default_bindings() }
    }
}

/// A refusal. `line` is 1-based, or 0 when the problem is not tied to one line.
#[derive(Debug, PartialEq, Eq)]
pub struct ControlsError {
    pub line: usize,
    pub message: String,
}

impl fmt::Display for ControlsError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.line == 0 {
            write!(formatter, "{}", self.message)
        } else {
            write!(formatter, "line {}: {}", self.line, self.message)
        }
    }
}

impl std::error::Error for ControlsError {}

fn error(line: usize, message: impl Into<String>) -> ControlsError {
    ControlsError { line, message: message.into() }
}

/// Curve of the trigger pressure.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Curve {
    Linear,
    Quadratic,
    Cubic,
}

impl Curve {
    pub fn as_str(self) -> &'static str {
        match self {
            Curve::Linear => "linear",
            Curve::Quadratic => "quadratic",
            Curve::Cubic => "cubic",
        }
    }
}

/// Analog trigger tuning used by the Controls page (stored as options of the trigger source).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TriggerTuning {
    pub deadzone: f32,
    pub curve: Curve,
}

/// Default trigger deadzone: 30 of 255 raw units.
pub const TRIGGER_DEFAULT_DEADZONE: f32 = 30.0 / 255.0;

impl Controls {
    pub fn bindings(&self) -> &BTreeMap<String, Vec<String>> {
        &self.bindings
    }

    pub fn sources(&self, input: &str) -> Option<&[String]> {
        self.bindings.get(input).map(|sources| sources.as_slice())
    }

    /// Replaces the sources of one input after checking each one. An empty list unbinds the input.
    pub fn set_sources(&mut self, input: &str, sources: Vec<String>) -> Result<(), ControlsError> {
        let kind = input_kind(input).ok_or_else(|| error(0, format!("unknown input '{input}'")))?;
        check_list(0, input, kind, &sources)?;
        self.bindings.insert(input.to_string(), sources);
        Ok(())
    }

    /// Trigger tuning of an input, read from its analog trigger source (`xinput:LT` or `xinput:RT`).
    pub fn trigger_tuning(&self, input: &str) -> TriggerTuning {
        let mut tuning = TriggerTuning { deadzone: TRIGGER_DEFAULT_DEADZONE, curve: Curve::Linear };
        let source = self.sources(input).and_then(|sources| {
            sources.iter().find(|source| source.starts_with("xinput:LT") || source.starts_with("xinput:RT"))
        });
        if let Some(source) = source {
            let (_, options) = split_options(source);
            for option in options {
                if let Some(value) = option.strip_prefix("deadzone=") {
                    if let Ok(parsed) = value.parse::<f32>() {
                        tuning.deadzone = parsed;
                    }
                }
                match option {
                    "curve=quadratic" => tuning.curve = Curve::Quadratic,
                    "curve=cubic" => tuning.curve = Curve::Cubic,
                    _ => {}
                }
            }
        }
        tuning
    }

    /// Writes the tuning into the analog trigger source of `input`. The deadzone is clamped to [0, 0.95].
    pub fn set_trigger_tuning(&mut self, input: &str, tuning: TriggerTuning) -> Result<(), ControlsError> {
        let current: Vec<String> = self.sources(input).map(|sources| sources.to_vec()).unwrap_or_default();
        let deadzone = tuning.deadzone.clamp(0.0, 0.95);
        let options = format!("deadzone={deadzone:.3} curve={}", tuning.curve.as_str());
        let updated: Vec<String> = current
            .into_iter()
            .map(|source| {
                if source.starts_with("xinput:LT") || source.starts_with("xinput:RT") {
                    let (base, _) = split_options(&source);
                    format!("{base} | {options}")
                } else {
                    source
                }
            })
            .collect();
        self.set_sources(input, updated)
    }

    /// The file text (schema 1). Every input is written, so the file is complete on its own.
    pub fn render(&self) -> String {
        let mut text = String::from("# GT4Recomp controls. Read by the game when it starts.\n");
        text.push_str(&format!("schema = {SCHEMA_VERSION}\n\n[bindings]\n"));
        for input in BUTTON_INPUTS.iter().chain(STICK_INPUTS.iter()) {
            let sources = self.bindings.get(*input).cloned().unwrap_or_default();
            let quoted: Vec<String> = sources.iter().map(|source| format!("\"{source}\"")).collect();
            text.push_str(&format!("{input} = [{}]\n", quoted.join(", ")));
        }
        text
    }

    /// Parses and checks a file. Refusals name the line, as the game does. Inputs not listed keep
    /// their defaults.
    pub fn parse(text: &str) -> Result<Controls, ControlsError> {
        let mut schema_seen = false;
        let mut in_bindings = false;
        let mut bindings: BTreeMap<String, Vec<String>> = BTreeMap::new();
        for (index, raw) in text.lines().enumerate() {
            let line_number = index + 1;
            let line = strip_comment(raw).trim();
            if line.is_empty() {
                continue;
            }
            if let Some(table) = line.strip_prefix('[') {
                let name = table
                    .strip_suffix(']')
                    .ok_or_else(|| error(line_number, "malformed table header"))?;
                if name != "bindings" {
                    return Err(error(line_number, format!("unknown table '{name}'")));
                }
                in_bindings = true;
                continue;
            }
            let (key, value) = line
                .split_once('=')
                .ok_or_else(|| error(line_number, "expected 'key = value'"))?;
            let key = key.trim();
            let value = value.trim();
            if !in_bindings {
                if key != "schema" {
                    return Err(error(line_number, format!("unknown key '{key}' outside [bindings]")));
                }
                if value != "1" {
                    return Err(error(line_number, "schema must be exactly 1"));
                }
                schema_seen = true;
                continue;
            }
            let kind = input_kind(key).ok_or_else(|| error(line_number, format!("unknown input '{key}'")))?;
            if bindings.contains_key(key) {
                return Err(error(line_number, format!("input '{key}' appears twice")));
            }
            let sources = parse_sources(line_number, value)?;
            check_list(line_number, key, kind, &sources)?;
            bindings.insert(key.to_string(), sources);
        }
        if !schema_seen {
            return Err(error(0, "schema = 1 is required"));
        }
        let mut merged = default_bindings();
        merged.extend(bindings);
        Ok(Controls { bindings: merged })
    }
}

/// Reads and checks an existing file. A missing file is not an error: the built-in bindings apply.
pub fn load(path: &Path) -> Result<Option<Controls>, String> {
    match std::fs::read_to_string(path) {
        Ok(text) => Controls::parse(&text)
            .map(Some)
            .map_err(|error| format!("{}: {error}", path.display())),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(None),
        Err(error) => Err(format!("{}: {error}", path.display())),
    }
}

/// Writes the file atomically (temporary file, then rename).
pub fn save(path: &Path, controls: &Controls) -> std::io::Result<()> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let temporary = path.with_extension("toml.tmp");
    std::fs::write(&temporary, controls.render())?;
    std::fs::rename(&temporary, path)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum InputKind {
    Button,
    Stick,
}

fn input_kind(input: &str) -> Option<InputKind> {
    if BUTTON_INPUTS.contains(&input) {
        Some(InputKind::Button)
    } else if STICK_INPUTS.contains(&input) {
        Some(InputKind::Stick)
    } else {
        None
    }
}

fn check_list(line: usize, input: &str, kind: InputKind, sources: &[String]) -> Result<(), ControlsError> {
    let mut seen: Vec<&str> = Vec::new();
    for source in sources {
        validate_source(line, kind, source)?;
        if seen.contains(&source.as_str()) {
            return Err(error(line, format!("duplicate source '{source}' on {input}")));
        }
        seen.push(source);
    }
    Ok(())
}

fn strip_comment(line: &str) -> &str {
    let mut quoted = false;
    for (index, character) in line.char_indices() {
        match character {
            '"' => quoted = !quoted,
            '#' if !quoted => return &line[..index],
            _ => {}
        }
    }
    line
}

/// `["a", "b"]` or a single `"a"` -> the quoted strings. Strings cannot contain `"`.
fn parse_sources(line: usize, value: &str) -> Result<Vec<String>, ControlsError> {
    let value = value.trim();
    if let Some(inner) = value.strip_prefix('[') {
        let inner = inner
            .strip_suffix(']')
            .ok_or_else(|| error(line, "list is not closed with ']'"))?;
        if inner.trim().is_empty() {
            return Ok(Vec::new());
        }
        inner.split(',').map(|item| parse_quoted(line, item.trim())).collect()
    } else {
        Ok(vec![parse_quoted(line, value)?])
    }
}

fn parse_quoted(line: usize, text: &str) -> Result<String, ControlsError> {
    let inner = text
        .strip_prefix('"')
        .and_then(|rest| rest.strip_suffix('"'))
        .ok_or_else(|| error(line, format!("source must be in double quotes: {text}")))?;
    if inner.contains('"') {
        return Err(error(line, "strings cannot contain '\"'"));
    }
    Ok(inner.to_string())
}

fn split_options(source: &str) -> (&str, Vec<&str>) {
    match source.split_once('|') {
        Some((base, options)) => (base.trim(), options.split_whitespace().collect()),
        None => (source.trim(), Vec::new()),
    }
}

fn validate_source(line: usize, kind: InputKind, source: &str) -> Result<(), ControlsError> {
    let (base, options) = split_options(source);
    let (family, name) = base
        .split_once(':')
        .ok_or_else(|| error(line, format!("source '{base}' needs a prefix 'xinput:' or 'key:'")))?;
    let mut analog = false;
    match family {
        "xinput" => {
            analog = XINPUT_TRIGGERS.contains(&name) || XINPUT_AXES.contains(&name);
            if !(XINPUT_BUTTONS.contains(&name) || analog) {
                return Err(error(line, format!("unknown XInput source '{name}'")));
            }
            if kind == InputKind::Stick && !XINPUT_AXES.contains(&name) {
                return Err(error(line, format!("'{name}' cannot drive a stick")));
            }
            if kind == InputKind::Button && XINPUT_AXES.contains(&name) {
                return Err(error(line, format!("stick axis '{name}' cannot drive a button")));
            }
        }
        "key" => {
            if kind == InputKind::Stick {
                return Err(error(line, "keyboard keys cannot drive a stick"));
            }
            if RESERVED_KEYS.contains(&name) {
                return Err(error(line, format!("key '{name}' is reserved by the launcher")));
            }
            if !is_key_name(name) {
                return Err(error(line, format!("unknown key '{name}'")));
            }
        }
        other => return Err(error(line, format!("unknown source family '{other}'"))),
    }
    for option in options {
        validate_option(line, kind, analog, option)?;
    }
    Ok(())
}

fn validate_option(line: usize, kind: InputKind, analog: bool, option: &str) -> Result<(), ControlsError> {
    let (key, value) = option
        .split_once('=')
        .ok_or_else(|| error(line, format!("option '{option}' needs a value")))?;
    match key {
        "deadzone" | "scale" => {
            if !analog {
                return Err(error(line, format!("{key} applies to triggers and sticks only")));
            }
            let parsed: f32 = value.parse().map_err(|_| error(line, format!("{key} must be a number")))?;
            if key == "deadzone" && !(0.0..1.0).contains(&parsed) {
                return Err(error(line, "deadzone must be at least 0 and below 1"));
            }
            if key == "scale" && !(0.0..=2.0).contains(&parsed) {
                return Err(error(line, "scale must be between 0 and 2"));
            }
        }
        "curve" => {
            if !analog {
                return Err(error(line, "curve applies to triggers and sticks only"));
            }
            if !["linear", "quadratic", "cubic"].contains(&value) {
                return Err(error(line, "curve must be linear, quadratic or cubic"));
            }
        }
        "invert" => {
            if kind != InputKind::Stick {
                return Err(error(line, "invert applies to sticks only"));
            }
            if !["true", "false"].contains(&value) {
                return Err(error(line, "invert must be true or false"));
            }
        }
        other => return Err(error(line, format!("unknown option '{other}'"))),
    }
    Ok(())
}

fn is_key_name(name: &str) -> bool {
    let lower = name.to_ascii_lowercase();
    if lower.len() == 1 && lower.as_bytes()[0].is_ascii_alphanumeric() {
        return true;
    }
    if let Some(number) = lower.strip_prefix('f') {
        if let Ok(value) = number.parse::<u32>() {
            return (1..=12).contains(&value);
        }
    }
    if let Some(number) = lower.strip_prefix("num") {
        return number.len() == 1 && number.as_bytes()[0].is_ascii_digit();
    }
    matches!(
        lower.as_str(),
        "enter" | "backspace" | "tab" | "space" | "shift" | "ctrl" | "alt" | "esc" | "up" | "down" | "left"
            | "right" | "insert" | "delete" | "home" | "end" | "pageup" | "pagedown"
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults_render_and_parse_back_to_themselves() {
        let controls = Controls::default();
        assert_eq!(Controls::parse(&controls.render()).unwrap(), controls);
    }

    #[test]
    fn the_schema_line_is_required_and_must_be_one() {
        assert!(Controls::parse("[bindings]\ncross = []\n").is_err());
        assert!(Controls::parse("schema = 2\n").is_err());
    }

    #[test]
    fn refusals_name_the_line() {
        let error = Controls::parse("schema = 1\n[bindings]\ncross = [\"xinput:Q\"]\n").unwrap_err();
        assert_eq!(error.line, 3);
        assert!(error.message.contains("unknown XInput source 'Q'"));
    }

    #[test]
    fn sticks_and_buttons_accept_only_their_sources() {
        assert!(Controls::parse("schema = 1\n[bindings]\nrstick_x = [\"key:Z\"]\n").is_err());
        assert!(Controls::parse("schema = 1\n[bindings]\ncross = [\"xinput:LX\"]\n").is_err());
        assert!(Controls::parse("schema = 1\n[bindings]\ncross = [\"key:F9\"]\n").is_err());
        assert!(Controls::parse("schema = 1\n[bindings]\nrstick_y = [\"xinput:RY | invert=true\"]\n").is_ok());
        assert!(Controls::parse("schema = 1\n[bindings]\ncross = [\"xinput:A | invert=true\"]\n").is_err());
    }

    #[test]
    fn options_are_range_checked() {
        assert!(Controls::parse("schema = 1\n[bindings]\nl2 = [\"xinput:LT | deadzone=1.5\"]\n").is_err());
        assert!(Controls::parse("schema = 1\n[bindings]\nl2 = [\"xinput:LT | curve=cubic\"]\n").is_ok());
        assert!(Controls::parse("schema = 1\n[bindings]\nl2 = [\"xinput:LT | curve=sharp\"]\n").is_err());
    }

    #[test]
    fn duplicates_and_unknown_tables_are_refused() {
        assert!(Controls::parse("schema = 1\n[bindings]\ncross = [\"key:Z\", \"key:Z\"]\n").is_err());
        assert!(Controls::parse("schema = 1\n[bindings]\ncross = []\ncross = []\n").is_err());
        assert!(Controls::parse("schema = 1\n[other]\n").is_err());
    }

    #[test]
    fn unlisted_inputs_keep_their_defaults_and_an_empty_list_unbinds() {
        let controls = Controls::parse("schema = 1\n[bindings]\ncross = []\n").unwrap();
        assert_eq!(controls.sources("cross"), Some(&[][..]));
        assert_eq!(
            controls.sources("circle").unwrap(),
            &["xinput:B".to_string(), "key:X".to_string()][..]
        );
    }

    #[test]
    fn trigger_tuning_round_trips_through_the_source_options() {
        let mut controls = Controls::default();
        controls
            .set_trigger_tuning("l2", TriggerTuning { deadzone: 0.05, curve: Curve::Quadratic })
            .unwrap();
        let tuning = controls.trigger_tuning("l2");
        assert!((tuning.deadzone - 0.05).abs() < 0.001);
        assert_eq!(tuning.curve, Curve::Quadratic);
        let text = controls.render();
        assert!(text.contains("l2 = [\"xinput:LT | deadzone=0.050 curve=quadratic\", \"key:1\"]"));
        assert!(Controls::parse(&text).is_ok());
    }

    #[test]
    fn default_trigger_tuning_matches_the_game() {
        let tuning = Controls::default().trigger_tuning("r2");
        assert!((tuning.deadzone - 30.0 / 255.0).abs() < 1e-6);
        assert_eq!(tuning.curve, Curve::Linear);
    }
}
