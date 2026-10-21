// Parity tests against the owner's memory card copy (read-only). The copy is taken from
// private/manual-play/card.ps2 into a scratch directory; the tests skip when it is missing.
// Override the path with GT4SAVES_TEST_CARD.

use gt4saves::card::{DirEntry, Node, IMAGE_WITHOUT_ECC, IMAGE_WITH_ECC, MODE_DIR_DEFAULT, MODE_FILE_DEFAULT};
use gt4saves::{ecc, Card};
use std::path::PathBuf;

fn owner_card_bytes() -> Option<Vec<u8>> {
    let path = std::env::var("GT4SAVES_TEST_CARD").map(PathBuf::from).unwrap_or_else(|_| {
        PathBuf::from("C:/Antigravity/gt4-staticrecomp/private/slice180-saves-owner-copy/card-copy.ps2")
    });
    std::fs::read(path).ok()
}

#[test]
fn ecc_matches_every_page_of_the_owner_card() {
    let Some(bytes) = owner_card_bytes() else {
        eprintln!("owner card copy not present; skipping");
        return;
    };
    assert_eq!(bytes.len(), IMAGE_WITH_ECC);
    for (index, page) in bytes.chunks_exact(528).enumerate() {
        if page.iter().all(|&b| b == 0xFF) {
            continue; // erased page: no ECC is written
        }
        let spare = ecc::page_spare(&page[..512]);
        assert_eq!(&page[512..524], &spare[..12], "ECC mismatch on page {index}");
        assert_eq!(&page[524..528], &[0, 0, 0, 0], "spare tail not zero on page {index}");
    }
}

#[test]
fn owner_card_lists_its_saves_and_reads_them() {
    let Some(bytes) = owner_card_bytes() else {
        return;
    };
    let card = Card::from_image(&bytes).expect("owner card opens");
    assert!(card.ecc_ok());
    let saves = card.root().expect("root directory reads");
    let names: Vec<String> = saves.iter().map(Node::name).collect();
    eprintln!("owner card root entries: {names:?}");
    assert!(names.iter().any(|n| n == "BASCUS-97328GAMEDATA"), "GT4 game save present: {names:?}");
    let game = saves.iter().find(|n| n.name() == "BASCUS-97328GAMEDATA").unwrap();
    for file in &game.children {
        eprintln!("  {} {} bytes", file.name(), file.data.len());
    }
}

#[test]
fn owner_card_round_trips_through_build() {
    let Some(bytes) = owner_card_bytes() else {
        return;
    };
    let card = Card::from_image(&bytes).unwrap();
    let saves = card.root().unwrap();
    let rebuilt = Card::build(&saves).expect("the owner's saves fit a fresh card");
    assert!(rebuilt.ecc_ok());
    assert_same_contents(&rebuilt.root().unwrap(), &saves, "");
    // Image without spare bytes is the same card minus the spare bytes.
    let raw = card.to_image_without_ecc();
    assert_eq!(raw.len(), IMAGE_WITHOUT_ECC);
    // A data-only dump keeps every data byte; spare bytes are recomputed (ECC is a function
    // of the data), so the reloaded card must still pass the ECC check.
    let reloaded = Card::from_image(&raw).unwrap();
    assert_eq!(reloaded.to_image_without_ecc(), raw);
    assert!(reloaded.ecc_ok());
}

/// Compares names, modes, lengths, timestamps and file bytes; cluster numbers may differ.
fn assert_same_contents(actual: &[Node], expected: &[Node], path: &str) {
    assert_eq!(actual.len(), expected.len(), "entry count under {path:?}");
    for (a, e) in actual.iter().zip(expected) {
        let here = format!("{path}/{}", e.name());
        assert_eq!(a.name(), e.name(), "name under {path:?}");
        assert_eq!(a.entry.mode(), e.entry.mode(), "mode of {here}");
        assert_eq!(a.entry.length(), e.entry.length(), "length of {here}");
        assert_eq!(a.entry.created(), e.entry.created(), "created time of {here}");
        assert_eq!(a.entry.modified(), e.entry.modified(), "modified time of {here}");
        assert!(a.data == e.data, "file bytes differ in {here}");
        assert_same_contents(&a.children, &e.children, &here);
    }
}
#[test]
fn fresh_superblock_matches_the_owner_card() {
    // A freshly formatted card carries the same superblock page as MCMAN writes.
    let Some(bytes) = owner_card_bytes() else {
        return;
    };
    let owner = Card::from_image(&bytes).unwrap();
    let blank = Card::blank();
    assert_eq!(blank.page(0), owner.page(0), "superblock data bytes");
}
/// For every sub-directory in the root: (name, its index in the root, the index its "." entry holds).
fn dot_entry_indices(card: &Card) -> Vec<(String, u32, u32)> {
    let root_count = card.read_dir(0, 1).unwrap()[0].length() as usize;
    let root = card.read_dir(0, root_count).unwrap();
    let mut found = Vec::new();
    for (index, entry) in root.iter().enumerate().skip(2) {
        if entry.exists() && entry.is_dir() {
            let own = card.read_dir(entry.cluster(), entry.length() as usize).unwrap();
            found.push((entry.name(), index as u32, own[0].dir_entry()));
        }
    }
    found
}

#[test]
fn owner_card_dot_entries_hold_the_directory_index() {
    let Some(bytes) = owner_card_bytes() else {
        return;
    };
    let card = Card::from_image(&bytes).unwrap();
    let found = dot_entry_indices(&card);
    assert!(!found.is_empty());
    for (name, index, held) in &found {
        assert_eq!(held, index, "{name}: the game's own card holds the directory index in \".\"");
    }
    // The rebuilt card must do the same, or GT4 shows "Load failed!" (2026-10-21).
    let rebuilt = Card::build(&card.root().unwrap()).unwrap();
    for (name, index, held) in dot_entry_indices(&rebuilt) {
        assert_eq!(held, index, "{name}: rebuilt \".\" entry");
    }
}

#[test]
fn built_card_dot_entries_hold_the_directory_index() {
    const STAMP: [u8; 8] = [0, 0, 0, 12, 21, 10, 0xEA, 0x07];
    let save = |name: &str| {
        let file = Node::file(DirEntry::new("icon.sys", MODE_FILE_DEFAULT, STAMP, STAMP).unwrap(), vec![1; 964]);
        Node::dir(DirEntry::new(name, MODE_DIR_DEFAULT, STAMP, STAMP).unwrap(), vec![file])
    };
    let card = Card::build(&[save("BASCUS-97328GAMEDATA"), save("BASCUS-97328REPLAY00"), save("BASCUS-97328REPLAY01")]).unwrap();
    let found = dot_entry_indices(&card);
    assert_eq!(found.iter().map(|(_, index, _)| *index).collect::<Vec<_>>(), vec![2, 3, 4]);
    for (name, index, held) in found {
        assert_eq!(held, index, "{name}");
    }
}
