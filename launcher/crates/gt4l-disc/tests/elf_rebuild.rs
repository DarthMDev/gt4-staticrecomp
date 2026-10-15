//! ELF rebuild: the synthetic game rebuilds exactly, and every kind of CORE damage is refused.

use gt4l_disc::elf::{self, ElfError};
use gt4l_disc::testing::synthetic_game;

const BODY: &[u8] = b"synthetic guest code, not game data";

#[test]
fn synthetic_core_rebuilds_to_the_pinned_elf() {
    let game = synthetic_game(0x0010_0000, 0x0010_0008, BODY);
    let rebuilt = elf::reconstruct(&game.core, &game.policy).expect("rebuild succeeds");
    assert_eq!(rebuilt, game.elf);
    assert_eq!(&rebuilt[..4], b"\x7fELF");
    assert_eq!(u16::from_le_bytes([rebuilt[18], rebuilt[19]]), 8, "e_machine is MIPS");
    assert_eq!(u32::from_le_bytes(rebuilt[24..28].try_into().unwrap()), 0x0010_0008, "entry point");
    // The one program header sits right after the 52-byte ELF header.
    assert_eq!(u32::from_le_bytes(rebuilt[52 + 4..52 + 8].try_into().unwrap()), 4096, "file offset");
    assert_eq!(&rebuilt[4096..4096 + BODY.len()], BODY);
}

#[test]
fn truncated_or_bad_stream_is_refused() {
    let game = synthetic_game(0x0010_0000, 0x0010_0008, BODY);
    assert_eq!(elf::inflate_core(&game.core[..5]), Err(ElfError::Truncated("header")));
    let mut trailing = game.core.clone();
    trailing.push(0);
    assert!(matches!(elf::inflate_core(&trailing), Err(ElfError::InvalidDeflate(_))));
    let mut wrong_size = game.core.clone();
    wrong_size[2] = wrong_size[2].wrapping_add(1);
    assert!(matches!(elf::inflate_core(&wrong_size), Err(ElfError::InvalidDeflate(_))));
    let mut too_big = game.core.clone();
    too_big[2..6].copy_from_slice(&(9u32 * 1024 * 1024).to_le_bytes());
    assert!(matches!(elf::inflate_core(&too_big), Err(ElfError::InflatedSizeLimit(_))));
}

#[test]
fn policy_mismatches_are_refused() {
    let game = synthetic_game(0x0010_0000, 0x0010_0008, BODY);

    let mut wrong_entry = game.policy.clone();
    wrong_entry.entry_address = 0x0010_0010;
    assert!(matches!(elf::reconstruct(&game.core, &wrong_entry), Err(ElfError::PolicyMismatch(_))));

    let mut wrong_hash = game.policy.clone();
    wrong_hash.records[0].payload_sha256 = "00".repeat(32);
    assert!(matches!(elf::reconstruct(&game.core, &wrong_hash), Err(ElfError::PolicyMismatch(_))));

    let mut wrong_address = game.policy.clone();
    wrong_address.records[0].core_target_address = 0x0020_0000;
    assert!(matches!(elf::reconstruct(&game.core, &wrong_address), Err(ElfError::PolicyMismatch(_))));

    let mut overlapping = game.policy.clone();
    overlapping.records[0].reference_program_header.file_offset = 40;
    assert!(matches!(elf::reconstruct(&game.core, &overlapping), Err(ElfError::Layout(_)) | Err(ElfError::HashMismatch)));

    let mut pinned_elf_hash = game.policy.clone();
    pinned_elf_hash.native_elf_sha256 = "11".repeat(32);
    assert_eq!(elf::reconstruct(&game.core, &pinned_elf_hash), Err(ElfError::HashMismatch));
}
