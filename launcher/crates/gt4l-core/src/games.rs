//! The games the launcher installs and runs (TASK-101, release 1.3.0, owner 2026-10-23;
//! `docs/MULTI-GAME-LAUNCHER-PLAN.md`).
//!
//! Two games side by side: Gran Turismo 4 v2.00 (retail, SCUS-97328) and Gran Turismo 4 Spec II 1.10 (SCUS-97436
//! lineage). The ids (`gt4`, `spec2`) are used in folder names, settings and logs; the display names are only for the
//! player. Each game has its own executable (their generated code shares symbol names, decision 0072), disc copy,
//! prepared files, runtime card, save library and replays. Settings, controls, GSdx and the textures are shared.

/// One game of the table.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Game {
    /// `gt4` or `spec2`: folder name, settings value, log text.
    pub id: &'static str,
    /// "Gran Turismo 4" / "Gran Turismo 4 Spec II".
    pub display_name: &'static str,
    /// The serial of the disc (`SYSTEM.CNF`).
    pub serial: &'static str,
    /// The game program inside `<program folder>\game`.
    pub executable: &'static str,
    /// The prepared ELF, in `games\<id>\prepared`. Its file name is the argv0 rule of the game (the donor derives the
    /// boot name from it).
    pub elf_name: &'static str,
    /// The home-screen logo asset (a file under `ui/assets`).
    pub logo_asset: &'static str,
}

/// Gran Turismo 4 v2.00 (USA, SCUS-97328).
pub const GT4: Game = Game {
    id: "gt4",
    display_name: "Gran Turismo 4",
    serial: "SCUS-97328",
    executable: "gt4vulkan.exe",
    elf_name: "SCUS_973.28",
    logo_asset: "assets/logo-gt4.png",
};

/// Gran Turismo 4 Spec II 1.10 (SCUS-97436 lineage). The authors' logo file is not in the launcher yet: the page shows
/// the GT4 logo with a "Spec II" line under it (placeholder, to be replaced with the authors' logo and credit).
pub const SPEC2: Game = Game {
    id: "spec2",
    display_name: "Gran Turismo 4 Spec II",
    serial: "SCUS-97436",
    executable: "gt4vulkan-spec2.exe",
    elf_name: "SCUS_974.36",
    logo_asset: "assets/logo-gt4.png",
};

/// Every game, in display order.
pub const GAMES: &[&Game] = &[&GT4, &SPEC2];

/// The game of an id (`gt4`, `spec2`).
pub fn by_id(id: &str) -> Option<&'static Game> {
    GAMES.iter().copied().find(|game| game.id == id)
}

/// The game of an id, or Gran Turismo 4 for an unknown one (a settings file from a newer launcher).
pub fn by_id_or_default(id: &str) -> &'static Game {
    by_id(id).unwrap_or(&GT4)
}

/// The folder (under `games`) of a game's files, in the data folder and in the user data folder.
pub const GAMES_FOLDER: &str = "games";

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_table_names_two_games_with_their_own_programs() {
        assert_eq!(GAMES.len(), 2);
        assert_eq!(by_id("gt4"), Some(&GT4));
        assert_eq!(by_id("spec2"), Some(&SPEC2));
        assert_eq!(by_id("gt5"), None);
        assert_eq!(by_id_or_default("gt5"), &GT4);
        assert_eq!(GT4.executable, "gt4vulkan.exe");
        assert_eq!(SPEC2.executable, "gt4vulkan-spec2.exe");
        assert_eq!((GT4.elf_name, SPEC2.elf_name), ("SCUS_973.28", "SCUS_974.36"));
        assert_eq!((GT4.display_name, SPEC2.display_name), ("Gran Turismo 4", "Gran Turismo 4 Spec II"));
        assert_ne!(GT4.id, SPEC2.id);
        assert_ne!(GT4.executable, SPEC2.executable, "the two games cannot share one executable (decision 0072)");
    }

    #[test]
    fn the_game_executable_constant_is_the_gt4_one() {
        assert_eq!(crate::paths::GAME_EXECUTABLE, GT4.executable);
    }
}
