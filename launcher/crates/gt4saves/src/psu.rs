//! Single-save `.psu` files (uLaunchELF / EMS layout): one 512-byte directory entry for the
//! save folder, then its "." and ".." entries, then each file as a 512-byte entry followed by
//! its data padded to 1024 bytes.
//!
//! Layout facts are from public format notes, not from a sample file: the 1024-byte data
//! padding is a hypothesis to confirm with a real `.psu` (see the TASK-025 report).

use crate::card::{CardError, DirEntry, Node, CLUSTER_BYTES, DIRENT_BYTES, MODE_DIR_DEFAULT, MODE_DOTDOT};

/// Writes a save folder (a directory node whose children are files) as `.psu` bytes.
pub fn write(save: &Node) -> Result<Vec<u8>, CardError> {
    if !save.entry.is_dir() {
        return Err(CardError::Corrupt("a .psu holds a save folder, not a file".into()));
    }
    let count = save.children.len() + 2;
    let mut out = Vec::new();
    let mut head = save.entry.clone();
    head.set_length(count as u32);
    out.extend_from_slice(head.raw());
    let dot = DirEntry::new(".", MODE_DIR_DEFAULT, save.entry.created(), save.entry.modified())?;
    let dotdot = DirEntry::new("..", MODE_DOTDOT, save.entry.created(), save.entry.modified())?;
    out.extend_from_slice(dot.raw());
    out.extend_from_slice(dotdot.raw());
    for child in &save.children {
        if child.entry.is_dir() {
            return Err(CardError::Corrupt("nested folders cannot be written to a .psu".into()));
        }
        let mut entry = child.entry.clone();
        entry.set_length(child.data.len() as u32);
        entry.set_cluster(0);
        out.extend_from_slice(entry.raw());
        out.extend_from_slice(&child.data);
        let padded = child.data.len().div_ceil(CLUSTER_BYTES) * CLUSTER_BYTES;
        out.resize(out.len() + padded - child.data.len(), 0);
    }
    Ok(out)
}

/// Reads `.psu` bytes into a save folder node.
pub fn read(bytes: &[u8]) -> Result<Node, CardError> {
    let corrupt = |what: &str| CardError::Corrupt(format!("psu: {what}"));
    let head = DirEntry::from_raw(bytes.get(..DIRENT_BYTES).ok_or_else(|| corrupt("too short"))?)?;
    if !head.is_dir() {
        return Err(corrupt("the first entry is not a folder"));
    }
    let count = head.length() as usize;
    if count < 2 {
        return Err(corrupt("folder entry count below 2"));
    }
    let mut offset = DIRENT_BYTES;
    let mut children = Vec::new();
    // Skip the "." and ".." entries that follow the folder entry.
    for index in 0..count {
        let raw = bytes.get(offset..offset + DIRENT_BYTES).ok_or_else(|| corrupt("entry past the end"))?;
        offset += DIRENT_BYTES;
        let entry = DirEntry::from_raw(raw)?;
        if index < 2 {
            if !entry.is_dot() {
                return Err(corrupt("missing \".\" or \"..\" entry"));
            }
            continue;
        }
        if entry.is_dir() {
            return Err(corrupt("nested folder"));
        }
        let size = entry.length() as usize;
        let padded = size.div_ceil(CLUSTER_BYTES) * CLUSTER_BYTES;
        let data = bytes.get(offset..offset + size).ok_or_else(|| corrupt("file data past the end"))?.to_vec();
        offset += padded;
        children.push(Node::file(entry, data));
    }
    Ok(Node::dir(head, children))
}
