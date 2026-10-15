// Parity tests against the owner's memory card copy (read-only). The copy is taken from
// private/manual-play/card.ps2 into a scratch directory; the tests skip when it is missing.
// Override the path with GT4SAVES_TEST_CARD.

use gt4saves::card::{Node, IMAGE_WITHOUT_ECC, IMAGE_WITH_ECC};
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