//! The command names the UI calls (ui/dev/README.md on branch task/019-launcher-ui). The test below reads
//! `generate_handler!` in main.rs and fails when a name is missing, extra or misspelled.

#[cfg(test)]
pub const UI_COMMANDS: &[&str] = &[
    "app_state",
    "setup_start",
    "setup_choose_disc",
    "setup_cancel",
    "play",
    "stop_game",
    "get_settings_table",
    "set_setting",
    "reset_gs_settings_to_defaults",
    "get_controls",
    "capture_binding",
    "cancel_capture",
    "reset_binding",
    "saves_list",
    "saves_import",
    "saves_activate",
    "saves_delete",
    "replays_list",
    "replays_import",
    "replays_delete",
    "about_info",
    "open_log_folder",
    "check_updates",
    "apply_update",
    "skip_update",
    "confirm_close",
];

#[cfg(test)]
mod tests {
    use super::UI_COMMANDS;

    /// The names listed in the `generate_handler!` call of main.rs, without module prefixes.
    fn registered() -> Vec<String> {
        let main = include_str!("main.rs");
        let start = main.find("generate_handler![").expect("handler list") + "generate_handler![".len();
        let end = start + main[start..].find(']').expect("end of handler list");
        main[start..end]
            .split(',')
            .map(str::trim)
            .filter(|name| !name.is_empty())
            .map(|name| name.rsplit("::").next().unwrap().to_string())
            .collect()
    }

    #[test]
    fn registered_commands_match_the_ui_contract() {
        let mut registered = registered();
        let mut expected: Vec<String> = UI_COMMANDS.iter().map(|name| name.to_string()).collect();
        registered.sort();
        expected.sort();
        assert_eq!(registered, expected);
    }
}
