//! The single table of user settings.
//!
//! Every setting appears once in [`SETTINGS`]. The config file, the validation, the Settings pages
//! and the game environment all read this table. Values are stored as strings (`"on"`/`"off"` for
//! toggles) so the TOML file stays readable. Environment variables are produced in `launch.rs`.
//!
//! `connected` is `false` for an option whose game-side variable is not in the build yet. Such an
//! option is shown greyed out and never sent, so an unknown value cannot stop the game.
//! Renderer-specific options are shown only for the selected renderer (see [`Settings::applies`]).

use std::collections::{BTreeMap, BTreeSet};
use std::fmt;

/// Settings pages in the order of the Settings navigation (Saves, Replays, Controls and About
/// have their own pages).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Page {
    Graphics,
    Display,
    Advanced,
}

/// One allowed value of a choice setting.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Choice {
    pub value: &'static str,
    pub label: &'static str,
    /// `false` keeps the entry visible but greyed out ("coming soon"); it cannot be selected.
    pub selectable: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    /// One of a fixed list of values, shown as a dropdown or a segmented control.
    Choice(&'static [Choice]),
    /// `on` or `off`.
    Toggle,
    /// A whole percentage from 0 to 100, shown as a slider. Stored as decimal text, e.g. `50`.
    Percent,
}

/// One row of a Settings page.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SettingDef {
    /// Stable identifier used in `settings.toml`. Never rename without a schema migration.
    pub id: &'static str,
    pub page: Page,
    pub group: &'static str,
    pub title: &'static str,
    /// Text of the "i" hover card. No helper text is shown under rows.
    pub info: &'static str,
    pub kind: Kind,
    pub default: &'static str,
    /// `false` = shown but not sent to the game yet (its variable is not in this build).
    pub connected: bool,
}

/// The PCSX2 GS in its own process (downloaded, default when installed). Decision 0070.
pub const PCSX2_GS: &str = "pcsx2";
/// Built-in renderer (the donor lineage, MIT). Secondary: its upscale, FSR, FXAA and deinterlace options are hidden.
pub const CORE_GS_11: &str = "coregs11";
/// Our own hardware renderer, not built yet ("coming soon").
pub const CORE_GS_20: &str = "coregs20";

const RENDERER_CHOICES: &[Choice] = &[
    Choice { value: PCSX2_GS, label: "PCSX2 GS", selectable: true },
    Choice { value: CORE_GS_11, label: "CoreGS 1.1", selectable: true },
    Choice { value: CORE_GS_20, label: "CoreGS 2.0 (coming soon)", selectable: false },
];

const RESOLUTION_CHOICES: &[Choice] = &[
    Choice { value: "480i", label: "Native (480i)", selectable: true },
    Choice { value: "480p", label: "480p", selectable: true },
    Choice { value: "1080i", label: "1080i", selectable: true },
];

const INTERNAL_SCALE_CHOICES: &[Choice] = &[
    Choice { value: "1", label: "1x", selectable: true },
    Choice { value: "2", label: "2x", selectable: true },
    Choice { value: "3", label: "3x", selectable: true },
];

/// `auto` sends no variable: the game picks nearest at 1x and linear when scaled.
const PRESENT_FILTER_CHOICES: &[Choice] = &[
    Choice { value: "auto", label: "Automatic", selectable: true },
    Choice { value: "nearest", label: "Nearest", selectable: true },
    Choice { value: "linear", label: "Linear", selectable: true },
    Choice { value: "fsr", label: "FSR 1.0", selectable: true },
];

/// FSR sharpness is a percentage: 0 is the softest, 100 the sharpest. The game takes the same number.
pub const FSR_SHARPNESS_DEFAULT: &str = "50";

const DEINTERLACE_CHOICES: &[Choice] = &[
    Choice { value: "off", label: "Off", selectable: true },
    Choice { value: "weave", label: "Weave", selectable: true },
    Choice { value: "bob", label: "Bob", selectable: true },
    Choice { value: "blend", label: "Blend", selectable: true },
    Choice { value: "adaptive", label: "Adaptive", selectable: true },
    Choice { value: "auto", label: "Auto", selectable: true },
];

const ANTI_ALIASING_CHOICES: &[Choice] = &[
    Choice { value: "off", label: "Off", selectable: true },
    Choice { value: "fxaa", label: "FXAA", selectable: true },
];

const VSYNC_CHOICES: &[Choice] = &[
    Choice { value: "on", label: "On", selectable: true },
    Choice { value: "fast", label: "Fast", selectable: true },
    Choice { value: "off", label: "Off", selectable: true },
];

const VERIFY_CHOICES: &[Choice] = &[
    Choice { value: "off", label: "Off", selectable: true },
    Choice { value: "100", label: "Every 100th call", selectable: true },
    Choice { value: "1000", label: "Every 1000th call", selectable: true },
];

const TOGGLE: Kind = Kind::Toggle;

