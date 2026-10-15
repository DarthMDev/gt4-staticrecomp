//! PS2 memory card image: 8 MB of 528-byte pages (512 data + 16 spare, ECC in the first 12).
//!
//! Layout used here (fitted to the owner's card, see tests/card_owner.rs):
//! * cluster = two pages (1024 bytes); physical cluster c holds pages 2c and 2c+1;
//! * superblock at page 0; the indirect FAT list at superblock offset 0x50 names the
//!   physical cluster that lists the FAT clusters (8 on a fresh card);
//! * the FAT maps cluster numbers relative to `alloc_offset` (41) to the next relative
//!   cluster of a chain, or `FAT_END`; unused entries are `FAT_FREE`;
//! * the root directory is relative cluster 0; a directory holds 512-byte entries.
//!
//! Only what the launcher needs is implemented: read, list, read files, and build a new
//! card from a tree. Repairing damaged cards is out of scope.

use crate::ecc;
use std::fmt;

pub const PAGE_DATA_BYTES: usize = 512;
pub const PAGE_SPARE_BYTES: usize = 16;
pub const PAGE_BYTES: usize = PAGE_DATA_BYTES + PAGE_SPARE_BYTES;
pub const PAGE_COUNT: usize = 16384;
/// Card image with spare bytes (what the game and PCSX2 write). 8,650,752 bytes.
pub const IMAGE_WITH_ECC: usize = PAGE_BYTES * PAGE_COUNT;
/// Card dump without spare bytes. 8,388,608 bytes.
pub const IMAGE_WITHOUT_ECC: usize = PAGE_DATA_BYTES * PAGE_COUNT;
pub const CLUSTER_BYTES: usize = 1024;
pub const DIRENT_BYTES: usize = 512;

pub const MAGIC: &[u8] = b"Sony PS2 Memory Card Format 1.2.0.0";
/// FAT values (verified on the owner's card): a used entry is 0x8000_0000 | next cluster,
/// the last cluster of a chain holds FAT_END, an unused entry holds FAT_FREE.
pub const FAT_END: u32 = 0xFFFF_FFFF;
pub const FAT_FREE: u32 = 0x7FFF_FFFF;
const FAT_IN_USE: u32 = 0x8000_0000;
/// Mode of the ".." entry on the owner's card (it carries the 0x2000 flag).
pub const MODE_DOTDOT: u16 = 0xA426;

pub const MODE_EXISTS: u16 = 0x8000;
pub const MODE_FILE: u16 = 0x0010;
pub const MODE_SUBDIR: u16 = 0x0020;
pub const MODE_DIR_DEFAULT: u16 = 0x8427;
pub const MODE_FILE_DEFAULT: u16 = 0x8497;

/// Physical cluster that lists the FAT clusters (fresh cards and the owner's card).
const IFC_CLUSTER: u32 = 8;
const ALLOC_OFFSET: u32 = 41;
const ALLOC_END: u32 = 8135;
const FAT_CLUSTERS: u32 = 32;
const FAT_ENTRIES_PER_CLUSTER: u32 = (CLUSTER_BYTES / 4) as u32;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CardError {
    /// The image is neither the 8 MB spare-byte form nor the 8 MB data-only form.
    BadSize(usize),
    /// The superblock magic is missing: not a PS2 memory card.
    NotACard,
    /// A chain, directory or FAT entry points outside the card or loops.
    Corrupt(String),
    /// The tree does not fit the free clusters of an 8 MB card.
    Full,
    /// A name cannot be stored (empty, longer than 32 bytes or not ASCII).
    BadName(String),
}

impl fmt::Display for CardError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CardError::BadSize(size) => write!(f, "not a PS2 card image: {size} bytes"),
            CardError::NotACard => write!(f, "not a PS2 memory card: superblock magic missing"),
            CardError::Corrupt(what) => write!(f, "corrupt card: {what}"),
            CardError::Full => write!(f, "the save does not fit on an 8 MB card"),
            CardError::BadName(name) => write!(f, "name cannot be stored on a card: {name:?}"),
        }
    }
}

