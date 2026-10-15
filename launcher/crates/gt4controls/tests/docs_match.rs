// The mirrored tables in gt4controls must match docs/CONTROLS.md (the single source of the controls
// contract) and the shipped example files. This test reads the document from the repository root,
// so it runs only inside the repository checkout (as the other launcher tests do).

use gt4controls::{default_bindings, Controls, BUTTON_INPUTS, STICK_INPUTS};
use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::path::PathBuf;

fn repository_file(relative: &str) -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../..").join(relative)
}

fn read(relative: &str) -> String {
    let path = repository_file(relative);
    fs::read_to_string(&path).unwrap_or_else(|error| panic!("{}: {error}", path.display()))
}

/// Cells of the Inputs table, keyed by input name: (xinput cell, key cell).
fn inputs_table() -> BTreeMap<String, (String, String)> {
    let mut rows = BTreeMap::new();
    let mut in_inputs = false;
    for line in read("docs/CONTROLS.md").lines() {
        if line.starts_with("## ") {
            in_inputs = line.trim() == "## Inputs";
        }
        if !in_inputs || !line.starts_with("| `") {
            continue;
        }
        let cells: Vec<&str> = line.split('|').map(str::trim).collect();
        // ["", names, meaning, kind, default XInput, default key, ""]
        assert_eq!(cells.len(), 7, "Inputs row must have five cells: {line}");
        for name in cells[1].split(',').map(|part| part.trim().trim_matches('`')) {
            rows.insert(name.to_string(), (cells[4].to_string(), cells[5].to_string()));
        }
    }
    rows
}

fn tokens(cell: &str) -> BTreeSet<String> {
    cell.split(|c: char| !c.is_ascii_alphanumeric()).filter(|t| !t.is_empty()).map(str::to_string).collect()
}

#[test]
fn the_default_map_has_the_inputs_of_the_mirror_tables() {
    let names: BTreeSet<String> = default_bindings().keys().cloned().collect();
    let mirror: BTreeSet<String> =
        BUTTON_INPUTS.iter().chain(STICK_INPUTS.iter()).map(|name| name.to_string()).collect();
    assert_eq!(names, mirror);
    assert_eq!(names.len(), 20);
}

#[test]
fn the_inputs_table_lists_every_input_once() {
    let table = inputs_table();
    let names: BTreeSet<String> = default_bindings().keys().cloned().collect();
    assert_eq!(table.keys().cloned().collect::<BTreeSet<String>>(), names);
}

#[test]
fn the_default_sources_appear_in_the_inputs_table() {
    let table = inputs_table();
    for (input, sources) in default_bindings() {
        let (xinput_cell, key_cell) = &table[&input];
        let xinput_tokens = tokens(xinput_cell);
        let key_tokens = tokens(key_cell);
        for source in &sources {
            let base = source.split(" | ").next().unwrap();
            let (kind, name) = base.split_once(':').expect("source has a kind");
            let (cell, label) = match kind {
                "xinput" => (&xinput_tokens, name),
                "key" => (&key_tokens, name),
                other => panic!("unexpected source kind {other} for {input}"),
            };
            assert!(cell.contains(label), "{input}: default {source} is not in the docs table ({cell:?})");
        }
        if sources.iter().all(|source| !source.starts_with("key:")) {
            assert!(key_tokens.contains("none") || key_cell.is_empty(), "{input}: docs list a key but no default");
        }
    }
}

#[test]
fn the_shipped_example_files_parse_and_default_matches_the_built_in_map() {
    let default = Controls::parse(&read("docs/controls/default.toml")).expect("default.toml parses");
    assert_eq!(default.bindings(), &default_bindings());
    for name in ["docs/controls/keyboard-only.toml", "docs/controls/triggers-as-pedals.toml"] {
        Controls::parse(&read(name)).unwrap_or_else(|error| panic!("{name}: {error}"));
    }
}
