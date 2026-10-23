//! Build script: generates the permissions of this app's own commands (see `capabilities/`), so the
//! page can call only the commands listed in `commands.list`. Nothing else in Tauri is enabled for the page.
//! It also embeds the launcher's short release notes (TASK-099): every `docs/release-notes/launcher/<version>.md` up to
//! this launcher's version, newest first, into `$OUT_DIR/release_notes.rs`. This version's own short notes must exist,
//! or the build stops. (The full, technical notes beside them in `docs/release-notes/` are for GitHub.)

use std::path::{Path, PathBuf};

/// Reads `commands.list`: one command name per line, `#` starts a comment line.
fn command_names() -> &'static [&'static str] {
    let list: &'static str = include_str!("commands.list");
    let names: Vec<&'static str> = list
        .lines()
        .map(str::trim)
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .collect();
    // The build script runs once per build; the names live until the process exits.
    Vec::leak(names)
}

/// `1.2.3` -> (1, 2, 3); anything else is not a release notes file.
fn parse_version(text: &str) -> Option<(u32, u32, u32)> {
    let mut parts = text.split('.');
    let version = (parts.next()?.parse().ok()?, parts.next()?.parse().ok()?, parts.next()?.parse().ok()?);
    if parts.next().is_some() {
        return None;
    }
    Some(version)
}

fn embed_release_notes() {
    let manifest = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR"));
    let folder = manifest.join("../../../docs/release-notes/launcher");
    println!("cargo:rerun-if-changed={}", folder.display());
    let current_text = std::env::var("CARGO_PKG_VERSION").expect("CARGO_PKG_VERSION");
    let current = parse_version(&current_text).expect("the launcher version is x.y.z");
    let mut notes: Vec<((u32, u32, u32), String, PathBuf)> = Vec::new();
    for entry in std::fs::read_dir(&folder).expect("docs/release-notes is readable") {
        let path = entry.expect("a release notes entry").path();
        let Some(stem) = path.file_stem().and_then(|stem| stem.to_str()) else { continue };
        if path.extension().and_then(|ext| ext.to_str()) != Some("md") {
            continue;
        }
        if let Some(version) = parse_version(stem) {
            if version <= current {
                println!("cargo:rerun-if-changed={}", path.display());
                notes.push((version, stem.to_string(), path.canonicalize().expect("a release notes path")));
            }
        }
    }
    notes.sort_by(|a, b| b.0.cmp(&a.0));
    assert!(
        notes.first().map(|note| note.0) == Some(current),
        "docs/release-notes/launcher/{current_text}.md is missing: every launcher version ships its own short release notes"
    );
    let mut source = String::from("/// Release notes embedded by build.rs, newest first: (version, Markdown).\n");
    source.push_str("pub const RELEASE_NOTES: &[(&str, &str)] = &[\n");
    for (_, version, path) in &notes {
        source.push_str(&format!("    ({version:?}, include_str!({:?})),\n", path.display().to_string()));
    }
    source.push_str("];\n");
    let out = PathBuf::from(std::env::var("OUT_DIR").expect("OUT_DIR"));
    std::fs::write(Path::new(&out).join("release_notes.rs"), source).expect("write release_notes.rs");
}

fn main() {
    embed_release_notes();
    println!("cargo:rerun-if-changed=commands.list");
    println!("cargo:rerun-if-changed=capabilities");
    let manifest = tauri_build::AppManifest::new().commands(command_names());
    tauri_build::try_build(tauri_build::Attributes::new().app_manifest(manifest))
        .expect("Tauri build script failed");
}