impl std::error::Error for CardError {}

/// One 512-byte directory entry. The raw bytes are kept so that fields this crate does not
/// interpret (padding, attributes) survive a round trip unchanged.
#[derive(Clone, PartialEq, Eq)]
pub struct DirEntry {
    raw: Vec<u8>,
}

impl fmt::Debug for DirEntry {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("DirEntry")
            .field("name", &self.name())
            .field("mode", &format_args!("{:#06x}", self.mode()))
            .field("length", &self.length())
            .field("cluster", &self.cluster())
            .finish()
    }
}

impl DirEntry {
    pub fn blank() -> Self {
        DirEntry { raw: vec![0u8; DIRENT_BYTES] }
    }

    /// Builds an entry from its interpreted fields; padding stays zero.
    pub fn new(name: &str, mode: u16, created: [u8; 8], modified: [u8; 8]) -> Result<Self, CardError> {
        validate_name(name)?;
        let mut entry = DirEntry::blank();
        entry.set_mode(mode);
        entry.raw[0x08..0x10].copy_from_slice(&created);
        entry.raw[0x18..0x20].copy_from_slice(&modified);
        entry.raw[0x40..0x40 + name.len()].copy_from_slice(name.as_bytes());
        Ok(entry)
    }

    pub fn from_raw(raw: &[u8]) -> Result<Self, CardError> {
        if raw.len() != DIRENT_BYTES {
            return Err(CardError::Corrupt("directory entry is not 512 bytes".into()));
        }
        Ok(DirEntry { raw: raw.to_vec() })
    }

    pub fn raw(&self) -> &[u8] {
        &self.raw
    }

    pub fn mode(&self) -> u16 {
        u16::from_le_bytes([self.raw[0], self.raw[1]])
    }

    pub fn set_mode(&mut self, mode: u16) {
        self.raw[0..2].copy_from_slice(&mode.to_le_bytes());
    }

    pub fn length(&self) -> u32 {
        u32::from_le_bytes(self.raw[4..8].try_into().unwrap())
    }

    pub fn set_length(&mut self, length: u32) {
        self.raw[4..8].copy_from_slice(&length.to_le_bytes());
    }

    /// Raw 8-byte timestamp of creation: unused, sec, min, hour, day, month, year (u16).
    pub fn created(&self) -> [u8; 8] {
        self.raw[0x08..0x10].try_into().unwrap()
    }

    pub fn modified(&self) -> [u8; 8] {
        self.raw[0x18..0x20].try_into().unwrap()
    }

    /// Relative cluster of the first cluster of the entry's data.
    pub fn cluster(&self) -> u32 {
        u32::from_le_bytes(self.raw[0x10..0x14].try_into().unwrap())
    }

    pub fn set_cluster(&mut self, cluster: u32) {
        self.raw[0x10..0x14].copy_from_slice(&cluster.to_le_bytes());
    }

    pub fn is_dir(&self) -> bool {
        self.mode() & MODE_SUBDIR != 0
    }

    pub fn exists(&self) -> bool {
        self.mode() & MODE_EXISTS != 0
    }

    pub fn name(&self) -> String {
        let field = &self.raw[0x40..0x60];
        let end = field.iter().position(|&b| b == 0).unwrap_or(field.len());
        String::from_utf8_lossy(&field[..end]).into_owned()
    }

    /// Entries with names "." and ".." are skipped by listings.
    pub fn is_dot(&self) -> bool {
        matches!(self.name().as_str(), "." | "..")
    }
}

pub fn validate_name(name: &str) -> Result<(), CardError> {
    if name.is_empty() || name.len() > 32 || !name.is_ascii() || name.contains('/') {
        return Err(CardError::BadName(name.to_string()));
    }
    Ok(())
}

