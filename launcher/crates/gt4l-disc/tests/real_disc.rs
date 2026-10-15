//! Checks the embedded pins against the owner's real disc. Ignored by default because the full
//! SHA-256 of the 5.3 GB image takes a while. Run with:
//!   set GT4_REAL_ISO=<path to Gran Turismo 4 (USA) (v2.00).iso>
//!   cargo test -p gt4l-disc --test real_disc -- --ignored --nocapture
//! The image is only read; nothing is written next to it.

use std::path::PathBuf;

use gt4l_disc::cnf;
use gt4l_disc::elf;
use gt4l_disc::iso::Iso;
use gt4l_disc::Pins;

#[test]
fn embedded_pins_are_self_consistent() {
    let pins = Pins::embedded().expect("pins parse");
    assert_eq!(pins.serial, "SCUS-97328");
    assert_eq!(pins.version, "2.00");
    assert_eq!(pins.disc.size_bytes, 5_314_478_080);
    assert_eq!(pins.core.size_bytes, 2_020_861);
    assert_eq!(pins.elf.size_bytes, 6_123_004);
    assert_eq!(pins.policy.records.len(), 3);
}

#[test]
#[ignore = "needs the owner's disc image; see the module comment"]
fn real_disc_matches_the_pins_and_rebuilds_the_elf() {
    let path = PathBuf::from(std::env::var("GT4_REAL_ISO").expect("set GT4_REAL_ISO to the ISO path"));
    let pins = Pins::embedded().expect("pins parse");
    let mut iso = Iso::open(&path).expect("plain ISO");
    let system = iso.find("SYSTEM.CNF;1").expect("SYSTEM.CNF");
    let config = cnf::parse(&iso.read(&system, 4096).unwrap()).unwrap();
    assert_eq!(config.serial, pins.serial);
    let core_entry = iso.find("CORE.GT4;1").unwrap();
    let core = iso.read(&core_entry, 16 * 1024 * 1024).unwrap();
    assert_eq!(elf::sha256_hex(&core), pins.core.sha256);
    let image = elf::reconstruct(&core, &pins.policy).expect("rebuild");
    assert_eq!(elf::sha256_hex(&image), pins.elf.sha256);
}
