//! Save and replay manager interface (TASK-025, W4 in docs/RELEASE-1.0.0-PLAN.md).
//!
//! This is a stub: the launcher calls it so the Saves and Replays pages and the card preparation have
//! a fixed place to plug in. The real implementation (folder model, card image build and harvest,
//! importers) lives in task/025-save-manager and replaces [`NullSaveManager`].

use std::path::Path;

/// One game save or replay as listed on the Saves or Replays page.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SaveSlot {
    pub number: u32,
    pub label: String,
    /// The save used the next time the game starts (game saves only).
    pub active: bool,
    /// Unix seconds of the last change, when known.
    pub modified_unix_seconds: Option<u64>,
}

pub trait SaveManager {
    /// Game saves, one row per slot, in slot order.
    fn saves(&self) -> Vec<SaveSlot>;
    /// Replays, all kept; `on_card` marks the ones written to the game's card for the next start.
    fn replays(&self) -> Vec<SaveSlot>;
    /// Builds the card image the game reads before a start. Called with the card path from the launcher.
    fn prepare_card(&self, card: &Path) -> Result<(), String>;
    /// Reads the card back after a session (harvest). Called after the game exits.
    fn harvest_card(&self, card: &Path) -> Result<(), String>;
}

/// Placeholder used until the save manager lands: no slots, and the card is left to the game
/// (a missing card file is a blank card, created by the game on the first save).
#[derive(Clone, Copy, Debug, Default)]
pub struct NullSaveManager;

impl SaveManager for NullSaveManager {
    fn saves(&self) -> Vec<SaveSlot> {
        Vec::new()
    }

    fn replays(&self) -> Vec<SaveSlot> {
        Vec::new()
    }

    fn prepare_card(&self, _card: &Path) -> Result<(), String> {
        Ok(())
    }

    fn harvest_card(&self, _card: &Path) -> Result<(), String> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_placeholder_lists_nothing_and_touches_no_file() {
        let manager = NullSaveManager;
        assert!(manager.saves().is_empty());
        assert!(manager.replays().is_empty());
        assert!(manager.prepare_card(Path::new("does-not-exist.ps2")).is_ok());
    }
}