/// A card directory or file with its children or data, ready to be written.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Node {
    pub entry: DirEntry,
    /// Children of a directory (empty for files).
    pub children: Vec<Node>,
    /// File contents (empty for directories).
    pub data: Vec<u8>,
}

impl Node {
    pub fn file(entry: DirEntry, data: Vec<u8>) -> Self {
        Node { entry, children: Vec::new(), data }
    }

    pub fn dir(entry: DirEntry, children: Vec<Node>) -> Self {
        Node { entry, children, data: Vec::new() }
    }

    pub fn name(&self) -> String {
        self.entry.name()
    }
}

/// A PS2 memory card held in memory in its 528-byte page form.
#[derive(Clone, PartialEq, Eq)]
pub struct Card {
    pages: Vec<u8>,
}

impl fmt::Debug for Card {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Card({} bytes)", self.pages.len())
    }
}

impl Card {
    /// Opens a card image with spare bytes, or a data-only dump (the spare bytes are then
    /// computed). Other sizes are refused.
    pub fn from_image(bytes: &[u8]) -> Result<Self, CardError> {
        let card = match bytes.len() {
            IMAGE_WITH_ECC => Card { pages: bytes.to_vec() },
            IMAGE_WITHOUT_ECC => {
                let mut pages = Vec::with_capacity(IMAGE_WITH_ECC);
                for page in bytes.chunks_exact(PAGE_DATA_BYTES) {
                    pages.extend_from_slice(page);
                    if page.iter().all(|&b| b == 0xFF) {
                        pages.extend_from_slice(&[0xFF; PAGE_SPARE_BYTES]); // erased page
                    } else {
                        pages.extend_from_slice(&ecc::page_spare(page));
                    }
                }
                Card { pages }
            }
            other => return Err(CardError::BadSize(other)),
        };
        if !card.superblock_ok() {
            return Err(CardError::NotACard);
        }
        Ok(card)
    }

    /// The image with spare bytes, as PCSX2 and the game write it.
    pub fn to_image(&self) -> Vec<u8> {
        self.pages.clone()
    }

    /// The image without spare bytes (data-only dump).
    pub fn to_image_without_ecc(&self) -> Vec<u8> {
        let mut out = Vec::with_capacity(IMAGE_WITHOUT_ECC);
        for page in self.pages.chunks_exact(PAGE_BYTES) {
            out.extend_from_slice(&page[..PAGE_DATA_BYTES]);
        }
        out
    }

    /// Returns true when every page's ECC bytes match the data. Erased pages (all 0xFF,
    /// spare bytes included) are valid as they are.
    pub fn ecc_ok(&self) -> bool {
        self.pages.chunks_exact(PAGE_BYTES).all(|page| {
            if page.iter().all(|&b| b == 0xFF) {
                return true;
            }
            let spare = ecc::page_spare(&page[..PAGE_DATA_BYTES]);
            page[PAGE_DATA_BYTES..PAGE_DATA_BYTES + ecc::ECC_BYTES_PER_PAGE] == spare[..ecc::ECC_BYTES_PER_PAGE]
        })
    }

    pub fn superblock_ok(&self) -> bool {
        self.pages[..MAGIC.len()] == *MAGIC
    }

    pub fn page(&self, page: usize) -> &[u8] {
        let start = page * PAGE_BYTES;
        &self.pages[start..start + PAGE_DATA_BYTES]
    }

    fn set_page(&mut self, page: usize, data: &[u8]) {
        let start = page * PAGE_BYTES;
        self.pages[start..start + PAGE_DATA_BYTES].copy_from_slice(data);
        let spare = ecc::page_spare(data);
        self.pages[start + PAGE_DATA_BYTES..start + PAGE_BYTES].copy_from_slice(&spare);
    }

    fn read_u32(&self, page: usize, offset: usize) -> u32 {
        u32::from_le_bytes(self.page(page)[offset..offset + 4].try_into().unwrap())
    }

