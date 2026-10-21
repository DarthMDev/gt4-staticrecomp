//! Schema migrations of the launcher-owned files: settings, controls and version.json (see
//! `docs/launcher/MIGRATIONS.md`, the rule that every change to a persisted format adds a migration and a test).
//!
//! A [`Chain`] holds the ordered steps `v0 -> v1 -> ... -> current` of one file kind. [`plan`] decides what to do
//! with a document, [`migrate`] runs the steps on it, and [`backup_name`] names the copy kept before a file is
//! rewritten. The functions are pure; the callers do the file reads and writes.
//!
//! Rules (owner, 2026-10-20):
//! - a file without a schema number is schema 0 and is migrated to the current schema;
//! - a file with a newer schema is never touched: the launcher uses defaults in memory and warns;
//! - a file that cannot be read is kept as a backup and replaced by defaults, with a notice;
//! - memory card images are never migrated or rewritten by the launcher.

use serde_json::Value;

/// One migration: it changes a document of schema `from` into schema `from + 1`. The chain sets the schema key
/// itself, so a step only changes the content.
#[derive(Clone, Copy)]
pub struct Step {
    pub from: u32,
    pub apply: fn(&mut Value) -> Result<(), String>,
}

/// The migrations of one file kind.
#[derive(Clone, Copy)]
pub struct Chain {
    /// File name, for the log and the backup name.
    pub file: &'static str,
    /// The key that holds the schema number (`schema_version` for settings, `schema` for the others).
    pub key: &'static str,
    /// The oldest schema the chain can read. Settings start at 0 (no schema number); the others at 1.
    pub first: u32,
    /// The schema this launcher writes.
    pub current: u32,
    /// Steps in order: `steps[i].from == first + i`, and `first + steps.len() == current`.
    pub steps: &'static [Step],
}

/// What to do with a document, decided before anything is changed.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Verdict {
    /// Already at the current schema.
    Current,
    /// Migrate from this schema to the current one (0 when the file has no schema number).
    Migrate { from: u32 },
    /// Written by a newer launcher: leave the file alone.
    Newer { found: u32 },
    /// The schema key holds something that is not a whole number.
    Corrupt(String),
}

/// Reads the schema number of a document: the chain's key, or 0 when it is absent.
pub fn schema_of(chain: &Chain, document: &Value) -> Result<u32, String> {
    match document.get(chain.key) {
        None => Ok(0),
        Some(value) => value
            .as_u64()
            .and_then(|number| u32::try_from(number).ok())
            .ok_or_else(|| format!("{}: {} is not a schema number", chain.file, chain.key)),
    }
}

/// Decides what happens to a document of this chain.
pub fn plan(chain: &Chain, document: &Value) -> Verdict {
    match schema_of(chain, document) {
        Err(message) => Verdict::Corrupt(message),
        Ok(found) if found > chain.current => Verdict::Newer { found },
        Ok(found) if found == chain.current => Verdict::Current,
        Ok(found) if found < chain.first => {
            Verdict::Corrupt(format!("{}: schema {found} is below the first schema {}", chain.file, chain.first))
        }
        Ok(found) => Verdict::Migrate { from: found },
    }
}

/// Checks that the steps of a chain are in order and complete. Tested for every chain the launcher has.
pub fn check_chain(chain: &Chain) -> Result<(), String> {
    if chain.first + chain.steps.len() as u32 != chain.current {
        return Err(format!("{}: {} steps from schema {} do not reach {}", chain.file, chain.steps.len(), chain.first, chain.current));
    }
    for (index, step) in chain.steps.iter().enumerate() {
        if step.from != chain.first + index as u32 {
            return Err(format!("{}: step {index} starts at schema {}", chain.file, step.from));
        }
    }
    Ok(())
}

/// Runs the steps from the document's schema to the current one. Returns the schema it started from.
/// A newer or corrupt document is an error here: [`plan`] must be checked first.
pub fn migrate(chain: &Chain, document: &mut Value) -> Result<u32, String> {
    let from = match plan(chain, document) {
        Verdict::Current => return Ok(chain.current),
        Verdict::Migrate { from } => from,
        Verdict::Newer { found } => return Err(format!("{} is schema {found}; not migrated", chain.file)),
        Verdict::Corrupt(message) => return Err(message),
    };
    for version in from..chain.current {
        let step = chain
            .steps
            .get((version - chain.first) as usize)
            .ok_or_else(|| format!("{}: no migration from schema {version}", chain.file))?;
        (step.apply)(document).map_err(|message| format!("{} migration {version} -> {}: {message}", chain.file, version + 1))?;
    }
    let object = document.as_object_mut().ok_or_else(|| format!("{}: the document is not a table", chain.file))?;
    object.insert(chain.key.to_string(), Value::from(chain.current));
    Ok(from)
}

/// The name of the copy kept before a migration: `<file>.bak-schema<N>-<unix seconds>`.
pub fn backup_name(file: &str, schema: u32, unix_seconds: u64) -> String {
    format!("{file}.bak-schema{schema}-{unix_seconds}")
}

