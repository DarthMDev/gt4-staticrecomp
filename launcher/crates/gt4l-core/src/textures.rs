//! Custom texture folder layout (TASK-093, owner 2026-10-22). gt4-gs 1.2.1 reads the `Textures` folder itself, so a
//! pack goes straight into it. Older gt4-gs versions read only `Textures\replacements`, so this module moves the
//! contents of an old `replacements` folder up into `Textures`, but only once gt4-gs 1.2.1 or later is in use (an
//! older renderer would stop loading the files otherwise).
//!
//! The rules of [`flatten_replacements`]:
//! - Runs only when the gt4-gs in use (`current.txt` under the renderer root) is at least [`FLATTEN_MINIMUM`].
//! - Runs only when `Textures\replacements` is a real folder (not a junction, symlink or other reparse point).
//! - Each direct child moves with a rename into `Textures` (same folder, same volume). A child whose name already
//!   exists in `Textures` is never overwritten or merged: it stays in `replacements` and the log says so.
//! - The `replacements` folder is removed only when it is empty, after the moves.
//! - The first error stops the moves; the rest stays in place and the next start tries again. Nothing is deleted
//!   except an empty `replacements` folder.
//! - Every target is checked with [`crate::guard::check_write`] first.

use std::fs;
use std::io;
use std::path::Path;

use crate::guard;
use crate::version::Version;

/// Name of the old texture subfolder that gt4-gs up to 1.2.0 read.
pub const REPLACEMENTS_FOLDER_NAME: &str = "replacements";

/// Lowest gt4-gs version that reads `Textures` itself, so the old folder may be flattened.
pub const FLATTEN_MINIMUM: Version = Version(1, 2, 1);

/// The file operations of the flatten, so tests can fail them.
pub struct FlattenOps<'a> {
    pub rename: &'a dyn Fn(&Path, &Path) -> io::Result<()>,
    pub remove_dir: &'a dyn Fn(&Path) -> io::Result<()>,
}

fn real_rename(from: &Path, to: &Path) -> io::Result<()> {
    fs::rename(from, to)
}

fn real_remove_dir(folder: &Path) -> io::Result<()> {
    fs::remove_dir(folder)
}

impl FlattenOps<'static> {
    /// The operations of the launcher.
    pub fn real() -> FlattenOps<'static> {
        FlattenOps { rename: &real_rename, remove_dir: &real_remove_dir }
    }
}

/// What the flatten did, as log lines for the launcher log.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum FlattenOutcome {
    /// gt4-gs below [`FLATTEN_MINIMUM`], or no readable `current.txt`: nothing moved.
    RendererTooOld(String),
    /// No `replacements` folder: nothing to do.
    Absent,
    /// `replacements` is a junction, symlink or other reparse point: left alone.
    Refused(String),
    /// The move ran. `moved` children went up, `kept` names stayed (their name was taken), `error` stopped the rest.
    Ran { moved: usize, kept: Vec<String>, removed_folder: bool, error: Option<String> },
    /// The folder could not be inspected or a guard refused a target.
    Failed(String),
}

impl FlattenOutcome {
    /// One log line per fact, for the launcher log.
    pub fn log_lines(&self) -> Vec<String> {
        match self {
            FlattenOutcome::RendererTooOld(reason) => {
                vec![format!("textures: not flattened ({reason}); replacements stays in use until gt4-gs 1.2.1")]
            }
            FlattenOutcome::Absent => Vec::new(),
            FlattenOutcome::Refused(reason) => vec![format!("textures: replacements skipped: {reason}")],
            FlattenOutcome::Failed(error) => vec![format!("textures: replacements not flattened: {error}")],
            FlattenOutcome::Ran { moved, kept, removed_folder, error } => {
                let mut lines = Vec::new();
                if *moved > 0 {
                    lines.push(format!("textures: moved {moved} item(s) from replacements into Textures"));
                }
                for name in kept {
                    lines.push(format!("textures: {name} already exists in Textures; it stays in replacements"));
                }
                if let Some(error) = error {
                    lines.push(format!("textures: flatten stopped, the rest stays in replacements; it runs again at the next start: {error}"));
                }
                if *removed_folder {
                    lines.push("textures: removed the empty replacements folder".to_string());
                }
                lines
            }
        }
    }
}

/// The gt4-gs version named by `current.txt` under the renderer root, if it parses.
pub fn renderer_version_in_use(renderer_root: &Path) -> Option<Version> {
    let text = fs::read_to_string(renderer_root.join("current.txt")).ok()?;
    Version::parse(text.trim())
}

/// Whether a path is a reparse point (junction, symlink, mount point or other): never followed by the flatten.
fn is_reparse_point(metadata: &fs::Metadata) -> bool {
    if metadata.file_type().is_symlink() {
        return true;
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        const FILE_ATTRIBUTE_REPARSE_POINT: u32 = 0x400;
        if metadata.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT != 0 {
            return true;
        }
    }
    false
}

