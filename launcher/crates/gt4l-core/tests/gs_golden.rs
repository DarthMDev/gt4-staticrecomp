//! Golden test (TASK-019, decision 0070): the Rust gt4-gs.ini writer and scripts/gs_settings.py give the same bytes.
//!
//! The fixtures in tests/fixtures/gs were written by scripts/gs_settings.py (Python in text mode on Windows, so the
//! lines end with CRLF). Regenerate them only with that script; never edit them by hand.
//! Commands used (repository root, Python from private/tooling-venv):
//!   python -I scripts/gs_settings.py ini --set upscale=2 --set aspect=4:3 --set hw_download_mode=unsynchronized
//!       --set fxaa=true --set mipmap=false --set blending=high --out <file>
//!   default-session.ini: render_ini(rows, {vsync: true, mailbox: true}, {cache_folder, data_root}) with the
//!   cache and data folders C:/gs/cache and C:/gs/data.

use std::collections::BTreeMap;

use gt4l_core::gs_table;

fn map(pairs: &[(&str, &str)]) -> BTreeMap<String, String> {
    pairs.iter().map(|(key, value)| (key.to_string(), value.to_string())).collect()
}

#[test]
fn default_session_ini_matches_the_python_writer_byte_for_byte() {
    let settings = map(&[("vsync", "true"), ("mailbox", "true")]);
    let launcher = map(&[("cache_folder", "C:/gs/cache"), ("data_root", "C:/gs/data")]);
    let text = gs_table::render_ini(&settings, &launcher).expect("render");
    assert_eq!(
        gs_table::ini_file_bytes(&text),
        include_bytes!("fixtures/gs/default-session.ini").to_vec(),
    );
}

#[test]
fn user_settings_ini_matches_the_python_writer_byte_for_byte() {
    let settings = map(&[
        ("upscale", "2"),
        ("aspect", "4:3"),
        ("hw_download_mode", "unsynchronized"),
        ("fxaa", "true"),
        ("mipmap", "false"),
        ("blending", "high"),
    ]);
    let text = gs_table::render_ini(&settings, &BTreeMap::new()).expect("render");
    assert_eq!(gs_table::ini_file_bytes(&text), include_bytes!("fixtures/gs/user-settings.ini").to_vec());
}

#[test]
fn refused_values_are_the_same_refusals_as_the_python_writer() {
    // scripts/gs_settings.py refuses these with SettingsError: out of range, not settable, launcher-only.
    // upscale 9 is the INI value of 9x (accepted); 13 is out of the table (refused by both writers).
    assert!(gs_table::render_ini(&map(&[("upscale", "13")]), &BTreeMap::new()).is_err());
    assert!(gs_table::render_ini(&map(&[("filter", "nearest")]), &BTreeMap::new()).is_err());
    assert!(gs_table::render_ini(&map(&[("cache_folder", "C:/x")]), &BTreeMap::new()).is_err());
    assert!(gs_table::render_ini(&BTreeMap::new(), &map(&[("upscale", "2")])).is_err());
}

#[test]
fn bundled_table_matches_the_table_file_in_the_repository() {
    // The launcher compiles docs/launcher/gs-settings.toml in; the Python writer reads the same file.
    let on_disk = std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/../../../docs/launcher/gs-settings.toml"))
        .expect("table file");
    assert_eq!(gs_table::TABLE_TEXT, on_disk);
}