/// Settings file (`settings.toml`), schema key `schema_version`. Schema 1 is the first versioned file.
/// Schema 0 (no `schema_version`) came from the earlier dev builds; schema 1 only adds the number.
pub const SETTINGS: Chain = Chain {
    file: "settings.toml",
    key: "schema_version",
    first: 0,
    current: 4,
    steps: &[
        Step { from: 0, apply: settings_0_to_1 },
        Step { from: 1, apply: settings_1_to_2 },
        Step { from: 2, apply: settings_2_to_3 },
        Step { from: 3, apply: settings_3_to_4 },
    ],
};

/// Controls file (`controls.toml`), schema key `schema`. Written as schema 1 since the first release.
pub const CONTROLS: Chain = Chain { file: "controls.toml", key: "schema", first: 1, current: 1, steps: &[] };

/// version.json in the install folder (or the data folder when the install folder is read-only).
pub const VERSION_FILE: Chain = Chain { file: "version.json", key: "schema", first: 1, current: 1, steps: &[] };

/// Schema 2: Fullscreen is on by default. A file of schema 1 may hold the old default ("off") that was never
/// chosen on the Settings page. Such a value becomes "on"; a value the user chose (`[choices] fullscreen = true`)
/// is kept. The backup of the old file is made by `config::prepare` before this step runs.
fn settings_1_to_2(document: &mut Value) -> Result<(), String> {
    let chosen = document.pointer("/choices/fullscreen").and_then(Value::as_bool) == Some(true);
    if chosen {
        return Ok(());
    }
    if let Some(settings) = document.get_mut("settings").and_then(Value::as_object_mut) {
        if settings.get("fullscreen").and_then(Value::as_str) == Some("off") {
            settings.insert("fullscreen".to_string(), Value::from("on"));
        }
    }
    Ok(())
}

/// Schema 3: the Widescreen 16:9 row (TASK-079, `GT4_FORCE_WIDESCREEN`). A file of schema 2 has no such key; it gets
/// the row's default "on" written out, so the file records the value the game is sent. Other values are kept.
fn settings_2_to_3(document: &mut Value) -> Result<(), String> {
    if let Some(settings) = document.get_mut("settings").and_then(Value::as_object_mut) {
        settings.entry("widescreen".to_string()).or_insert_with(|| Value::from("on"));
    }
    Ok(())
}

/// Schema 4: the Aspect ratio and Custom ratio rows (TASK-080, `GT4_DISPLAY_ASPECT`). A file of schema 3 has neither
/// key; it gets the defaults written out ("auto" and 233 hundredths), so the file records what the game is sent. Values
/// already present are kept.
fn settings_3_to_4(document: &mut Value) -> Result<(), String> {
    if let Some(settings) = document.get_mut("settings").and_then(Value::as_object_mut) {
        settings.entry("aspect_ratio".to_string()).or_insert_with(|| Value::from("auto"));
        settings.entry("aspect_custom".to_string()).or_insert_with(|| Value::from("233"));
    }
    Ok(())
}