    /// Data of a physical cluster (two pages, 1024 bytes).
    fn cluster(&self, physical: u32) -> Result<Vec<u8>, CardError> {
        if physical as usize >= PAGE_COUNT / 2 {
            return Err(CardError::Corrupt(format!("cluster {physical} outside the card")));
        }
        let first = physical as usize * 2;
        let mut data = Vec::with_capacity(CLUSTER_BYTES);
        data.extend_from_slice(self.page(first));
        data.extend_from_slice(self.page(first + 1));
        Ok(data)
    }

    fn set_cluster(&mut self, physical: u32, data: &[u8]) {
        debug_assert_eq!(data.len(), CLUSTER_BYTES);
        let first = physical as usize * 2;
        self.set_page(first, &data[..PAGE_DATA_BYTES]);
        self.set_page(first + 1, &data[PAGE_DATA_BYTES..]);
    }

    /// Physical cluster of the FAT cluster that holds the entries for `relative`.
    fn fat_table_cluster(&self, fat_index: u32) -> Result<u32, CardError> {
        // The indirect list is the first entry of the superblock's list at 0x50.
        let ifc = self.read_u32(0, 0x50);
        if ifc as usize >= PAGE_COUNT / 2 {
            return Err(CardError::Corrupt("indirect FAT list outside the card".into()));
        }
        let ifc_data = self.cluster(ifc)?;
        let offset = fat_index as usize * 4;
        if offset + 4 > ifc_data.len() {
            return Err(CardError::Corrupt("FAT index beyond the indirect list".into()));
        }
        Ok(u32::from_le_bytes(ifc_data[offset..offset + 4].try_into().unwrap()))
    }

    /// FAT entry for a relative cluster.
    pub fn fat_entry(&self, relative: u32) -> Result<u32, CardError> {
        let table = self.fat_table_cluster(relative / FAT_ENTRIES_PER_CLUSTER)?;
        let data = self.cluster(table)?;
        let offset = (relative % FAT_ENTRIES_PER_CLUSTER) as usize * 4;
        Ok(u32::from_le_bytes(data[offset..offset + 4].try_into().unwrap()))
    }

    /// Follows a chain from `start` and returns its clusters (relative numbers).
    pub fn chain(&self, start: u32) -> Result<Vec<u32>, CardError> {
        let mut clusters = Vec::new();
        let mut current = start;
        loop {
            if current >= ALLOC_END - ALLOC_OFFSET {
                return Err(CardError::Corrupt(format!("chain entry {current} outside the allocation area")));
            }
            if clusters.len() > (ALLOC_END - ALLOC_OFFSET) as usize {
                return Err(CardError::Corrupt("chain loops".into()));
            }
            clusters.push(current);
            let raw = self.fat_entry(current)?;
            if raw == FAT_END {
                return Ok(clusters);
            }
            if raw & FAT_IN_USE == 0 {
                return Err(CardError::Corrupt(format!("chain reaches unused cluster {current}")));
            }
            current = raw & !FAT_IN_USE;
        }
    }

    /// Reads `length` bytes of the chain that starts at `start`.
    pub fn read_chain(&self, start: u32, length: usize) -> Result<Vec<u8>, CardError> {
        let mut out = Vec::with_capacity(length);
        for relative in self.chain(start)? {
            out.extend_from_slice(&self.cluster(ALLOC_OFFSET + relative)?);
        }
        if out.len() < length {
            return Err(CardError::Corrupt("chain shorter than its recorded length".into()));
        }
        out.truncate(length);
        Ok(out)
    }

    /// The `count` entries of the directory at relative cluster `start`, including "." and "..".
    /// The count is the length of the directory's entry in its parent (the root has none:
    /// its own "." entry carries it, see `root`).
    pub fn read_dir(&self, start: u32, count: usize) -> Result<Vec<DirEntry>, CardError> {
        let raw = self.read_chain(start, count * DIRENT_BYTES)?;
        raw.chunks_exact(DIRENT_BYTES).map(DirEntry::from_raw).collect()
    }

