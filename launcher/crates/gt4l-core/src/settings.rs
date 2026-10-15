//! The single table of user settings.
//!
//! Every setting appears once in [`SETTINGS`]. The config file, the validation, the Settings pages
//! and the game environment all read this table. Values are stored as strings (`"on"`/`"off"` for
//! toggles) so the TOML file stays readable. Environment variables are produced in `launch.rs`.
//!
//! `connected` is `false` for an option whose game-side variable is not in the build yet. Such an
//! option is shown greyed out and never sent, so an unknown value cannot stop the game.
//! Renderer-specific options are shown only for the selected renderer (see [`Settings::applies`]).

use std::collections::BTreeMap;
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
    Choice(&'static [Choice]),
    Toggle,
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

pub const CORE_GS_11: &str = "coregs11";
pub const PARALLEL_GS: &str = "parallel";
pub const CORE_GS_20: &str = "coregs20";

const RENDERER_CHOICES: &[Choice] = &[
    Choice { value: CORE_GS_11, label: "CoreGS 1.1", selectable: true },
    Choice { value: PARALLEL_GS, label: "paraLLEl-GS", selectable: true },
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

/// Values are RCAS stops: 0 is the sharpest, 2 the softest.
const SHARPNESS_CHOICES: &[Choice] = &[
    Choice { value: "0", label: "0 (sharpest)", selectable: true },
    Choice { value: "0.2", label: "0.2 (default)", selectable: true },
    Choice { value: "0.5", label: "0.5", selectable: true },
    Choice { value: "1", label: "1", selectable: true },
    Choice { value: "2", label: "2 (softest)", selectable: true },
];

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

const SUPER_SAMPLING_CHOICES: &[Choice] = &[
    Choice { value: "1", label: "Off", selectable: true },
    Choice { value: "2", label: "2x", selectable: true },
    Choice { value: "4", label: "4x", selectable: true },
    Choice { value: "8", label: "8x", selectable: true },
    Choice { value: "16", label: "16x", selectable: true },
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
        default: CORE_GS_11,
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
        info: "How much edge detail FSR 1.0 adds. Only used with the FSR scaling filter.",
        kind: Kind::Choice(SHARPNESS_CHOICES),
        default: "0.2",
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
        id: "super_sampling",
        page: Page::Graphics,
        group: "paraLLEl-GS",
        title: "Super-sampling",
        info: "Renders each pixel several times for smoother edges. Higher values need a faster GPU.",
        kind: Kind::Choice(SUPER_SAMPLING_CHOICES),
        default: "1",
        connected: true,
    },
    SettingDef {
        id: "high_resolution_output",
        page: Page::Graphics,
        group: "paraLLEl-GS",
        title: "High-resolution output",
        info: "Uses the extra samples to output a sharper, higher-resolution image. Available with \
               super-sampling at 4x or more.",
        kind: TOGGLE,
        default: "on",
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
pub fn validate(definition: &SettingDef, value: &str) -> Result<&'static str, SettingError> {
    let invalid = || SettingError::InvalidValue { id: definition.id.to_string(), value: value.to_string() };
    match definition.kind {
        Kind::Toggle => match value {
            "on" => Ok("on"),
            "off" => Ok("off"),
            _ => Err(invalid()),
        },
        Kind::Choice(choices) => choices
            .iter()
            .find(|choice| choice.selectable && choice.value == value)
            .map(|choice| choice.value)
            .ok_or_else(invalid),
    }
}

/// Validated values for every setting. Always holds one entry per definition.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Settings {
    values: BTreeMap<&'static str, &'static str>,
}

impl Default for Settings {
    fn default() -> Self {
        let values = SETTINGS.iter().map(|definition| (definition.id, definition.default)).collect();
        Settings { values }
    }
}

impl Settings {
    /// The value of a setting. Every definition has a value, so this never fails for a known id.
    pub fn get(&self, id: &str) -> &'static str {
        self.values.get(id).copied().unwrap_or_else(|| {
            definition(id).map(|definition| definition.default).unwrap_or("off")
        })
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

    /// Sets a value, or resets it to the default when it is invalid. Returns a warning for the log.
    pub fn set_or_default(&mut self, id: &str, value: &str) -> Option<String> {
        match self.set(id, value) {
            Ok(()) => None,
            Err(error) => {
                if let Some(definition) = definition(id) {
                    self.values.insert(definition.id, definition.default);
                }
                Some(format!("{error}; using the default"))
            }
        }
    }

    /// Whether a setting belongs to the selected renderer. Other rows are hidden, not greyed.
    pub fn applies(&self, id: &str) -> bool {
        let renderer = self.get("renderer");
        match id {
            "internal_scale" | "present_filter" | "deinterlacing" | "anti_aliasing" => renderer == CORE_GS_11,
            // Only the FSR filter has a sharpness control.
            "fsr_sharpness" => renderer == CORE_GS_11 && self.get("present_filter") == "fsr",
            "super_sampling" | "high_resolution_output" => renderer == PARALLEL_GS,
            _ => true,
        }
    }

    /// Whether a shown row can be changed now: it is connected to the game, it applies to the selected
    /// renderer, and high-resolution output needs at least 4x super-sampling.
    pub fn is_active(&self, id: &str) -> bool {
        let connected = definition(id).map(|definition| definition.connected).unwrap_or(false);
        if !connected || !self.applies(id) {
            return false;
        }
        match id {
            "high_resolution_output" => self.super_sampling_factor() >= 4,
            _ => true,
        }
    }

    pub fn super_sampling_factor(&self) -> u32 {
        self.get("super_sampling").parse().unwrap_or(1)
    }
}
