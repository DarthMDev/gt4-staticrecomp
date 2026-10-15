//! Turns the settings table into the rows of a Settings page. Pure Rust, so the rules (what is
//! enabled, which label a value shows) are tested without a window.

use gt4l_core::settings::{self, Choice, Kind, Page, Settings, SETTINGS};

/// One row as the UI shows it. The Slint side converts this into its own struct.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RowData {
    pub id: &'static str,
    /// Group caption shown above the first row of a group, empty otherwise.
    pub group_header: String,
    pub title: &'static str,
    pub info: &'static str,
    pub is_choice: bool,
    /// Labels of every choice, including greyed-out ones (`coming soon`).
    pub choices: Vec<String>,
    pub choice_index: i32,
    pub toggle_on: bool,
    /// `false` when the option cannot take effect now (locked, not connected, or wrong renderer).
    pub enabled: bool,
}

/// Maps the Settings navigation index to a settings page. Saves, Replays, Controls and About have
/// their own content and no rows here.
pub fn page_for_index(index: i32) -> Option<Page> {
    match index {
        0 => Some(Page::Graphics),
        1 => Some(Page::Display),
        5 => Some(Page::Advanced),
        _ => None,
    }
}

/// Rows of one page, in table order.
pub fn rows_for_page(settings: &Settings, page: Page) -> Vec<RowData> {
    let mut rows = Vec::new();
    let mut previous_group = "";
    for definition in SETTINGS
        .iter()
        .filter(|definition| definition.page == page && settings.applies(definition.id))
    {
        let group_header = if definition.group != previous_group {
            definition.group.to_string()
        } else {
            String::new()
        };
        previous_group = definition.group;
        let (is_choice, choices, choice_index) = match definition.kind {
            Kind::Choice(choices) => {
                let current = settings.get(definition.id);
                let index = choices.iter().position(|choice| choice.value == current).unwrap_or(0);
                (true, labels(choices), index as i32)
            }
            Kind::Toggle => (false, Vec::new(), 0),
        };
        rows.push(RowData {
            id: definition.id,
            group_header,
            title: definition.title,
            info: definition.info,
            is_choice,
            choices,
            choice_index,
            toggle_on: settings.is_on(definition.id),
            enabled: definition.connected && settings.is_active(definition.id),
        });
    }
    rows
}

fn labels(choices: &[Choice]) -> Vec<String> {
    choices.iter().map(|choice| choice.label.to_string()).collect()
}

/// Converts the label the ComboBox reports back to the stored value. Greyed-out choices are refused.
pub fn value_for_label(id: &str, label: &str) -> Option<&'static str> {
    let definition = settings::definition(id)?;
    match definition.kind {
        Kind::Choice(choices) => choices
            .iter()
            .find(|choice| choice.label == label && choice.selectable)
            .map(|choice| choice.value),
        Kind::Toggle => None,
    }
}

/// The text of the About page. The trademark notice is the same text as NOTICE.md and the installer.
pub fn about_text(version: &str) -> String {
    format!(
        "GT4Recomp {version}\n\n\
         GT4Recomp is an unofficial fan project. It is not affiliated with, endorsed by or sponsored by \
         Sony Interactive Entertainment Inc. or Polyphony Digital Inc.\n\n\
         \"Gran Turismo\", \"GT\", the Gran Turismo 4 logo, the GT emblem and the Gran Turismo 4 cover \
         artwork are trademarks or copyrighted works of Sony Interactive Entertainment Inc. and/or Polyphony \
         Digital Inc. They are used here only to identify the game. No game data is included in the source \
         repository. You need your own copy of Gran Turismo 4 (USA, v2.00) to play.\n\n\
         Open-source licenses\n\
         GT4Recomp is MIT licensed. The launcher uses the Slint toolkit (Royalty-free License 2.0 or GPLv3; \
         \"Made with Slint\"), Rust crates listed in launcher/Cargo.lock, and paraLLEl-GS (LGPL-3.0-or-later) \
         in its own library once it ships. Full texts are installed with the program.\n\n\
         If you are a rights holder and want something removed, please open an issue at \
         github.com/moesuito/gt4-staticrecomp."
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn graphics_page_groups_rows_and_starts_with_a_group_caption() {
        let rows = rows_for_page(&Settings::default(), Page::Graphics);
        assert_eq!(rows[0].group_header, "Renderer");
        assert!(rows[1].group_header == "Image");
        assert!(rows.iter().any(|row| row.group_header == "CoreGS 1.1"));
    }

    #[test]
    fn coming_soon_renderer_is_listed_but_cannot_be_chosen() {
        let rows = rows_for_page(&Settings::default(), Page::Graphics);
        let renderer = rows.iter().find(|row| row.id == "renderer").unwrap();
        assert_eq!(renderer.choice_index, 0, "CoreGS 1.1 is preselected");
        assert_eq!(renderer.choices.len(), 3);
        assert_eq!(value_for_label("renderer", "CoreGS 2.0 (coming soon)"), None);
        assert_eq!(value_for_label("renderer", "paraLLEl-GS"), Some("parallel"));
    }

    #[test]
    fn each_renderer_shows_only_its_own_options() {
        let rows = rows_for_page(&Settings::default(), Page::Graphics);
        let ids: Vec<&str> = rows.iter().map(|row| row.id).collect();
        assert!(ids.contains(&"internal_scale") && !ids.contains(&"super_sampling"));

        let mut settings = Settings::default();
        settings.set("renderer", "parallel").unwrap();
        let ids: Vec<&str> = rows_for_page(&settings, Page::Graphics).iter().map(|row| row.id).collect();
        assert!(ids.contains(&"super_sampling") && ids.contains(&"high_resolution_output"));
        assert!(!ids.contains(&"internal_scale"));

        // CoreGS 2.0 is not selectable, so the renderer stays on paraLLEl-GS here.
        assert!(settings.set("renderer", "coregs20").is_err());
    }

    #[test]
    fn high_resolution_output_is_locked_below_four_times() {
        let mut settings = Settings::default();
        settings.set("renderer", "parallel").unwrap();
        let rows = rows_for_page(&settings, Page::Graphics);
        assert!(!rows.iter().find(|row| row.id == "high_resolution_output").unwrap().enabled);
        settings.set("super_sampling", "4").unwrap();
        let rows = rows_for_page(&settings, Page::Graphics);
        assert!(rows.iter().find(|row| row.id == "high_resolution_output").unwrap().enabled);
    }

    #[test]
    fn rows_not_yet_connected_are_greyed_out() {
        let rows = rows_for_page(&Settings::default(), Page::Display);
        let overscan = rows.iter().find(|row| row.id == "show_overscan").unwrap();
        assert!(!overscan.enabled);
        let fullscreen = rows.iter().find(|row| row.id == "fullscreen").unwrap();
        assert!(fullscreen.enabled && fullscreen.toggle_on);
    }

    #[test]
    fn about_text_carries_the_trademark_notice() {
        let text = about_text("1.0.0");
        assert!(text.starts_with("GT4Recomp 1.0.0"));
        assert!(text.contains("not affiliated with, endorsed by or sponsored by"));
    }
}