/// Every user setting, in display order. Ids are the TOML keys.
pub const SETTINGS: &[SettingDef] = &[
    SettingDef {
        id: "renderer",
        page: Page::Graphics,
        group: "Renderer",
        title: "Renderer",
        info: "CoreGS 1.1 is our renderer, built on the GPU. paraLLEl-GS is an alternative renderer from the \
               paraLLEl-GS project (LGPL). CoreGS 2.0 is in development.",
        kind: Kind::Choice(RENDERER_CHOICES),
        default: PCSX2_GS,
        connected: true,
    },
    SettingDef {
        id: "input_resolution",
        page: Page::Graphics,
        group: "Image",
        title: "Input resolution",
        info: "The video mode the game renders races in. 480p is recommended: it is progressive. \
               Native (480i) and 1080i are interlaced, which looks softer and flickers. Menus are not affected.",
        kind: Kind::Choice(RESOLUTION_CHOICES),
        default: "480p",
        connected: true,
    },
    SettingDef {
        id: "internal_scale",
        page: Page::Graphics,
        group: "CoreGS 1.1",
        title: "Internal resolution",
        info: "Draws the picture at a higher internal resolution before it is shown. Higher values look \
               sharper and need a faster GPU.",
        kind: Kind::Choice(INTERNAL_SCALE_CHOICES),
        default: "1",
        connected: true,
    },
    SettingDef {
        id: "present_filter",
        page: Page::Graphics,
        group: "CoreGS 1.1",
        title: "Scaling filter",
        info: "How the picture is enlarged to your screen. Nearest keeps pixels sharp; Linear smooths them. \
               FSR 1.0 is a higher-quality upscaler with a sharpness control.",
        kind: Kind::Choice(PRESENT_FILTER_CHOICES),
        default: "auto",
        connected: true,
    },
    SettingDef {
        id: "fsr_sharpness",
        page: Page::Graphics,
        group: "CoreGS 1.1",
        title: "FSR sharpness",
        info: "How much edge detail FSR 1.0 adds: 0% is softest, 100% sharpest. Only used with the FSR scaling filter.",
        kind: Kind::Percent,
        default: FSR_SHARPNESS_DEFAULT,
        connected: true,
    },
    SettingDef {
        id: "deinterlacing",
        page: Page::Graphics,
        group: "CoreGS 1.1",
        title: "Deinterlacing",
        info: "For screens the game draws interlaced, such as menus and interlaced races. Off shows raw fields; \
               Weave, Bob, Blend and Adaptive combine the two fields; Auto chooses per frame. Progressive \
               pictures are not changed.",
        kind: Kind::Choice(DEINTERLACE_CHOICES),
        default: "auto",
        connected: true,
    },
    SettingDef {
        id: "anti_aliasing",
        page: Page::Graphics,
        group: "CoreGS 1.1",
        title: "Anti-aliasing",
        info: "Smooths jagged edges after the picture is drawn. FXAA is fast and softens fine detail a little.",
        kind: Kind::Choice(ANTI_ALIASING_CHOICES),
        default: "off",
        connected: true,
    },
    SettingDef {
        id: "fullscreen",
        page: Page::Display,
        group: "Window",
        title: "Fullscreen",
        info: "Start in fullscreen. Alt+Enter or F11 switches while playing.",
        kind: TOGGLE,
        default: "on",
        connected: true,
    },
    SettingDef {
        id: "vsync",
        page: Page::Display,
        group: "Window",
        title: "VSync",
        info: "On keeps motion smooth with no tearing. Fast and Off lower latency and may tear. Modes \
               the graphics driver lacks are not offered.",
        kind: Kind::Choice(VSYNC_CHOICES),
        default: "on",
        connected: true,
    },
    SettingDef {
        id: "async_gs",
        page: Page::Advanced,
        group: "Performance",
        title: "Asynchronous GS",
        info: "Sends graphics work to the GPU without waiting. Needed for full speed; turn off only to compare.",
        kind: TOGGLE,
        default: "on",
        connected: true,
    },
    SettingDef {
        id: "performance_report",
        page: Page::Advanced,
        group: "Diagnostics",
        title: "Performance report",
        info: "Records frame timing while you play for bug reports.",
        kind: TOGGLE,
        default: "off",
        connected: true,
    },
    SettingDef {
        id: "check_vu_code",
        page: Page::Advanced,
        group: "Diagnostics",
        title: "Check compiled VU code",
        info: "Compares compiled vector-unit code with the reference while playing. Slower.",
        kind: Kind::Choice(VERIFY_CHOICES),
        default: "off",
        connected: true,
    },
];

/// Looks up a definition by id.
pub fn definition(id: &str) -> Option<&'static SettingDef> {
    SETTINGS.iter().find(|definition| definition.id == id)
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum SettingError {
    UnknownSetting(String),
    InvalidValue { id: String, value: String },
}

impl fmt::Display for SettingError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SettingError::UnknownSetting(id) => write!(formatter, "unknown setting '{id}'"),
            SettingError::InvalidValue { id, value } => {
                write!(formatter, "value '{value}' is not allowed for '{id}'")
            }
        }
    }
}

impl std::error::Error for SettingError {}