    /// Reads a directory and all its contents. `count` is its entry count from the parent.
    pub fn read_tree(&self, start: u32, count: usize) -> Result<Vec<Node>, CardError> {
        let mut nodes = Vec::new();
        for entry in self.read_dir(start, count)? {
            if entry.is_dot() || !entry.exists() {
                continue;
            }
            if entry.is_dir() {
                let children = self.read_tree(entry.cluster(), entry.length() as usize)?;
                nodes.push(Node::dir(entry, children));
            } else {
                let data = if entry.length() == 0 {
                    Vec::new()
                } else {
                    self.read_chain(entry.cluster(), entry.length() as usize)?
                };
                nodes.push(Node::file(entry, data));
            }
        }
        Ok(nodes)
    }

    /// Top-level contents of the root directory (the saves on a card).
    pub fn root(&self) -> Result<Vec<Node>, CardError> {
        let head = DirEntry::from_raw(&self.read_chain(0, DIRENT_BYTES)?)?;
        if head.name() != "." {
            return Err(CardError::Corrupt("root directory lacks its \".\" entry".into()));
        }
        self.read_tree(0, head.length() as usize)
    }

    /// Writes `items` as the contents of a fresh card. Items must be directories or files
    /// with names unique within the root.
    pub fn build(items: &[Node]) -> Result<Card, CardError> {
        let mut builder = Builder::new();
        let root_children = builder.write_children(items, 0)?;
        builder.finish(root_children)
    }

    /// Creates a blank formatted card (superblock, FAT, empty root).
    pub fn blank() -> Card {
        Card::build(&[]).expect("an empty card always fits")
    }
}

/// Allocates clusters and writes a tree in one pass.
///
/// Relative cluster 0 is the root directory and is never handed out to a child. The root
/// directory chain grows after the children are written, so its clusters are allocated last.
struct Builder {
    card: Card,
    fat: Vec<u32>,
    next_free: u32,
}

impl Builder {
    fn new() -> Self {
        let usable = (ALLOC_END - ALLOC_OFFSET) as usize;
        // Erased pages are 0xFF everywhere, spare bytes included (as on the owner's card).
        let mut card = Card { pages: vec![0xFFu8; IMAGE_WITH_ECC] };
        // Superblock as on the owner's card: unused areas 0xFF, backup blocks at the card's end.
        let mut superblock = vec![0u8; PAGE_DATA_BYTES];
        superblock[..MAGIC.len()].copy_from_slice(MAGIC);
        superblock[0x28..0x2A].copy_from_slice(&512u16.to_le_bytes());
        superblock[0x2A..0x2C].copy_from_slice(&2u16.to_le_bytes());
        superblock[0x2C..0x2E].copy_from_slice(&16u16.to_le_bytes());
        superblock[0x2F] = 0xFF;
        superblock[0x30..0x34].copy_from_slice(&8192u32.to_le_bytes());
        superblock[0x34..0x38].copy_from_slice(&ALLOC_OFFSET.to_le_bytes());
        superblock[0x38..0x3C].copy_from_slice(&ALLOC_END.to_le_bytes());
        superblock[0x3C..0x40].copy_from_slice(&0u32.to_le_bytes());
        superblock[0x40..0x44].copy_from_slice(&1023u32.to_le_bytes()); // backup block 1
        superblock[0x44..0x48].copy_from_slice(&1022u32.to_le_bytes()); // backup block 2
        superblock[0x50..0x54].copy_from_slice(&IFC_CLUSTER.to_le_bytes());
        superblock[0xD0..0x150].fill(0xFF); // bad block list: none
        superblock[0x150] = 2; // card type
        superblock[0x151] = 0x2B; // card flags
        // Remaining constants observed on the owner's superblock (meaning not yet known).
        superblock[0x155] = 0x04;
        superblock[0x159] = 0x01;
        superblock[0x15C] = 0x08;
        superblock[0x160..0x164].fill(0xFF);
        superblock[0x170] = 0x41;
        superblock[0x171] = 0x1F;
        superblock[0x17C..].fill(0xFF);
        card.set_page(0, &superblock);

        // Indirect FAT: physical cluster 8 lists FAT clusters 9..=40.
        let mut ifc = vec![0xFFu8; CLUSTER_BYTES];
        for index in 0..FAT_CLUSTERS {
            let physical = IFC_CLUSTER + 1 + index;
            let offset = index as usize * 4;
            ifc[offset..offset + 4].copy_from_slice(&physical.to_le_bytes());
        }
        card.set_cluster(IFC_CLUSTER, &ifc);

        let mut fat = vec![FAT_FREE; usable];
        fat[0] = FAT_IN_USE; // the root directory's chain, relinked in finish()
        Builder { card, fat, next_free: 1 }
    }

