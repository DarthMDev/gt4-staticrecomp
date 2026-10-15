//! The save library in the user data folder (TASK-025):
//!
//! ```text
//! <root>/memcard/                 folder card: the active game save and every replay
//! <root>/memcard/.backup/         copies of a game save before the harvest replaces it
//! <root>/saves/Save NN/<game>/    inactive game saves, one slot per folder (ids only grow)
//! <root>/backups/                 card images written before they are replaced
//! ```
//!
//! Rules: an import never overwrites (it adds a slot or skips an identical item); a harvest
//! never deletes; the card built for Play holds exactly one game save and the chosen replays.

use crate::card::{self, Card, CardError, Node, GAME_SAVE_NAME};
use crate::folder::{self, FolderError};
use crate::psu;
use std::fmt;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

#[derive(Debug)]
pub enum LibraryError {
    Io(io::Error),
    Card(CardError),
    Folder(FolderError),
    /// The named item does not exist in the library.
    NotFound(String),
    /// The item is not something this library stores (for example another game's save).
    NotGtSave(String),
}

impl From<io::Error> for LibraryError {
    fn from(error: io::Error) -> Self {
        LibraryError::Io(error)
    }
}

impl From<CardError> for LibraryError {
    fn from(error: CardError) -> Self {
        LibraryError::Card(error)
    }
}

impl From<FolderError> for LibraryError {
    fn from(error: FolderError) -> Self {
        LibraryError::Folder(error)
    }
}

impl fmt::Display for LibraryError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            LibraryError::Io(error) => write!(f, "{error}"),
            LibraryError::Card(error) => write!(f, "{error}"),
            LibraryError::Folder(error) => write!(f, "{error}"),
            LibraryError::NotFound(name) => write!(f, "not in the library: {name}"),
            LibraryError::NotGtSave(name) => write!(f, "not a Gran Turismo 4 save: {name}"),
        }
    }
}

impl std::error::Error for LibraryError {}

/// What an import or harvest did, for the Saves and Replays pages.
#[derive(Debug, Default, PartialEq, Eq)]
pub struct Report {
    /// Slots created ("Save NN") or replays added, by name.
    pub added: Vec<String>,
    /// Items already present with the same contents (nothing written).
    pub unchanged: Vec<String>,
    /// Items refused, with the reason.
    pub skipped: Vec<String>,
    /// The active game save was replaced; the old copy went to memcard/.backup.
    pub replaced_active: bool,
}

/// `Save 01`, `Save 02`, ...
pub fn slot_name(number: u32) -> String {
    format!("Save {number:02}")
}

pub struct Library {
    root: PathBuf,
}

impl Library {
    pub fn open(root: &Path) -> Result<Library, LibraryError> {
        for folder in [root.join("memcard"), root.join("memcard").join(".backup"), root.join("saves"), root.join("backups")] {
            fs::create_dir_all(folder)?;
        }
        Ok(Library { root: root.to_path_buf() })
    }

    pub fn memcard_dir(&self) -> PathBuf {
        self.root.join("memcard")
    }

    fn saves_dir(&self) -> PathBuf {
        self.root.join("saves")
    }

    /// Name of the active game save in memcard/, if any.
    pub fn active_save(&self) -> Result<Option<String>, LibraryError> {
        let game = self.memcard_dir().join(GAME_SAVE_NAME);
        Ok(if game.is_dir() { Some(GAME_SAVE_NAME.to_string()) } else { None })
    }

    /// Slot folders in saves/ that hold a game save, sorted by slot number.
    pub fn slots(&self) -> Result<Vec<String>, LibraryError> {
        let mut slots: Vec<String> = fs::read_dir(self.saves_dir())?
            .filter_map(|e| e.ok())
            .filter(|e| e.path().join(GAME_SAVE_NAME).is_dir())
            .map(|e| e.file_name().to_string_lossy().into_owned())
            .collect();
        slots.sort();
        Ok(slots)
    }

    /// Replay items in memcard/ (every replay the library keeps).
    pub fn replays(&self) -> Result<Vec<String>, LibraryError> {
        let mut names: Vec<String> = fs::read_dir(self.memcard_dir())?
            .filter_map(|e| e.ok())
            .filter(|e| e.path().is_dir())
            .map(|e| e.file_name().to_string_lossy().into_owned())
            .filter(|n| card::is_replay_name(n))
            .collect();
        names.sort();
        Ok(names)
    }

