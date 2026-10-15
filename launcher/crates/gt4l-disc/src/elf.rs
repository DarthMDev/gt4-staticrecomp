//! Rebuilds the game's ELF from `CORE.GT4`, a port of `ee_producer.py::reconstruct_elf`.
//!
//! Nothing is compiled and the game code is not changed: the CORE payload is inflated (raw deflate),
//! its records are copied to their program-header offsets according to the pinned layout in
//! `docs/inputs/usa-v2.00-native.json`, and the result must hash to the pinned ELF.

use flate2::{Decompress, FlushDecompress, Status};
use serde::Deserialize;
use sha2::{Digest, Sha256};

/// Largest inflated CORE payload accepted (the qualified limit in `ee_producer.py`).
pub const MAX_INFLATED_CORE: usize = 8 * 1024 * 1024;
/// ELF flags for the EE (`EF_MIPS_*` style value used by the qualified layout).
pub const R5900_FLAGS: u32 = 0x2092_4001;
const ELF_HEADER_BYTES: usize = 52;
const PROGRAM_HEADER_BYTES: usize = 32;
const MAX_ELF_BYTES: u64 = 16 * 1024 * 1024;

/// The pinned layout (`docs/inputs/usa-v2.00-native.json`). Unknown fields are ignored.
#[derive(Clone, Debug, Deserialize)]
pub struct NativePolicy {
    pub entry_address: u32,
    pub native_elf_size: u64,
    pub native_elf_sha256: String,
    pub records: Vec<CoreRecord>,
}

#[derive(Clone, Debug, Deserialize)]
pub struct CoreRecord {
    pub core_target_address: u32,
    pub size_bytes: u32,
    pub inflated_payload_offset: u32,
    pub payload_sha256: String,
    pub reference_program_header: ProgramHeader,
}

#[derive(Clone, Debug, Deserialize)]
pub struct ProgramHeader {
    #[serde(rename = "type")]
    pub kind: u32,
    pub file_offset: u32,
    pub virtual_address: u32,
    pub physical_address: u32,
    pub file_size: u32,
    pub memory_size: u32,
    pub flags: u32,
    pub alignment: u32,
}

#[derive(Debug, PartialEq, Eq)]
pub enum ElfError {
    Truncated(&'static str),
    InflatedSizeLimit(u32),
    InvalidDeflate(String),
    /// The CORE payload disagrees with the pinned layout (a bug or a different disc).
    PolicyMismatch(String),
    Layout(String),
    HashMismatch,
}

impl std::fmt::Display for ElfError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ElfError::Truncated(what) => write!(formatter, "CORE is truncated ({what})"),
            ElfError::InflatedSizeLimit(size) => write!(formatter, "CORE inflates to {size} bytes, above the limit"),
            ElfError::InvalidDeflate(detail) => write!(formatter, "CORE deflate stream is invalid: {detail}"),
            ElfError::PolicyMismatch(detail) => write!(formatter, "CORE differs from the pinned layout: {detail}"),
            ElfError::Layout(detail) => write!(formatter, "ELF layout is invalid: {detail}"),
            ElfError::HashMismatch => write!(formatter, "rebuilt ELF differs from the pinned ELF"),
        }
    }
}

impl std::error::Error for ElfError {}

/// Rebuilds the ELF from raw `CORE.GT4` bytes and checks it against the pinned hash.
pub fn reconstruct(core: &[u8], policy: &NativePolicy) -> Result<Vec<u8>, ElfError> {
    let payload = inflate_core(core)?;
    let parts = split_payload(&payload, policy)?;
    let image = assemble(&parts, policy)?;
    if sha256_hex(&image) != policy.native_elf_sha256 {
        return Err(ElfError::HashMismatch);
    }
    Ok(image)
}

/// Inflates the CORE payload: `u16 unused`, `u32 inflated size`, then a raw deflate stream.
pub fn inflate_core(core: &[u8]) -> Result<Vec<u8>, ElfError> {
    if core.len() < 6 {
        return Err(ElfError::Truncated("header"));
    }
    let inflated_size = u32::from_le_bytes([core[2], core[3], core[4], core[5]]);
    if inflated_size as usize > MAX_INFLATED_CORE {
        return Err(ElfError::InflatedSizeLimit(inflated_size));
    }
    let expected = inflated_size as usize;
    let mut decoder = Decompress::new(false);
    let mut output = Vec::with_capacity(expected + 1);
    let status = decoder
        .decompress_vec(&core[6..], &mut output, FlushDecompress::Finish)
        .map_err(|error| ElfError::InvalidDeflate(error.to_string()))?;
    if status != Status::StreamEnd {
        return Err(ElfError::InvalidDeflate("stream does not end at the inflated size".into()));
    }
    if decoder.total_in() as usize != core.len() - 6 {
        return Err(ElfError::InvalidDeflate("unused data after the stream".into()));
    }
    if output.len() != expected {
        return Err(ElfError::InvalidDeflate(format!(
            "inflated to {} bytes, header says {expected}",
            output.len()
        )));
    }
    Ok(output)
}

/// One program segment: the pinned program header and the bytes that go into the file.
pub struct Part<'a> {
    pub header: &'a ProgramHeader,
    pub body: &'a [u8],
}

