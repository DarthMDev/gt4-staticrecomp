//! Setup against small synthetic ISO images: good disc, wrong serial/version/region, damaged dump,
//! truncated copy, wrong file type, cancellation and the staging/rename rules.

use std::fs;
use std::path::PathBuf;
use std::sync::atomic::AtomicBool;

use gt4l_disc::setup::{self, SetupError, SetupRequest, Stage};
use gt4l_disc::testing::{build_iso, pins_for, synthetic_game, system_cnf, SyntheticGame};
use gt4l_disc::Pins;

const BODY: &[u8] = b"synthetic guest code, not game data, 0123456789abcdef";
const SPEC_TRAILER: usize = 3000;

struct Fixture {
    root: PathBuf,
}

impl Fixture {
    fn new(name: &str) -> Fixture {
        let root = std::env::temp_dir().join(format!("gt4l-setup-{name}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(&root).unwrap();
        Fixture { root }
    }

    fn path(&self, name: &str) -> PathBuf {
        self.root.join(name)
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.root);
    }
}

/// A disc image for the synthetic game with the given `SYSTEM.CNF` boot name, version and volume id.
fn image_for(game: &SyntheticGame, boot_name: &str, version: &str, volume: &str) -> Vec<u8> {
    build_iso(
        volume,
        &[
            ("SYSTEM.CNF;1", system_cnf(boot_name, version, "NTSC")),
            ("CORE.GT4;1", game.core.clone()),
            ("SCUS_973.28;1", vec![0xA5; SPEC_TRAILER]),
        ],
    )
}

/// Pins that describe the good image, plus the image itself.
fn good_pins_and_image(volume: &str) -> (Pins, Vec<u8>) {
    let game = synthetic_game(0x0010_0000, 0x0010_0008, BODY);
    let image = image_for(&game, "SCUS_973.28", "2.00", volume);
    let pins = pins_for(&image, &game, "SCUS-97328", "2.00");
    (pins, image)
}

fn run_setup(
    fixture: &Fixture,
    disc: &PathBuf,
    pins: &Pins,
    cancel: &AtomicBool,
) -> Result<setup::Receipt, SetupError> {
    let prepared = fixture.path("prepared");
    let staging = fixture.path("prepared.tmp");
    let request = SetupRequest {
        disc,
        prepared: &prepared,
        staging: &staging,
        pins,
        launcher_version: "test",
        cancel,
    };
    let mut seen: Vec<(Stage, f32)> = Vec::new();
    let result = setup::run(&request, &mut |stage, fraction| seen.push((stage, fraction)));
    if result.is_ok() {
        assert!(seen.iter().any(|(stage, _)| *stage == Stage::Checking));
        assert_eq!(seen.last().copied(), Some((Stage::Preparing, 1.0)));
    }
    result
}

#[test]
fn good_disc_produces_the_prepared_folder_and_receipt() {
    let fixture = Fixture::new("good");
    let (pins, image) = good_pins_and_image("GT4 USA");
    let disc = fixture.path("game.iso");
    fs::write(&disc, &image).unwrap();
    let cancel = AtomicBool::new(false);
    let receipt = run_setup(&fixture, &disc, &pins, &cancel).expect("setup succeeds");
    assert_eq!(receipt.disc.serial, "SCUS-97328");
    assert_eq!(receipt.disc.volume_identifier, "GT4 USA");
    let prepared = fixture.path("prepared");
    assert_eq!(fs::read(prepared.join("SCUS_973.28")).unwrap(), expected_elf());
    assert!(prepared.join("CORE.GT4").exists());
    assert!(prepared.join("prepare.json").exists());
    assert!(!fixture.path("prepared.tmp").exists(), "staging folder is renamed away");
    setup::verify_prepared(&prepared, &pins).expect("prepared sizes match the pins");
    let receipt_text = fs::read_to_string(prepared.join("prepare.json")).unwrap();
    assert!(receipt_text.contains("\"payload\": \"bundled\""));
}

/// The ELF the synthetic game must rebuild to (the same build the pins were made from).
fn expected_elf() -> Vec<u8> {
    synthetic_game(0x0010_0000, 0x0010_0008, BODY).elf
}

#[test]
fn a_second_run_replaces_the_prepared_folder() {
    let fixture = Fixture::new("rerun");
    let (pins, image) = good_pins_and_image("GT4 USA");
    let disc = fixture.path("game.iso");
    fs::write(&disc, &image).unwrap();
    let cancel = AtomicBool::new(false);
    run_setup(&fixture, &disc, &pins, &cancel).unwrap();
    fs::write(fixture.path("prepared").join("stale.txt"), b"old").unwrap();
    run_setup(&fixture, &disc, &pins, &cancel).unwrap();
    assert!(!fixture.path("prepared").join("stale.txt").exists());
    let leftovers: Vec<_> = fs::read_dir(&fixture.root)
        .unwrap()
        .filter_map(|entry| entry.ok().map(|entry| entry.file_name().to_string_lossy().into_owned()))
        .filter(|name| name.starts_with("prepared.old-"))
        .collect();
    assert!(leftovers.is_empty(), "old folder removed: {leftovers:?}");
}

#[test]
fn wrong_serial_region_and_spec_ii_have_their_own_messages() {
    let fixture = Fixture::new("identity");
    let cancel = AtomicBool::new(false);
    let game = synthetic_game(0x0010_0000, 0x0010_0008, BODY);
    let disc = fixture.path("other.iso");

    // Another US serial: the disc's SYSTEM.CNF names SCUS_971.00.
    let image = image_for(&game, "SCUS_971.00", "2.00", "GT4 USA");
    fs::write(&disc, &image).unwrap();
    let pins = pins_for(&image, &game, "SCUS-97328", "2.00");
    let error = run_setup(&fixture, &disc, &pins, &cancel).unwrap_err();
    assert!(matches!(&error, SetupError::NotGt4 { serial } if serial == "SCUS-97100"), "{error:?}");
    assert_eq!(
        error.user_message(),
        "This is not Gran Turismo 4 USA (SCUS-97328). The disc says SCUS-97100."
    );

    // A PAL boot path.
    let image = image_for(&game, "SCES_500.01", "2.00", "GT4 EUR");
    fs::write(&disc, &image).unwrap();
    let pins = pins_for(&image, &game, "SCUS-97328", "2.00");
    assert!(matches!(run_setup(&fixture, &disc, &pins, &cancel), Err(SetupError::PalVersion)));

    // Spec II: a different serial and a volume identifier that names it.
    let spec = fixture.path("spec.iso");
    let image = image_for(&game, "SCUS_971.99", "2.00", "GT4 SPEC II");
    fs::write(&spec, &image).unwrap();
    let pins = pins_for(&image, &game, "SCUS-97328", "2.00");
    assert!(matches!(run_setup(&fixture, &spec, &pins, &cancel), Err(SetupError::SpecII)));
}

#[test]
fn other_version_is_refused_with_the_version_it_found() {
    let fixture = Fixture::new("version");
    let cancel = AtomicBool::new(false);
    let game = synthetic_game(0x0010_0000, 0x0010_0008, BODY);
    let disc = fixture.path("v1.iso");
    let image = image_for(&game, "SCUS_973.28", "1.00", "GT4 USA");
    fs::write(&disc, &image).unwrap();
    let pins = pins_for(&image, &game, "SCUS-97328", "2.00");
    match run_setup(&fixture, &disc, &pins, &cancel) {
        Err(SetupError::WrongVersion { version }) => assert_eq!(version, "1.00"),
        other => panic!("expected WrongVersion, got {other:?}"),
    }
}

#[test]
fn truncated_copy_is_incomplete_and_a_changed_byte_is_a_different_dump() {
    let fixture = Fixture::new("dump");
    let cancel = AtomicBool::new(false);
    let (pins, image) = good_pins_and_image("GT4 USA");

    let truncated = fixture.path("truncated.iso");
    fs::write(&truncated, &image[..image.len() - 2048]).unwrap();
    let error = run_setup(&fixture, &truncated, &pins, &cancel).unwrap_err();
    assert_eq!(error.user_message(), "This copy is incomplete or modified.");

    // Flip one byte inside the CORE file's data (same size, different hash).
    let core_offset = find_bytes(&image, &game_core_bytes()).expect("CORE data present in image");
    let mut changed = image.clone();
    changed[core_offset + 100] ^= 0xFF;
    let damaged = fixture.path("damaged.iso");
    fs::write(&damaged, &changed).unwrap();
    let error = run_setup(&fixture, &damaged, &pins, &cancel).unwrap_err();
    assert!(matches!(error, SetupError::DiffersFromKnownGood), "{error:?}");
    assert!(!fixture.path("prepared").exists(), "nothing is written for a rejected disc");
}

fn game_core_bytes() -> Vec<u8> {
    synthetic_game(0x0010_0000, 0x0010_0008, BODY).core
}

fn find_bytes(haystack: &[u8], needle: &[u8]) -> Option<usize> {
    haystack.windows(needle.len()).position(|window| window == needle)
}

#[test]
fn wrong_file_types_are_refused_before_hashing() {
    let fixture = Fixture::new("types");
    let cancel = AtomicBool::new(false);
    let (pins, image) = good_pins_and_image("GT4 USA");

    let bin = fixture.path("game.bin");
    fs::write(&bin, &image).unwrap();
    let error = run_setup(&fixture, &bin, &pins, &cancel).unwrap_err();
    assert_eq!(
        error.user_message(),
        "Please use a plain .iso file. A .bin file is not supported. Convert it to a plain .iso first."
    );

    let chd = fixture.path("game.chd");
    fs::write(&chd, &image).unwrap();
    let error = run_setup(&fixture, &chd, &pins, &cancel).unwrap_err();
    assert_eq!(error.user_message(), "This image is compressed (.chd). Please convert it to a plain .iso.");

    let zeros = fixture.path("zeros.iso");
    fs::write(&zeros, vec![0u8; 20 * 2048]).unwrap();
    let error = run_setup(&fixture, &zeros, &pins, &cancel).unwrap_err();
    assert!(matches!(error, SetupError::NotIso(_)), "{error:?}");
}

#[test]
fn cancel_leaves_no_staging_folder() {
    let fixture = Fixture::new("cancel");
    let (pins, image) = good_pins_and_image("GT4 USA");
    let disc = fixture.path("game.iso");
    fs::write(&disc, &image).unwrap();
    let cancel = AtomicBool::new(true);
    let error = run_setup(&fixture, &disc, &pins, &cancel).unwrap_err();
    assert!(matches!(error, SetupError::Cancelled));
    assert!(!fixture.path("prepared.tmp").exists());
    assert!(!fixture.path("prepared").exists());
}
