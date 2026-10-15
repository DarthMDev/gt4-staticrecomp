//! "Preparing game files": verify the disc, extract `CORE.GT4`, rebuild the ELF and write the
//! prepared folder. Output goes to a staging folder and is renamed into place at the end, so a
//! cancelled or failed run never leaves a half-written prepared folder behind.
//!
//! Steps (the UI shows them as rows):
//! 1. Format and identity: plain ISO, `SYSTEM.CNF` (serial and version), file size.
//! 2. Full SHA-256 of the image against the pin, with progress (`Stage::Checking`).
//! 3. Extract `CORE.GT4` and verify its pin; rebuild the ELF and verify its pin (`Stage::Preparing`).
//! 4. Write `CORE.GT4`, `SCUS_973.28` and `prepare.json` into the staging folder, then rename.
//! The memory card needs no step: a missing card is a blank card created by the game on first save.

use std::fs::{self, File};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

use serde::Serialize;
use sha2::{Digest, Sha256};

use crate::cnf;
use crate::elf;
use crate::iso::{Iso, IsoError};
use crate::pins::{Identity, Pins};

/// Name of the receipt written into the prepared folder.
pub const RECEIPT_FILE: &str = "prepare.json";
/// File name the rebuilt ELF must have (the donor derives the boot name from it).
pub const ELF_FILE: &str = "SCUS_973.28";
pub const CORE_FILE: &str = "CORE.GT4";
pub const RECEIPT_SCHEMA: u32 = 1;
const HASH_CHUNK_BYTES: usize = 4 * 1024 * 1024;
const MAX_SYSTEM_CNF_BYTES: u64 = 4096;
const MAX_CORE_BYTES: u64 = 16 * 1024 * 1024;

/// The two progress phases the UI shows.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Stage {
    Checking,
    Preparing,
}

/// Why setup stopped. `user_message` is the one or two plain sentences the UI shows.
#[derive(Debug)]
pub enum SetupError {
    NotPlainIso { extension: Option<String> },
    NotIso(String),
    NotPlayStation2,
    NotGt4 { serial: String },
    PalVersion,
    SpecII,
    WrongVersion { version: String },
    IncompleteOrModified,
    DiffersFromKnownGood,
    Cancelled,
    /// Internal inconsistency after the disc matched: reported as a bug.
    Internal(String),
    Io(String),
}

impl SetupError {
    pub fn user_message(&self) -> String {
        match self {
            SetupError::NotPlainIso { extension: Some(extension) } if extension.eq_ignore_ascii_case("chd") => {
                "This image is compressed (.chd). Please convert it to a plain .iso.".into()
            }
            SetupError::NotPlainIso { extension: Some(extension) } => format!(
                "Please use a plain .iso file. A .{extension} file is not supported. Convert it to a plain .iso first."
            ),
            SetupError::NotPlainIso { extension: None } => "Please choose a plain .iso file.".into(),
            SetupError::NotIso(_) => "This file is not a plain 2048-byte-sector .iso image.".into(),
            SetupError::NotPlayStation2 => "This is not a PlayStation 2 disc image.".into(),
            SetupError::NotGt4 { serial } => {
                format!("This is not Gran Turismo 4 USA (SCUS-97328). The disc says {serial}.")
            }
            SetupError::PalVersion => "PAL versions are not supported.".into(),
            SetupError::SpecII => "Gran Turismo 4 Spec II is not supported (yet).".into(),
            SetupError::WrongVersion { version } => {
                format!("This is GT4 USA version {version}; only the v2.00 disc is supported.")
            }
            SetupError::IncompleteOrModified => "This copy is incomplete or modified.".into(),
            SetupError::DiffersFromKnownGood => {
                "This dump differs from the known good v2.00 disc (damaged or patched). Please re-dump your disc."
                    .into()
            }
            SetupError::Cancelled => "Setup was cancelled. Nothing was changed.".into(),
            SetupError::Internal(_) | SetupError::Io(_) => {
                "Preparing the game files failed. Your disc was not changed. Please report this with the log.".into()
            }
        }
    }
}

impl std::fmt::Display for SetupError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            SetupError::NotIso(detail) | SetupError::Internal(detail) | SetupError::Io(detail) => {
                write!(formatter, "{}: {detail}", self.user_message())
            }
            other => write!(formatter, "{}", other.user_message()),
        }
    }
}

impl std::error::Error for SetupError {}

impl From<std::io::Error> for SetupError {
    fn from(error: std::io::Error) -> Self {
        SetupError::Io(error.to_string())
    }
}

/// Written to `prepare.json`. It records what was verified, for support and for later checks.
#[derive(Clone, Debug, Serialize)]
pub struct Receipt {
    pub schema_version: u32,
    pub launcher_version: String,
    pub payload: String,
    pub disc: DiscReceipt,
    pub core: Identity,
    pub elf: Identity,
    pub prepared_unix_seconds: u64,
}