/// Example and first real step: the earlier dev builds wrote no schema number and nothing else differs, so the
/// step has no content change. It is the template for later steps: change the document, nothing else.
fn settings_0_to_1(_document: &mut Value) -> Result<(), String> {
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    /// A test chain with two steps, to prove the chaining order and the schema key being set.
    const TEST_CHAIN: Chain = Chain {
        file: "test.toml",
        key: "schema",
        first: 0,
        current: 2,
        steps: &[
            Step { from: 0, apply: add_a },
            Step { from: 1, apply: rename_a_to_b },
        ],
    };

    fn add_a(document: &mut Value) -> Result<(), String> {
        document["a"] = json!(1);
        Ok(())
    }

    fn rename_a_to_b(document: &mut Value) -> Result<(), String> {
        let value = document.as_object_mut().unwrap().remove("a").ok_or("no a")?;
        document["b"] = value;
        Ok(())
    }

    fn failing(_document: &mut Value) -> Result<(), String> {
        Err("broken".into())
    }

    #[test]
    fn every_real_chain_has_complete_ordered_steps() {
        for chain in [&SETTINGS, &CONTROLS, &VERSION_FILE, &TEST_CHAIN] {
            check_chain(chain).unwrap_or_else(|error| panic!("{error}"));
        }
    }

    #[test]
    fn an_unversioned_document_is_schema_zero_and_migrates_through_every_step() {
        let mut document = json!({});
        assert_eq!(plan(&TEST_CHAIN, &document), Verdict::Migrate { from: 0 });
        assert_eq!(migrate(&TEST_CHAIN, &mut document).unwrap(), 0);
        assert_eq!(document, json!({"b": 1, "schema": 2}));
    }

    #[test]
    fn a_document_one_step_behind_runs_only_the_remaining_step() {
        let mut document = json!({"schema": 1, "a": 7});
        assert_eq!(migrate(&TEST_CHAIN, &mut document).unwrap(), 1);
        assert_eq!(document, json!({"schema": 2, "b": 7}));
    }

    #[test]
    fn a_current_document_is_left_alone() {
        let mut document = json!({"schema": 2, "b": 3});
        assert_eq!(plan(&TEST_CHAIN, &document), Verdict::Current);
        assert_eq!(migrate(&TEST_CHAIN, &mut document).unwrap(), 2);
        assert_eq!(document, json!({"schema": 2, "b": 3}));
    }

    #[test]
    fn a_newer_document_is_reported_and_never_changed() {
        let mut document = json!({"schema": 9, "x": true});
        assert_eq!(plan(&TEST_CHAIN, &document), Verdict::Newer { found: 9 });
        let before = document.clone();
        assert!(migrate(&TEST_CHAIN, &mut document).is_err());
        assert_eq!(document, before, "a newer file is not touched");
    }

    #[test]
    fn a_schema_that_is_not_a_number_is_corrupt() {
        let document = json!({"schema": "two"});
        assert!(matches!(plan(&TEST_CHAIN, &document), Verdict::Corrupt(_)));
        assert!(matches!(plan(&TEST_CHAIN, &json!({"schema": -1})), Verdict::Corrupt(_)));
    }

    #[test]
    fn a_failing_step_stops_the_chain_and_names_the_step() {
        const BROKEN: Chain = Chain {
            file: "broken.toml",
            key: "schema",
            first: 0,
            current: 1,
            steps: &[Step { from: 0, apply: failing }],
        };
        let mut document = json!({});
        let error = migrate(&BROKEN, &mut document).unwrap_err();
        assert!(error.contains("0 -> 1") && error.contains("broken"), "{error}");
    }

    #[test]
    fn a_chain_with_a_gap_is_refused_by_the_check() {
        const GAP: Chain = Chain {
            file: "gap.toml",
            key: "schema",
            first: 0,
            current: 2,
            steps: &[Step { from: 0, apply: add_a }, Step { from: 0, apply: add_a }],
        };
        assert!(check_chain(&GAP).is_err());
    }

    #[test]
    fn a_document_below_the_first_schema_of_its_chain_is_corrupt() {
        assert!(matches!(plan(&VERSION_FILE, &json!({"gt4recomp": "1.0.0"})), Verdict::Corrupt(_)));
    }

    #[test]
    fn backup_names_carry_the_schema_and_the_time() {
        assert_eq!(backup_name("settings.toml", 0, 1760000000), "settings.toml.bak-schema0-1760000000");
    }

    #[test]
    fn settings_schema_zero_reaches_the_current_schema_and_keeps_its_values() {
        let mut document = json!({"settings": {"vsync": "fast"}});
        assert_eq!(migrate(&SETTINGS, &mut document).unwrap(), 0);
        assert_eq!(document["schema_version"], json!(4));
        assert_eq!(document["settings"]["vsync"], json!("fast"));
        assert_eq!(document["settings"]["widescreen"], json!("on"));
        assert_eq!(document["settings"]["aspect_ratio"], json!("auto"));
        assert_eq!(document["settings"]["aspect_custom"], json!("233"));
    }

    #[test]
    fn schema_three_gets_aspect_auto_and_keeps_values_already_written() {
        let mut missing = json!({"schema_version": 3, "settings": {"vsync": "on", "widescreen": "off"}});
        assert_eq!(migrate(&SETTINGS, &mut missing).unwrap(), 3);
        assert_eq!(missing["schema_version"], json!(4));
        assert_eq!(missing["settings"]["aspect_ratio"], json!("auto"));
        assert_eq!(missing["settings"]["aspect_custom"], json!("233"));
        assert_eq!(missing["settings"]["widescreen"], json!("off"), "other values survive");

        let mut custom = json!({"schema_version": 3, "settings": {"aspect_ratio": "custom", "aspect_custom": "356"}});
        migrate(&SETTINGS, &mut custom).unwrap();
        assert_eq!(custom["settings"]["aspect_ratio"], json!("custom"));
        assert_eq!(custom["settings"]["aspect_custom"], json!("356"));
    }

    #[test]
    fn schema_two_gets_widescreen_on_and_keeps_a_value_already_written() {
        let mut missing = json!({"schema_version": 2, "settings": {"vsync": "on"}});
        assert_eq!(migrate(&SETTINGS, &mut missing).unwrap(), 2);
        assert_eq!(missing["schema_version"], json!(4));
        assert_eq!(missing["settings"]["widescreen"], json!("on"));

        let mut off = json!({"schema_version": 2, "settings": {"widescreen": "off"}});
        migrate(&SETTINGS, &mut off).unwrap();
        assert_eq!(off["settings"]["widescreen"], json!("off"));
    }

    #[test]
    fn schema_one_fullscreen_off_becomes_on_unless_chosen() {
        let mut unchosen = json!({"schema_version": 1, "settings": {"fullscreen": "off"}});
        assert_eq!(migrate(&SETTINGS, &mut unchosen).unwrap(), 1);
        assert_eq!(unchosen["settings"]["fullscreen"], json!("on"));

        let mut chosen = json!({"schema_version": 1, "settings": {"fullscreen": "off"}, "choices": {"fullscreen": true}});
        migrate(&SETTINGS, &mut chosen).unwrap();
        assert_eq!(chosen["settings"]["fullscreen"], json!("off"));
    }
}
