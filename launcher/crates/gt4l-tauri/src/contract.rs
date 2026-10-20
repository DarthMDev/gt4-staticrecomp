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
    "open_game_files_folder",
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

    /// Command names the page's scripts call: `call("name"` and the library page's `list: "name"` table.
    fn called_by_the_page() -> Vec<String> {
        let mut names = Vec::new();
        let mut stack = vec![std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("ui").join("js")];
        while let Some(folder) = stack.pop() {
            for entry in std::fs::read_dir(&folder).expect("ui/js") {
                let path = entry.unwrap().path();
                if path.is_dir() {
                    stack.push(path);
                    continue;
                }
                if path.extension().and_then(|value| value.to_str()) != Some("js") {
                    continue;
                }
                let text = std::fs::read_to_string(&path).unwrap();
                for marker in ["call(\"", "list: \"", "importCmd: \"", "activate: \"", "remove: \""] {
                    for piece in text.split(marker).skip(1) {
                        let name: String = piece.chars().take_while(|c| c.is_ascii_lowercase() || *c == '_').collect();
                        if piece[name.len()..].starts_with('"') && !name.is_empty() && !names.contains(&name) {
                            names.push(name);
                        }
                    }
                }
            }
        }
        names.sort();
        names
    }

    #[test]
    fn every_command_the_page_calls_is_registered_and_every_registered_command_is_called() {
        let registered = registered();
        let called = called_by_the_page();
        let missing: Vec<&String> = called.iter().filter(|name| !registered.contains(name)).collect();
        assert!(missing.is_empty(), "the page calls unregistered commands: {missing:?}");
        let unused: Vec<&String> = registered.iter().filter(|name| !called.contains(name)).collect();
        assert!(unused.is_empty(), "registered commands no page calls: {unused:?}");
    }
}