    /// True when the active save or a slot already holds these exact contents.
    fn holds_identical_game_save(&self, node: &Node) -> Result<bool, LibraryError> {
        let mut candidates = vec![self.memcard_dir().join(GAME_SAVE_NAME)];
        for slot in self.slots()? {
            candidates.push(self.saves_dir().join(slot).join(GAME_SAVE_NAME));
        }
        for path in candidates.into_iter().filter(|p| p.is_dir()) {
            let existing = folder::import_item(&path)?;
            if card::same_contents(std::slice::from_ref(&existing), std::slice::from_ref(node)) {
                return Ok(true);
            }
        }
        Ok(false)
    }

    /// First unused replay name `BASCUS-97328Rnnnn` in memcard/ (letter R, four digits).
    fn next_free_replay_name(&self) -> Result<String, LibraryError> {
        for number in 1..=9999 {
            let name = format!("{}R{number:04}", card::GAME_PREFIX);
            if !self.memcard_dir().join(&name).exists() {
                return Ok(name);
            }
        }
        Err(LibraryError::Card(CardError::Full))
    }

    fn next_slot_name(&self) -> Result<String, LibraryError> {
        let mut highest = 0;
        for entry in fs::read_dir(self.saves_dir())? {
            let name = entry?.file_name().to_string_lossy().into_owned();
            if let Some(number) = name.strip_prefix("Save ").and_then(|n| n.parse::<u32>().ok()) {
                highest = highest.max(number);
            }
        }
        Ok(slot_name(highest + 1))
    }

    /// Adds `node` (a game save or replay) to the library. Returns the name it was stored under.
    fn add_item(&self, node: &Node, report: &mut Report) -> Result<(), LibraryError> {
        let name = node.name();
        if card::is_game_save_name(&name) {
            if self.holds_identical_game_save(node)? {
                report.unchanged.push(name);
                return Ok(());
            }
            // Every other game save becomes a new slot; the active save never changes on import.
            let slot = self.next_slot_name()?;
            let slot_dir = self.saves_dir().join(&slot);
            fs::create_dir_all(&slot_dir)?;
            folder::export_item(&slot_dir, node)?;
            report.added.push(slot);
        } else if card::is_replay_name(&name) {
            let existing = self.memcard_dir().join(&name);
            if existing.is_dir() && card::same_contents(std::slice::from_ref(&folder::import_item(&existing)?), std::slice::from_ref(node)) {
                report.unchanged.push(name);
            } else if existing.is_dir() {
                // Same name, other contents: keep both, under a fresh name.
                let renamed = self.next_free_replay_name()?;
                let mut copy = node.clone();
                copy.entry.set_name(&renamed)?;
                folder::export_item(&self.memcard_dir(), &copy)?;
                report.added.push(renamed);
            } else {
                folder::export_item(&self.memcard_dir(), node)?;
                report.added.push(name);
            }
        } else {
            report.skipped.push(format!("{name}: not a Gran Turismo 4 save"));
        }
        Ok(())
    }

    /// Imports every item of a card image (`.ps2`, `.mcd`, `.bin`, with or without spare bytes).
    pub fn import_card_image(&self, bytes: &[u8]) -> Result<Report, LibraryError> {
        let card = Card::from_image(bytes)?;
        let mut report = Report::default();
        for item in card.root()? {
            self.add_item(&item, &mut report)?;
        }
        Ok(report)
    }

    /// Imports a `.psu` save file.
    pub fn import_psu(&self, bytes: &[u8]) -> Result<Report, LibraryError> {
        let item = psu::read(bytes)?;
        let mut report = Report::default();
        self.add_item(&item, &mut report)?;
        Ok(report)
    }

    /// Imports a PCSX2 folder memory card: each save sub-folder is a raw directory.
    pub fn import_folder_card(&self, folder_card: &Path) -> Result<Report, LibraryError> {
        let mut report = Report::default();
        let mut dirs: Vec<PathBuf> = fs::read_dir(folder_card)?
            .filter_map(|e| e.ok())
            .map(|e| e.path())
            .filter(|p| p.is_dir())
            .collect();
        dirs.sort();
        for dir in dirs {
            let item = folder::import_raw_directory(&dir)?;
            self.add_item(&item, &mut report)?;
        }
        Ok(report)
    }

