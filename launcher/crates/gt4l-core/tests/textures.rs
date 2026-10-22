//! Custom textures (TASK-088, gt4-gs 1.2.0): the Textures folder of the player's folder, the folders the launcher
//! makes on demand, and the two Graphics rows (Load custom textures on by default, Precache off by default).

use std::collections::BTreeMap;
use std::fs;
use std::path::PathBuf;

use gt4l_core::gs_table;
use gt4l_core::paths::{ensure_texture_folders, UserDirs, UserRootSource};

fn scratch(name: &str) -> PathBuf {
    let path = std::env::temp_dir().join(format!("gt4l-textures-{}-{name}", std::process::id()));
    let _ = fs::remove_dir_all(&path);
    fs::create_dir_all(&path).unwrap();
    path
}

#[test]
fn the_textures_folder_is_inside_the_player_folder_with_no_serial_folder() {
    let documents = scratch("root");
    let user = UserDirs::new(documents.join("Gran Turismo 4"), UserRootSource::Override);
    assert_eq!(user.textures(), documents.join("Gran Turismo 4").join("Textures"));
    assert!(!user.textures().exists(), "resolving creates nothing");
}

#[test]
fn the_launcher_makes_the_textures_and_replacements_folders_on_demand() {
    let textures = scratch("ensure").join("Gran Turismo 4").join("Textures");
    ensure_texture_folders(&textures).unwrap();
    assert!(textures.join("replacements").is_dir());
    // Idempotent: a second call keeps what is there.
    fs::write(textures.join("replacements").join("pack.txt"), b"kept").unwrap();
    ensure_texture_folders(&textures).unwrap();
    assert_eq!(fs::read(textures.join("replacements").join("pack.txt")).unwrap(), b"kept");
}

#[test]
fn load_custom_textures_is_on_by_default_and_always_written() {
    let row = gs_table::row("texture_load").unwrap();
    assert_eq!((row.page.as_str(), row.default.as_str(), row.written.as_str(), row.status.as_str()), ("basic", "true", "always", "ok"));
    assert!(gs_table::offered().iter().any(|offered| offered.id == "texture_load"));
    let text = gs_table::render_ini(&BTreeMap::new(), &BTreeMap::new()).unwrap();
    assert!(text.contains("LoadTextureReplacements = true\n"), "{text}");
    let off = BTreeMap::from([("texture_load".to_string(), "false".to_string())]);
    let text = gs_table::render_ini(&off, &BTreeMap::new()).unwrap();
    assert!(text.contains("LoadTextureReplacements = false\n"), "{text}");
}

#[test]
fn precache_is_in_advanced_off_by_default_and_written_only_when_on() {
    let row = gs_table::row("texture_precache").unwrap();
    assert_eq!((row.page.as_str(), row.default.as_str(), row.written.as_str()), ("advanced", "false", "when-set"));
    assert!(gs_table::offered().iter().any(|offered| offered.id == "texture_precache"));
    let text = gs_table::render_ini(&BTreeMap::new(), &BTreeMap::new()).unwrap();
    assert!(!text.contains("PrecacheTextureReplacements"), "{text}");
    let on = BTreeMap::from([("texture_precache".to_string(), "true".to_string())]);
    let text = gs_table::render_ini(&on, &BTreeMap::new()).unwrap();
    assert!(text.contains("PrecacheTextureReplacements = true\n"), "{text}");
}

#[test]
fn texture_dumping_is_not_offered() {
    for id in ["texture_dump", "texture_dump_mipmaps", "texture_dump_fmv"] {
        assert!(!gs_table::offered().iter().any(|offered| offered.id == id), "{id} is not offered");
    }
}

#[test]
fn the_textures_folder_is_written_only_when_the_launcher_supplies_it() {
    let launcher = BTreeMap::from([("textures_folder".to_string(), "C:/Users/x/Documents/Gran Turismo 4/Textures".to_string())]);
    let text = gs_table::render_ini(&BTreeMap::new(), &launcher).unwrap();
    assert!(text.contains("[Folders]\nTextures = C:/Users/x/Documents/Gran Turismo 4/Textures\n"), "{text}");
}
