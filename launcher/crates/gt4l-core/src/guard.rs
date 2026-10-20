//! The data-path guard: installs and updates must never write to the user's data (see `docs/RELEASES.md` and the
//! owner rule "user data is sacred"). Every write the install and update code makes under the data folder is
//! checked here first. A refused path is an error, never a silent skip.
//!
//! The guard is lexical: `..` is resolved before the check, and names are compared case-insensitively
//! (Windows). A path outside the data folder is not user data for this guard; the program folder is replaced by
//! updates on purpose.

use std::path::{Component, Path};

/// Folders under the data folder that hold memory cards, save slots, replays or the card the game reads.
pub const PROTECTED_FOLDERS: &[&str] = &["saves", "cards", "library", "replays", "runtime"];

/// File extensions of memory card images and save files, refused anywhere under the data folder.
pub const PROTECTED_EXTENSIONS: &[&str] = &["ps2", "psu", "mcd", "card", "sav"];

/// The components of a path, with `..` resolved, names lower-cased.
fn normalized(path: &Path) -> Vec<String> {
    let mut parts: Vec<String> = Vec::new();
    for component in path.components() {
        match component {
            Component::CurDir => {}
            Component::ParentDir => {
                parts.pop();
            }
            other => parts.push(other.as_os_str().to_string_lossy().to_lowercase()),
        }
    }
    parts
}

/// `Ok` when `target` may be written by an install or update. An error names the refused path.
pub fn check_write(data_root: &Path, target: &Path) -> Result<(), String> {
    let root = normalized(data_root);
    let path = normalized(target);
    if !path.starts_with(&root) {
        return Ok(());
    }
    let inside = &path[root.len()..];
    if inside.iter().any(|part| PROTECTED_FOLDERS.contains(&part.as_str())) {
        return Err(format!("refused: {} is user data (saves, cards or replays)", target.display()));
    }
    if let Some(last) = inside.last() {
        if let Some((_, extension)) = last.rsplit_once('.') {
            if PROTECTED_EXTENSIONS.contains(&extension) {
                return Err(format!("refused: {} is a memory card or save file", target.display()));
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::check_write;
    use std::path::Path;

    const DATA: &str = "C:/Users/someone/AppData/Local/GT4Recomp";

    #[test]
    fn renderer_and_settings_paths_are_allowed() {
        assert!(check_write(Path::new(DATA), Path::new(&format!("{DATA}/gs/1.0.1"))).is_ok());
        assert!(check_write(Path::new(DATA), Path::new(&format!("{DATA}/gs/current.txt"))).is_ok());
        assert!(check_write(Path::new(DATA), Path::new(&format!("{DATA}/config/settings.toml"))).is_ok());
        assert!(check_write(Path::new(DATA), Path::new(&format!("{DATA}/prepared/CORE.GT4"))).is_ok());
    }

    #[test]
    fn card_save_and_replay_folders_are_refused_at_any_depth() {
        for path in ["saves/slot1.psu", "cards/card.ps2", "library/Active save", "replays/run.bin", "runtime/card.ps2"] {
            let target = format!("{DATA}/{path}");
            assert!(check_write(Path::new(DATA), Path::new(&target)).is_err(), "{target} must be refused");
        }
        assert!(check_write(Path::new(DATA), Path::new(&format!("{DATA}/gs/saves/x.txt"))).is_err());
    }

    #[test]
    fn memory_card_and_save_extensions_are_refused_anywhere_under_the_data_folder() {
        assert!(check_write(Path::new(DATA), Path::new(&format!("{DATA}/gs/copy.PS2"))).is_err());
        assert!(check_write(Path::new(DATA), Path::new(&format!("{DATA}/sessions/misc/x.psu"))).is_err());
        assert!(check_write(Path::new(DATA), Path::new(&format!("{DATA}/gs/1.0.1/gt4-gs.exe"))).is_ok());
    }

    #[test]
    fn parent_segments_are_resolved_before_the_check() {
        let escaping = format!("{DATA}/gs/../saves/slot1.psu");
        assert!(check_write(Path::new(DATA), Path::new(&escaping)).is_err());
        let back_inside = format!("{DATA}/saves/../gs/1.0.1");
        assert!(check_write(Path::new(DATA), Path::new(&back_inside)).is_ok());
    }

    #[test]
    fn names_are_compared_case_insensitively() {
        assert!(check_write(Path::new(DATA), Path::new(&format!("{DATA}/SAVES/x"))).is_err());
    }

    #[test]
    fn paths_outside_the_data_folder_are_not_user_data_for_this_guard() {
        assert!(check_write(Path::new(DATA), Path::new("C:/Program Files/GT4Recomp/version.json")).is_ok());
    }
}
