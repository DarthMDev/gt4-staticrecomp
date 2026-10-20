//! Build script: generates the permissions of this app's own commands (see `capabilities/`), so the
//! page can call only the commands listed in `commands.list`. Nothing else in Tauri is enabled for the page.

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

fn main() {
    println!("cargo:rerun-if-changed=commands.list");
    println!("cargo:rerun-if-changed=capabilities");
    let manifest = tauri_build::AppManifest::new().commands(command_names());
    tauri_build::try_build(tauri_build::Attributes::new().app_manifest(manifest))
        .expect("Tauri build script failed");
}