/// Moves the direct children of `Textures\replacements` up into `Textures` and removes the folder when it is empty.
/// `data_root` is the data folder the write guard checks against. Never panics; every problem is in the outcome.
pub fn flatten_replacements(textures: &Path, renderer_root: &Path, data_root: &Path, ops: &FlattenOps<'_>) -> FlattenOutcome {
    match renderer_version_in_use(renderer_root) {
        Some(version) if version >= FLATTEN_MINIMUM => {}
        Some(version) => return FlattenOutcome::RendererTooOld(format!("gt4-gs {} is in use", version.text())),
        None => return FlattenOutcome::RendererTooOld("no gt4-gs version in use".to_string()),
    }
    let folder = textures.join(REPLACEMENTS_FOLDER_NAME);
    let metadata = match fs::symlink_metadata(&folder) {
        Ok(metadata) => metadata,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return FlattenOutcome::Absent,
        Err(error) => return FlattenOutcome::Failed(format!("{}: {error}", folder.display())),
    };
    if is_reparse_point(&metadata) {
        return FlattenOutcome::Refused(format!("{} is a junction or link", folder.display()));
    }
    if !metadata.is_dir() {
        return FlattenOutcome::Refused(format!("{} is not a folder", folder.display()));
    }
    let entries = match fs::read_dir(&folder) {
        Ok(entries) => entries,
        Err(error) => return FlattenOutcome::Failed(format!("{}: {error}", folder.display())),
    };
    let mut children = Vec::new();
    for entry in entries {
        match entry {
            Ok(entry) => children.push(entry.path()),
            Err(error) => return FlattenOutcome::Failed(format!("{}: {error}", folder.display())),
        }
    }
    children.sort();

    let mut moved = 0;
    let mut kept = Vec::new();
    let mut error = None;
    for source in &children {
        let Some(name) = source.file_name() else { continue };
        let destination = textures.join(name);
        if fs::symlink_metadata(&destination).is_ok() {
            kept.push(name.to_string_lossy().into_owned());
            continue;
        }
        if let Err(reason) = guard::check_write(data_root, &destination) {
            error = Some(reason);
            break;
        }
        if let Err(reason) = (ops.rename)(source, &destination) {
            error = Some(format!("{} to {}: {reason}", source.display(), destination.display()));
            break;
        }
        moved += 1;
    }

    // Removed only when empty: a kept child, a failed move or an unreadable entry leaves the folder in place.
    let mut removed_folder = false;
    if error.is_none() && kept.is_empty() {
        let still_empty = fs::read_dir(&folder).map(|mut rest| rest.next().is_none()).unwrap_or(false);
        if still_empty {
            if let Err(reason) = guard::check_write(data_root, &folder).and_then(|()| (ops.remove_dir)(&folder).map_err(|e| e.to_string())) {
                error = Some(format!("{}: {reason}", folder.display()));
            } else {
                removed_folder = true;
            }
        }
    }
    FlattenOutcome::Ran { moved, kept, removed_folder, error }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::paths::TEXTURES_FOLDER_NAME;

    fn scratch(name: &str) -> std::path::PathBuf {
        let path = std::env::temp_dir().join(format!("gt4l-core-textures-{}-{name}", std::process::id()));
        let _ = fs::remove_dir_all(&path);
        fs::create_dir_all(&path).unwrap();
        path
    }

    fn set_renderer(root: &Path, version: &str) {
        fs::create_dir_all(root).unwrap();
        fs::write(root.join("current.txt"), version).unwrap();
    }

    fn real() -> FlattenOps<'static> {
        FlattenOps::real()
    }

    #[test]
    fn children_move_up_and_the_empty_folder_is_removed() {
        let base = scratch("moves");
        let data = base.join("data");
        let textures = data.join(TEXTURES_FOLDER_NAME);
        let folder = textures.join(REPLACEMENTS_FOLDER_NAME);
        fs::create_dir_all(folder.join("SCUS-97328")).unwrap();
        fs::write(folder.join("pack.txt"), b"pack").unwrap();
        fs::write(folder.join("SCUS-97328").join("a.png"), b"tex").unwrap();
        set_renderer(&data.join("gs"), "1.2.1");

        let outcome = flatten_replacements(&textures, &data.join("gs"), &data, &real());
        assert_eq!(outcome, FlattenOutcome::Ran { moved: 2, kept: vec![], removed_folder: true, error: None });
        assert_eq!(fs::read(textures.join("pack.txt")).unwrap(), b"pack");
        assert_eq!(fs::read(textures.join("SCUS-97328").join("a.png")).unwrap(), b"tex");
        assert!(!folder.exists());
    }

    #[test]
    fn a_name_collision_keeps_that_child_and_the_folder() {
        let base = scratch("collision");
        let data = base.join("data");
        let textures = data.join(TEXTURES_FOLDER_NAME);
        let folder = textures.join(REPLACEMENTS_FOLDER_NAME);
        fs::create_dir_all(&folder).unwrap();
        fs::create_dir_all(&textures).unwrap();
        fs::write(folder.join("same.png"), b"old").unwrap();
        fs::write(textures.join("same.png"), b"new").unwrap();
        fs::write(folder.join("other.png"), b"other").unwrap();
        set_renderer(&data.join("gs"), "1.2.1");

        let outcome = flatten_replacements(&textures, &data.join("gs"), &data, &real());
        assert_eq!(
            outcome,
            FlattenOutcome::Ran { moved: 1, kept: vec!["same.png".to_string()], removed_folder: false, error: None }
        );
        assert_eq!(fs::read(textures.join("same.png")).unwrap(), b"new", "the Textures file is not overwritten");
        assert_eq!(fs::read(folder.join("same.png")).unwrap(), b"old", "the colliding child stays");
        assert!(folder.is_dir());
    }

    #[test]
    fn gt4_gs_1_2_0_or_no_current_file_moves_nothing() {
        let base = scratch("old-renderer");
        let data = base.join("data");
        let textures = data.join(TEXTURES_FOLDER_NAME);
        let folder = textures.join(REPLACEMENTS_FOLDER_NAME);
        fs::create_dir_all(&folder).unwrap();
        fs::write(folder.join("pack.txt"), b"pack").unwrap();

        set_renderer(&data.join("gs"), "1.2.0");
        let outcome = flatten_replacements(&textures, &data.join("gs"), &data, &real());
        assert!(matches!(outcome, FlattenOutcome::RendererTooOld(_)), "{outcome:?}");

        let missing = data.join("no-gs");
        let outcome = flatten_replacements(&textures, &missing, &data, &real());
        assert!(matches!(outcome, FlattenOutcome::RendererTooOld(_)), "{outcome:?}");
        assert_eq!(fs::read(folder.join("pack.txt")).unwrap(), b"pack");
        assert!(!textures.join("pack.txt").exists());
    }

    #[test]
    fn a_missing_replacements_folder_is_a_no_op() {
        let base = scratch("missing");
        let data = base.join("data");
        let textures = data.join(TEXTURES_FOLDER_NAME);
        fs::create_dir_all(&textures).unwrap();
        set_renderer(&data.join("gs"), "1.2.1");
        assert_eq!(flatten_replacements(&textures, &data.join("gs"), &data, &real()), FlattenOutcome::Absent);
        assert!(FlattenOutcome::Absent.log_lines().is_empty());
    }

    #[test]
    fn a_failing_rename_deletes_nothing_and_reports_one_error() {
        let base = scratch("failing");
        let data = base.join("data");
        let textures = data.join(TEXTURES_FOLDER_NAME);
        let folder = textures.join(REPLACEMENTS_FOLDER_NAME);
        fs::create_dir_all(&folder).unwrap();
        fs::write(folder.join("a.png"), b"a").unwrap();
        fs::write(folder.join("b.png"), b"b").unwrap();
        set_renderer(&data.join("gs"), "1.2.1");

        let fail = |_: &Path, _: &Path| -> io::Result<()> { Err(io::Error::other("locked")) };
        let remove = |_: &Path| -> io::Result<()> { panic!("nothing may be removed after a failed rename") };
        let ops = FlattenOps { rename: &fail, remove_dir: &remove };
        let outcome = flatten_replacements(&textures, &data.join("gs"), &data, &ops);
        match &outcome {
            FlattenOutcome::Ran { moved: 0, kept, removed_folder: false, error: Some(_) } => assert!(kept.is_empty()),
            other => panic!("unexpected {other:?}"),
        }
        assert_eq!(outcome.log_lines().iter().filter(|line| line.contains("flatten stopped")).count(), 1);
        assert_eq!(fs::read(folder.join("a.png")).unwrap(), b"a");
        assert_eq!(fs::read(folder.join("b.png")).unwrap(), b"b");
        assert!(!textures.join("a.png").exists());
    }

    #[test]
    fn a_folder_that_is_a_file_is_refused() {
        let base = scratch("file");
        let data = base.join("data");
        let textures = data.join(TEXTURES_FOLDER_NAME);
        fs::create_dir_all(&textures).unwrap();
        fs::write(textures.join(REPLACEMENTS_FOLDER_NAME), b"not a folder").unwrap();
        set_renderer(&data.join("gs"), "1.2.1");
        assert!(matches!(flatten_replacements(&textures, &data.join("gs"), &data, &real()), FlattenOutcome::Refused(_)));
        assert!(textures.join(REPLACEMENTS_FOLDER_NAME).is_file());
    }

    #[test]
    fn an_empty_replacements_folder_is_removed_without_moves() {
        let base = scratch("empty");
        let data = base.join("data");
        let textures = data.join(TEXTURES_FOLDER_NAME);
        fs::create_dir_all(textures.join(REPLACEMENTS_FOLDER_NAME)).unwrap();
        set_renderer(&data.join("gs"), "1.2.1");
        let outcome = flatten_replacements(&textures, &data.join("gs"), &data, &real());
        assert_eq!(outcome, FlattenOutcome::Ran { moved: 0, kept: vec![], removed_folder: true, error: None });
        assert!(!textures.join(REPLACEMENTS_FOLDER_NAME).exists());
    }
}