/// Walks the inflated payload: two length-prefixed preamble blocks, then `count`, `entry` and the
/// records. Every record is checked against the pinned policy (address, size, offset, SHA-256).
pub fn split_payload<'a>(payload: &'a [u8], policy: &'a NativePolicy) -> Result<Vec<Part<'a>>, ElfError> {
    let need = |offset: usize, size: usize| -> Result<(), ElfError> {
        if offset.checked_add(size).map_or(true, |end| end > payload.len()) {
            Err(ElfError::Truncated("payload"))
        } else {
            Ok(())
        }
    };
    let mut offset = 0usize;
    for _ in 0..2 {
        need(offset, 2)?;
        let length = u16::from_le_bytes([payload[offset], payload[offset + 1]]) as usize;
        need(offset + 2, length)?;
        offset += 2 + length;
    }
    need(offset, 8)?;
    let count = i32::from_le_bytes(payload[offset..offset + 4].try_into().unwrap());
    let entry = u32::from_le_bytes(payload[offset + 4..offset + 8].try_into().unwrap());
    offset += 8;
    if count != policy.records.len() as i32 || count <= 0 || count > 16 || entry != policy.entry_address {
        return Err(ElfError::PolicyMismatch("record count or entry address".into()));
    }
    let mut parts = Vec::with_capacity(policy.records.len());
    for record in &policy.records {
        need(offset, 8)?;
        let address = u32::from_le_bytes(payload[offset..offset + 4].try_into().unwrap());
        let size = u32::from_le_bytes(payload[offset + 4..offset + 8].try_into().unwrap());
        need(offset + 8, size as usize)?;
        let body = &payload[offset + 8..offset + 8 + size as usize];
        if address != record.core_target_address
            || size != record.size_bytes
            || (offset + 8) as u64 != record.inflated_payload_offset as u64
            || sha256_hex(body) != record.payload_sha256
        {
            return Err(ElfError::PolicyMismatch(format!("record at 0x{address:08X}")));
        }
        parts.push(Part { header: &record.reference_program_header, body });
        offset += 8 + size as usize;
    }
    if offset != payload.len() {
        return Err(ElfError::PolicyMismatch("unexpected trailing payload".into()));
    }
    Ok(parts)
}

/// Lays out the ELF file: header, program-header table (sorted by file offset) and segment bodies.
pub fn assemble(parts: &[Part<'_>], policy: &NativePolicy) -> Result<Vec<u8>, ElfError> {
    let count = parts.len();
    if policy.native_elf_size < ELF_HEADER_BYTES as u64 || policy.native_elf_size > MAX_ELF_BYTES {
        return Err(ElfError::Layout("ELF size outside the qualified limit".into()));
    }
    let size = policy.native_elf_size as usize;
    if ELF_HEADER_BYTES + PROGRAM_HEADER_BYTES * count > size {
        return Err(ElfError::Layout("program header table does not fit".into()));
    }
    let mut output = vec![0u8; size];
    output[..7].copy_from_slice(&[0x7f, b'E', b'L', b'F', 1, 1, 1]);
    let mut header = Vec::with_capacity(48);
    push_u16(&mut header, 2); // e_type: executable
    push_u16(&mut header, 8); // e_machine: MIPS
    push_u32(&mut header, 1); // e_version
    push_u32(&mut header, policy.entry_address);
    push_u32(&mut header, ELF_HEADER_BYTES as u32); // e_phoff
    push_u32(&mut header, 0); // e_shoff
    push_u32(&mut header, R5900_FLAGS);
    push_u16(&mut header, ELF_HEADER_BYTES as u16);
    push_u16(&mut header, PROGRAM_HEADER_BYTES as u16);
    push_u16(&mut header, count as u16);
    push_u16(&mut header, 0); // e_shentsize
    push_u16(&mut header, 0); // e_shnum
    push_u16(&mut header, 0); // e_shstrndx
    output[16..16 + header.len()].copy_from_slice(&header);

    let mut ordered: Vec<&Part<'_>> = parts.iter().collect();
    ordered.sort_by_key(|part| part.header.file_offset);
    let mut ranges: Vec<(u64, u64)> = vec![(0, (ELF_HEADER_BYTES + PROGRAM_HEADER_BYTES * count) as u64)];
    for (index, part) in ordered.iter().enumerate() {
        let header = part.header;
        let start = header.file_offset as u64;
        let end = start + header.file_size as u64;
        if header.file_size as usize != part.body.len()
            || end > size as u64
            || header.memory_size < header.file_size
            || header.virtual_address as u64 + header.memory_size as u64 > 1 << 32
            || header.physical_address as u64 + header.memory_size as u64 > 1 << 32
            || ranges.iter().any(|&(previous_start, previous_end)| start < previous_end && end > previous_start)
        {
            return Err(ElfError::Layout(format!("segment {index} is outside the file or overlaps")));
        }
        let at = ELF_HEADER_BYTES + index * PROGRAM_HEADER_BYTES;
        let mut program_header = Vec::with_capacity(PROGRAM_HEADER_BYTES);
        for field in [
            header.kind,
            header.file_offset,
            header.virtual_address,
            header.physical_address,
            header.file_size,
            header.memory_size,
            header.flags,
            header.alignment,
        ] {
            push_u32(&mut program_header, field);
        }
        output[at..at + PROGRAM_HEADER_BYTES].copy_from_slice(&program_header);
        output[start as usize..end as usize].copy_from_slice(part.body);
        ranges.push((start, end));
    }
    Ok(output)
}

pub fn sha256_hex(bytes: &[u8]) -> String {
    let digest = Sha256::digest(bytes);
    digest.iter().map(|byte| format!("{byte:02x}")).collect()
}

fn push_u16(target: &mut Vec<u8>, value: u16) {
    target.extend_from_slice(&value.to_le_bytes());
}

fn push_u32(target: &mut Vec<u8>, value: u32) {
    target.extend_from_slice(&value.to_le_bytes());
}