    fn allocate(&mut self, count: usize) -> Result<Vec<u32>, CardError> {
        let mut clusters = Vec::with_capacity(count);
        while clusters.len() < count {
            if self.next_free as usize >= self.fat.len() {
                return Err(CardError::Full);
            }
            clusters.push(self.next_free);
            self.next_free += 1;
        }
        Ok(clusters)
    }

    /// Links `clusters` into one chain and writes `data` (zero-padded) into them.
    fn write_chain(&mut self, clusters: &[u32], data: &[u8]) {
        for (i, &cluster) in clusters.iter().enumerate() {
            self.fat[cluster as usize] = match clusters.get(i + 1) {
                Some(&next) => FAT_IN_USE | next,
                None => FAT_END,
            };
            let mut block = vec![0u8; CLUSTER_BYTES];
            let start = i * CLUSTER_BYTES;
            if start < data.len() {
                let end = (start + CLUSTER_BYTES).min(data.len());
                block[..end - start].copy_from_slice(&data[start..end]);
            }
            self.card.set_cluster(ALLOC_OFFSET + cluster, &block);
        }
    }

    /// Writes `items` as the children of a directory whose relative cluster is
    /// `parent_cluster`. Returns the entries to place in that directory, with clusters and
    /// lengths set.
    fn write_children(&mut self, items: &[Node], parent_cluster: u32) -> Result<Vec<DirEntry>, CardError> {
        let mut entries = Vec::with_capacity(items.len());
        for item in items {
            let mut entry = item.entry.clone();
            if item.entry.is_dir() {
                let (cluster, count) = self.write_directory(item, parent_cluster)?;
                entry.set_cluster(cluster);
                entry.set_length(count as u32);
            } else {
                let clusters = self.allocate(item.data.len().div_ceil(CLUSTER_BYTES))?;
                self.write_chain(&clusters, &item.data);
                entry.set_cluster(clusters.first().copied().unwrap_or(FAT_END));
                entry.set_length(item.data.len() as u32);
            }
            entries.push(entry);
        }
        Ok(entries)
    }

    /// Writes one sub-directory and its children. Returns its first cluster and entry count.
    fn write_directory(&mut self, node: &Node, parent_cluster: u32) -> Result<(u32, usize), CardError> {
        let count = node.children.len() + 2;
        let blocks = (count * DIRENT_BYTES).div_ceil(CLUSTER_BYTES);
        let clusters = self.allocate(blocks)?;
        let children = self.write_children(&node.children, clusters[0])?;

        // Sub-directory "." and ".." carry no length and "." no cluster (owner's card).
        let mut dot = DirEntry::new(".", MODE_DIR_DEFAULT, node.entry.created(), node.entry.modified())?;
        dot.set_cluster(0);
        let mut dotdot = DirEntry::new("..", MODE_DIR_DEFAULT, node.entry.created(), node.entry.modified())?;
        dotdot.set_cluster(parent_cluster);

        let mut raw = Vec::with_capacity(blocks * CLUSTER_BYTES);
        raw.extend_from_slice(dot.raw());
        raw.extend_from_slice(dotdot.raw());
        for entry in &children {
            raw.extend_from_slice(entry.raw());
        }
        raw.resize(blocks * CLUSTER_BYTES, 0);
        self.write_chain(&clusters, &raw);
        Ok((clusters[0], count))
    }

