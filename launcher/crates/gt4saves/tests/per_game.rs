// One save library per game (TASK-101): the two games write the same PS2 folder name, so nothing may cross.

use gt4saves::card::{DirEntry, Node, GAME_SAVE_NAME, MODE_DIR_DEFAULT, MODE_FILE_DEFAULT};
use gt4saves::{Card, Library, LibraryPaths};
use std::fs;
use std::path::PathBuf;

const STAMP: [u8; 8] = [0, 39, 50, 7, 12, 10, 0xEA, 0x07];

fn scratch(name: &str) -> PathBuf {
    let path = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join(name);
    let _ = fs::remove_dir_all(&path);
    fs::create_dir_all(&path).unwrap();
    path
}

fn game_save(seed: u8) -> Node {
    let file = |name: &str, len: usize| {
        let entry = DirEntry::new(name, MODE_FILE_DEFAULT, STAMP, STAMP).unwrap();
        Node::file(entry, (0..len).map(|i| (i as u8).wrapping_add(seed)).collect())
    };
    let entry = DirEntry::new(GAME_SAVE_NAME, MODE_DIR_DEFAULT, STAMP, STAMP).unwrap();
    Node::dir(entry, vec![file("icon.sys", 964), file("garage", 20_000)])
}

fn card_with(seed: u8) -> Vec<u8> {
    Card::build(&[game_save(seed)]).unwrap().to_image()
}

#[test]
fn the_libraries_of_two_games_are_different_folders() {
    let root = scratch("per-game-paths");
    let gt4 = LibraryPaths::for_game(&root, "gt4");
    let spec2 = LibraryPaths::for_game(&root, "spec2");
    assert_eq!(gt4.saves, root.join("games/gt4/Saves"));
    assert_eq!(spec2.replays, root.join("games/spec2/Replays"));
    assert_eq!(spec2.backups, root.join("games/spec2/backups"));
    assert_ne!(gt4, spec2);
}

#[test]
fn an_import_or_harvest_of_one_game_never_reaches_the_other() {
    let root = scratch("per-game-never-mix");
    let gt4 = Library::open_paths(&LibraryPaths::for_game(&root, "gt4")).unwrap();
    let spec2 = Library::open_paths(&LibraryPaths::for_game(&root, "spec2")).unwrap();

    // Import into Gran Turismo 4: Spec II sees nothing.
    let report = gt4.import_card_image(&card_with(1)).unwrap();
    assert_eq!(report.added.len(), 1);
    gt4.activate_slot("Save 01").unwrap();
    assert_eq!(gt4.active_save().unwrap().as_deref(), Some(GAME_SAVE_NAME));
    assert_eq!(spec2.active_save().unwrap(), None);
    assert!(spec2.slots().unwrap().is_empty());

    // The runtime card of each game is built from its own library only.
    let gt4_card = Card::from_image(&gt4.build_card(&[]).unwrap()).unwrap();
    let spec2_card = Card::from_image(&spec2.build_card(&[]).unwrap()).unwrap();
    assert!(gt4saves::card::same_contents(&gt4_card.root().unwrap(), &[game_save(1)]));
    assert!(spec2_card.root().unwrap().is_empty(), "a Spec II card starts without a Gran Turismo 4 save");

    // The game wrote a save on the Spec II card: it is harvested into the Spec II library only.
    spec2.harvest(&card_with(2)).unwrap();
    assert_eq!(spec2.active_save().unwrap().as_deref(), Some(GAME_SAVE_NAME));
    let still_gt4 = Card::from_image(&gt4.build_card(&[]).unwrap()).unwrap();
    assert!(gt4saves::card::same_contents(&still_gt4.root().unwrap(), &[game_save(1)]), "Gran Turismo 4's save did not change");
    let spec2_now = Card::from_image(&spec2.build_card(&[]).unwrap()).unwrap();
    assert!(gt4saves::card::same_contents(&spec2_now.root().unwrap(), &[game_save(2)]));
}

#[test]
fn no_known_rule_tells_the_games_of_a_save_apart_so_an_import_must_ask() {
    assert_eq!(gt4saves::game_of_save(&game_save(1)), None);
}