#[derive(Clone, Debug, Serialize)]
pub struct DiscReceipt {
    pub path: String,
    pub size_bytes: u64,
    pub sha256: String,
    pub serial: String,
    pub version: String,
    pub volume_identifier: String,
}

/// What a successful identity check learned about the disc.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DiscSummary {
    pub serial: String,
    pub version: String,
    pub video_mode: String,
    pub volume_identifier: String,
    pub size_bytes: u64,
}

/// Inputs of one setup run.
pub struct SetupRequest<'a> {
    pub disc: &'a Path,
    /// `%LOCALAPPDATA%\GT4Recomp\prepared`.
    pub prepared: &'a Path,
    /// `%LOCALAPPDATA%\GT4Recomp\prepared.tmp`.
    pub staging: &'a Path,
    pub pins: &'a Pins,
    pub launcher_version: &'a str,
    pub cancel: &'a AtomicBool,
}

/// Fast identity checks (no full hash): extension, ISO structure, `SYSTEM.CNF`, size.
pub fn precheck(disc: &Path, pins: &Pins) -> Result<DiscSummary, SetupError> {
    let extension = disc.extension().and_then(|value| value.to_str()).map(str::to_string);
    match extension.as_deref() {
        Some(value) if value.eq_ignore_ascii_case("iso") => {}
        other => return Err(SetupError::NotPlainIso { extension: other.map(str::to_string) }),
    }
    let mut iso = Iso::open(disc).map_err(|error| match error {
        IsoError::Io(error) => SetupError::Io(error.to_string()),
        other => SetupError::NotIso(other.to_string()),
    })?;
    let system_entry = iso.find("SYSTEM.CNF;1").map_err(|_| SetupError::NotPlayStation2)?;
    let system_bytes = iso
        .read(&system_entry, MAX_SYSTEM_CNF_BYTES)
        .map_err(|error| SetupError::NotIso(error.to_string()))?;
    let config = cnf::parse(&system_bytes).map_err(|detail| SetupError::NotIso(detail))?;
    let volume_identifier = iso.volume_identifier().to_string();
    let length_bytes = iso.length_bytes();

    if config.serial != pins.serial || config.boot_path != pins.boot_path {
        if ["SCES", "SLES", "SCED", "SLED", "SCPS"].iter().any(|prefix| config.serial.starts_with(prefix)) {
            return Err(SetupError::PalVersion);
        }
        if volume_identifier.to_ascii_uppercase().contains("SPEC") {
            return Err(SetupError::SpecII);
        }
        return Err(SetupError::NotGt4 { serial: config.serial });
    }
    if config.version != pins.version {
        return Err(SetupError::WrongVersion { version: config.version });
    }
    if length_bytes != pins.disc.size_bytes {
        return Err(SetupError::IncompleteOrModified);
    }
    Ok(DiscSummary {
        serial: config.serial,
        version: config.version,
        video_mode: config.video_mode,
        volume_identifier,
        size_bytes: length_bytes,
    })
}

/// Full SHA-256 of the image, reporting the fraction done and honouring cancellation.
pub fn hash_image(
    path: &Path,
    cancel: &AtomicBool,
    progress: &mut dyn FnMut(Stage, f32),
) -> Result<Identity, SetupError> {
    let mut file = File::open(path)?;
    let total = file.metadata()?.len().max(1);
    let mut digest = Sha256::new();
    let mut buffer = vec![0u8; HASH_CHUNK_BYTES];
    let mut done = 0u64;
    loop {
        if cancel.load(Ordering::Relaxed) {
            return Err(SetupError::Cancelled);
        }
        let read = file.read(&mut buffer)?;
        if read == 0 {
            break;
        }
        digest.update(&buffer[..read]);
        done += read as u64;
        progress(Stage::Checking, done as f32 / total as f32);
    }
    let sha256: String = digest.finalize().iter().map(|byte| format!("{byte:02x}")).collect();
    Ok(Identity { size_bytes: done, sha256 })
}