    fn finish(mut self, root_children: Vec<DirEntry>) -> Result<Card, CardError> {
        let count = root_children.len() + 2;
        let blocks = (count * DIRENT_BYTES).div_ceil(CLUSTER_BYTES);
        let mut root_clusters = vec![0u32];
        root_clusters.extend(self.allocate(blocks.saturating_sub(1))?);

        let now = crate::card_time_now();
        let mut dot = DirEntry::new(".", MODE_DIR_DEFAULT, now, now)?;
        dot.set_length(count as u32);
        dot.set_cluster(0);
        let mut dotdot = DirEntry::new("..", MODE_DOTDOT, now, now)?;
        dotdot.set_cluster(0);
        let mut raw = Vec::with_capacity(blocks * CLUSTER_BYTES);
        raw.extend_from_slice(dot.raw());
        raw.extend_from_slice(dotdot.raw());
        for entry in &root_children {
            raw.extend_from_slice(entry.raw());
        }
        raw.resize(blocks * CLUSTER_BYTES, 0);
        self.write_chain(&root_clusters, &raw);
        self.write_fat();
        Ok(self.card)
    }

    fn write_fat(&mut self) {
        for table in 0..FAT_CLUSTERS {
            let mut block = vec![0u8; CLUSTER_BYTES];
            for index in 0..FAT_ENTRIES_PER_CLUSTER {
                let relative = table * FAT_ENTRIES_PER_CLUSTER + index;
                let value = self.fat.get(relative as usize).copied().unwrap_or(FAT_FREE);
                let offset = index as usize * 4;
                block[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
            }
            self.card.set_cluster(IFC_CLUSTER + 1 + table, &block);
        }
    }
}

/// Name of the GT4 game save directory (fixed by the game).
pub const GAME_SAVE_NAME: &str = "BASCUS-97328GAMEDATA";
/// Prefix shared by the GT4 save and replay directory names.
pub const GAME_PREFIX: &str = "BASCUS-97328";

pub fn is_game_save_name(name: &str) -> bool {
    name == GAME_SAVE_NAME
}

/// Replay directories: the prefix, a type letter (R full replay, B best lap) and four digits.
pub fn is_replay_name(name: &str) -> bool {
    let bytes = name.as_bytes();
    let letter = GAME_PREFIX.len();
    name.len() == letter + 5
        && name.starts_with(GAME_PREFIX)
        && matches!(bytes[letter], b'R' | b'B')
        && bytes[letter + 1..].iter().all(u8::is_ascii_digit)
}

impl DirEntry {
    pub fn set_name(&mut self, name: &str) -> Result<(), CardError> {
        validate_name(name)?;
        self.raw[0x40..0x60].fill(0);
        self.raw[0x40..0x40 + name.len()].copy_from_slice(name.as_bytes());
        Ok(())
    }
}

/// True when two lists of items have the same names, modes, lengths, timestamps and file
/// bytes in the same order. Cluster numbers (layout) are ignored.
pub fn same_contents(a: &[Node], b: &[Node]) -> bool {
    a.len() == b.len()
        && a.iter().zip(b).all(|(x, y)| {
            x.entry.name() == y.entry.name()
                && x.entry.mode() == y.entry.mode()
                // Lengths are derived: a file's is its data, a folder's is its entry count.
                && x.entry.created() == y.entry.created()
                && x.entry.modified() == y.entry.modified()
                && x.data == y.data
                && same_contents(&x.children, &y.children)
        })
}