/// Checks one value against its definition. Choices must be selectable; toggles are `on`/`off`.
pub fn validate(definition: &SettingDef, value: &str) -> Result<String, SettingError> {
    let invalid = || SettingError::InvalidValue { id: definition.id.to_string(), value: value.to_string() };
    match definition.kind {
        Kind::Toggle => match value {
            "on" | "off" => Ok(value.to_string()),
            _ => Err(invalid()),
        },
        Kind::Choice(choices) => choices
            .iter()
            .find(|choice| choice.selectable && choice.value == value)
            .map(|choice| choice.value.to_string())
            .ok_or_else(invalid),
        Kind::Percent => match value.parse::<u8>() {
            Ok(percent) if percent <= 100 => Ok(percent.to_string()),
            _ => Err(invalid()),
        },
    }
}

/// Validated values for every setting. Always holds one entry per definition.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Settings {
    values: BTreeMap<&'static str, String>,
    gs: BTreeMap<String, String>,
    /// Settings the user set on the Settings page (`[choices]` in settings.toml). A migration keeps these as they are.
    chosen: BTreeSet<&'static str>,
}

impl Default for Settings {
    fn default() -> Self {
        let values = SETTINGS.iter().map(|definition| (definition.id, definition.default.to_string())).collect();
        Settings { values, gs: BTreeMap::new(), chosen: BTreeSet::new() }
    }
}

impl Settings {
    /// The value of a setting. Every definition has a value, so this never fails for a known id.
    pub fn get(&self, id: &str) -> &str {
        match self.values.get(id) {
            Some(value) => value.as_str(),
            None => definition(id).map(|definition| definition.default).unwrap_or("off"),
        }
    }

    pub fn is_on(&self, id: &str) -> bool {
        self.get(id) == "on"
    }

    /// Sets a validated value. Unknown ids and invalid values are refused and leave the state unchanged.
    pub fn set(&mut self, id: &str, value: &str) -> Result<(), SettingError> {
        let definition = definition(id).ok_or_else(|| SettingError::UnknownSetting(id.to_string()))?;
        let value = validate(definition, value)?;
        self.values.insert(definition.id, value);
        Ok(())
    }

    /// Sets a value the user chose on the Settings page, and marks it as chosen.
    pub fn set_by_user(&mut self, id: &str, value: &str) -> Result<(), SettingError> {
        self.set(id, value)?;
        if let Some(definition) = definition(id) {
            self.chosen.insert(definition.id);
        }
        Ok(())
    }

    /// Marks a setting as chosen by the user (used when a file is read).
    pub fn mark_chosen(&mut self, id: &str) {
        if let Some(definition) = definition(id) {
            self.chosen.insert(definition.id);
        }
    }

    /// Whether the user chose this value (see [`Settings::set_by_user`]).
    pub fn was_chosen(&self, id: &str) -> bool {
        self.chosen.contains(id)
    }

    /// The ids the user chose, in table order.
    pub fn chosen_ids(&self) -> Vec<&'static str> {
        self.chosen.iter().copied().collect()
    }

    /// Sets a value, or resets it to the default when it is invalid. Returns a warning for the log.
    pub fn set_or_default(&mut self, id: &str, value: &str) -> Option<String> {
        match self.set(id, value) {
            Ok(()) => None,
            Err(error) => {
                if let Some(definition) = definition(id) {
                    self.values.insert(definition.id, definition.default.to_string());
                }
                Some(format!("{error}; using the default"))
            }
        }
    }

    /// Whether a setting is shown. Rows of the built-in renderer's upscale, FSR, anti-aliasing and deinterlace
    /// options are hidden (decision 0070): they keep their defaults and are still sent to CoreGS 1.1.
    /// The PCSX2 GS options come from the table in `gs_table`, not from this list.
    pub fn applies(&self, id: &str) -> bool {
        !matches!(id, "internal_scale" | "present_filter" | "fsr_sharpness" | "deinterlacing" | "anti_aliasing")
    }

    /// Whether a shown row can be changed now: it is connected to the game and it applies to the
    /// selected renderer.
    pub fn is_active(&self, id: &str) -> bool {
        let connected = definition(id).map(|definition| definition.connected).unwrap_or(false);
        if !connected || !self.applies(id) {
            return false;
        }
        match id {
            _ => true,
        }
    }

    /// A PCSX2 GS table value (`gs_table`), or its table default when the user has not set it.
    pub fn gs_value(&self, id: &str) -> String {
        match self.gs.get(id) {
            Some(value) => value.clone(),
            None => crate::gs_table::default_value(id).unwrap_or_default(),
        }
    }

    /// Sets a PCSX2 GS table value after the table's own check (range, choice, status).
    pub fn set_gs(&mut self, id: &str, value: &str) -> Result<(), String> {
        let normalized = crate::gs_table::check_user_value(id, value)?;
        self.gs.insert(id.to_string(), normalized);
        Ok(())
    }

    /// Drops every PCSX2 GS value the user set, so each table row is back at its default.
    pub fn clear_gs(&mut self) {
        self.gs.clear();
    }

    /// The PCSX2 GS values the user has set (only these are stored in `settings.toml`).
    pub fn gs_overrides(&self) -> &BTreeMap<String, String> {
        &self.gs
    }
}