    /// `.psu` bytes of a library item (a slot name such as "Save 02", or a replay name).
    pub fn export_psu(&self, name: &str) -> Result<Vec<u8>, LibraryError> {
        let folder_path = if name.starts_with("Save ") {
            self.saves_dir().join(name).join(GAME_SAVE_NAME)
        } else {
            self.memcard_dir().join(name)
        };
        if !folder_path.is_dir() {
            return Err(LibraryError::NotFound(name.to_string()));
        }
        let item = folder::import_item(&folder_path)?;
        Ok(psu::write(&item)?)
    }

    /// Makes `slot` the active game save. The current active save (if any) gets a new slot.
    pub fn activate_slot(&self, slot: &str) -> Result<(), LibraryError> {
        let source = self.saves_dir().join(slot).join(GAME_SAVE_NAME);
        if !source.is_dir() {
            return Err(LibraryError::NotFound(slot.to_string()));
        }
        let target = self.memcard_dir().join(GAME_SAVE_NAME);
        if target.is_dir() {
            let parked = self.next_slot_name()?;
            let parked_dir = self.saves_dir().join(&parked);
            fs::create_dir_all(&parked_dir)?;
            fs::rename(&target, parked_dir.join(GAME_SAVE_NAME))?;
        }
        fs::rename(&source, &target)?;
        fs::remove_dir(self.saves_dir().join(slot))?; // now empty
        Ok(())
    }

    /// Builds the card image for the next Play: the active game save plus `replays`.
    /// The image is verified by reading it back before it is returned.
    pub fn build_card(&self, replays: &[String]) -> Result<Vec<u8>, LibraryError> {
        let mut items = Vec::new();
        if self.active_save()?.is_some() {
            items.push(folder::import_item(&self.memcard_dir().join(GAME_SAVE_NAME))?);
        }
        for name in replays {
            let path = self.memcard_dir().join(name);
            if !path.is_dir() {
                return Err(LibraryError::NotFound(name.clone()));
            }
            items.push(folder::import_item(&path)?);
        }
        let card = Card::build(&items)?;
        let bytes = card.to_image();
        let readback = Card::from_image(&bytes)?.root()?;
        if !card::same_contents(&readback, &items) {
            return Err(LibraryError::Card(CardError::Corrupt("built card does not read back identical".into())));
        }
        Ok(bytes)
    }

    /// Copies the card image written by a Play back into the library, without deleting
    /// anything: a changed active save replaces the old one (the old copy goes to
    /// memcard/.backup); new replays are added.
    pub fn harvest(&self, card_bytes: &[u8]) -> Result<Report, LibraryError> {
        let card = Card::from_image(card_bytes)?;
        let mut report = Report::default();
        for item in card.root()? {
            let name = item.name();
            if card::is_game_save_name(&name) {
                let current = self.memcard_dir().join(&name);
                if current.is_dir() {
                    let before = folder::import_item(&current)?;
                    if card::same_contents(std::slice::from_ref(&before), std::slice::from_ref(&item)) {
                        report.unchanged.push(name);
                        continue;
                    }
                    let backup = self.memcard_dir().join(".backup");
                    let stamp = crate::timestamp();
                    let copy_dir = backup.join(format!("{name}-{stamp}"));
                    fs::create_dir_all(&copy_dir)?;
                    folder::export_item(&copy_dir, &before)?;
                    report.replaced_active = true;
                }
                folder::export_item(&self.memcard_dir(), &item)?;
                report.added.push(name);
            } else if card::is_replay_name(&name) {
                self.add_item(&item, &mut report)?;
            } else {
                report.skipped.push(format!("{name}: not a Gran Turismo 4 save"));
            }
        }
        Ok(report)
    }

    /// Writes a card image to `path`: the previous file (if any) is copied to backups/ first,
    /// and the new image is written to a temporary file and renamed into place.
    pub fn write_card_image(&self, path: &Path, bytes: &[u8]) -> Result<(), LibraryError> {
        if path.is_file() {
            let backup = self.root.join("backups").join(format!("{}-{}", crate::timestamp(), file_name(path)));
            fs::copy(path, backup)?;
        }
        let temporary = path.with_extension("tmp");
        fs::write(&temporary, bytes)?;
        fs::rename(&temporary, path)?;
        Ok(())
    }
}

fn file_name(path: &Path) -> String {
    path.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_else(|| "card".into())
}