/// Runs the whole setup. On success the prepared folder holds the receipt, CORE and ELF.
pub fn run(request: &SetupRequest<'_>, progress: &mut dyn FnMut(Stage, f32)) -> Result<Receipt, SetupError> {
    let pins = request.pins;
    let summary = precheck(request.disc, pins)?;
    progress(Stage::Checking, 0.0);
    let disc_identity = hash_image(request.disc, request.cancel, progress)?;
    if disc_identity.sha256 != pins.disc.sha256 || disc_identity.size_bytes != pins.disc.size_bytes {
        return Err(SetupError::DiffersFromKnownGood);
    }
    progress(Stage::Checking, 1.0);

    progress(Stage::Preparing, 0.0);
    let (core_bytes, elf_bytes) = extract_and_rebuild(request.disc, pins)?;
    progress(Stage::Preparing, 0.5);
    write_staging(request, &core_bytes, &elf_bytes)?;
    progress(Stage::Preparing, 1.0);

    let receipt = Receipt {
        schema_version: RECEIPT_SCHEMA,
        launcher_version: request.launcher_version.to_string(),
        payload: "bundled".to_string(),
        disc: DiscReceipt {
            path: request.disc.display().to_string(),
            size_bytes: disc_identity.size_bytes,
            sha256: disc_identity.sha256,
            serial: summary.serial,
            version: summary.version,
            volume_identifier: summary.volume_identifier,
        },
        core: Identity { size_bytes: core_bytes.len() as u64, sha256: elf::sha256_hex(&core_bytes) },
        elf: Identity { size_bytes: elf_bytes.len() as u64, sha256: elf::sha256_hex(&elf_bytes) },
        prepared_unix_seconds: unix_seconds(),
    };
    fs::write(request.staging.join(RECEIPT_FILE), serde_json::to_vec_pretty(&receipt).map_err(|error| {
        SetupError::Internal(error.to_string())
    })?)?;
    swap_into_place(request.staging, request.prepared)?;
    Ok(receipt)
}

fn extract_and_rebuild(disc: &Path, pins: &Pins) -> Result<(Vec<u8>, Vec<u8>), SetupError> {
    let mut iso = Iso::open(disc).map_err(|error| SetupError::Io(error.to_string()))?;
    let entry = iso
        .find("CORE.GT4;1")
        .map_err(|error| SetupError::Internal(error.to_string()))?;
    let core = iso
        .read(&entry, MAX_CORE_BYTES)
        .map_err(|error| SetupError::Internal(error.to_string()))?;
    let core_identity = Identity { size_bytes: core.len() as u64, sha256: elf::sha256_hex(&core) };
    if core_identity != pins.core {
        return Err(SetupError::Internal("CORE.GT4 differs from the pinned identity".into()));
    }
    let image = elf::reconstruct(&core, &pins.policy).map_err(|error| SetupError::Internal(error.to_string()))?;
    let elf_identity = Identity { size_bytes: image.len() as u64, sha256: elf::sha256_hex(&image) };
    if elf_identity != pins.elf {
        return Err(SetupError::Internal("rebuilt ELF differs from the pinned identity".into()));
    }
    Ok((core, image))
}

fn write_staging(request: &SetupRequest<'_>, core: &[u8], image: &[u8]) -> Result<(), SetupError> {
    if request.staging.exists() {
        fs::remove_dir_all(request.staging)?;
    }
    fs::create_dir_all(request.staging)?;
    write_synced(&request.staging.join(CORE_FILE), core)?;
    write_synced(&request.staging.join(ELF_FILE), image)?;
    Ok(())
}

fn write_synced(path: &Path, bytes: &[u8]) -> Result<(), SetupError> {
    let mut file = File::create(path)?;
    file.write_all(bytes)?;
    file.sync_all()?;
    Ok(())
}

/// Moves the staging folder to `prepared`. The old folder is removed only after the new one is in place.
fn swap_into_place(staging: &Path, prepared: &Path) -> Result<(), SetupError> {
    let old: Option<PathBuf> = if prepared.exists() {
        let parked = prepared.with_file_name(format!("prepared.old-{}", unix_seconds()));
        fs::rename(prepared, &parked)?;
        Some(parked)
    } else {
        None
    };
    fs::rename(staging, prepared)?;
    if let Some(old) = old {
        let _ = fs::remove_dir_all(old);
    }
    Ok(())
}

fn unix_seconds() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|duration| duration.as_secs()).unwrap_or(0)
}

/// Fast check before Play: the prepared files exist with the pinned sizes.
pub fn verify_prepared(prepared: &Path, pins: &Pins) -> Result<(), String> {
    let elf_path = prepared.join(ELF_FILE);
    let core_path = prepared.join(CORE_FILE);
    let elf_size = fs::metadata(&elf_path).map_err(|_| format!("{ELF_FILE} is missing"))?.len();
    if elf_size != pins.elf.size_bytes {
        return Err(format!("{ELF_FILE} has size {elf_size}, expected {}", pins.elf.size_bytes));
    }
    let core_size = fs::metadata(&core_path).map_err(|_| format!("{CORE_FILE} is missing"))?.len();
    if core_size != pins.core.size_bytes {
        return Err(format!("{CORE_FILE} has size {core_size}, expected {}", pins.core.size_bytes));
    }
    Ok(())
}
