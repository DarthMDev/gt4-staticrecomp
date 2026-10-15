//! Error-correction bytes of a PS2 memory card page.
//!
//! Each 512-byte page carries 16 spare bytes. The first 12 hold three ECC bytes for each
//! 128-byte chunk; the rest are zero on cards written by MCMAN. The parity layout was fitted
//! against every page of the owner's card (tests/card_owner.rs) and is confirmed there.
//!
//! Per 128-byte chunk (index bits 0..6, byte bits 0..7):
//!
//! * column parities CP (six, stored inverted in ECC byte 0 at bits 0,1,2,4,5,6);
//! * line parities LP_k,v = parity of the bytes whose index has bit k equal to v; the even
//!   value (v = 0) of bit k goes inverted into ECC byte 1 bit k, the odd value into byte 2.
//! Bits 3 and 7 of byte 0 and bit 7 of bytes 1 and 2 stay zero.

/// Bytes covered by one ECC triple.
pub const CHUNK_BYTES: usize = 128;
/// Spare bytes per page that carry ECC (four chunks, three bytes each).
pub const ECC_BYTES_PER_PAGE: usize = 12;

// Column parity bit sets over the byte value: (bits selected, ECC byte 0 bit).
const COLUMN_SETS: [(u8, u8); 6] = [
    (0b0101_0101, 0), // bits 0,2,4,6
    (0b0011_0011, 1), // bits 0,1,4,5
    (0b0000_1111, 2), // bits 0,1,2,3
    (0b1010_1010, 4), // bits 1,3,5,7
    (0b1100_1100, 5), // bits 2,3,6,7
    (0b1111_0000, 6), // bits 4,5,6,7
];

fn parity_of(value: u8) -> u8 {
    (value.count_ones() & 1) as u8
}

/// The three ECC bytes of one 128-byte chunk.
pub fn chunk_ecc(chunk: &[u8]) -> [u8; 3] {
    assert_eq!(chunk.len(), CHUNK_BYTES, "ECC covers 128-byte chunks");
    let mut column = [0u8; 6];
    let mut line_even = [0u8; 7];
    let mut line_odd = [0u8; 7];
    for (index, &byte) in chunk.iter().enumerate() {
        for (n, (mask, _)) in COLUMN_SETS.iter().enumerate() {
            column[n] ^= parity_of(byte & mask);
        }
        let byte_parity = parity_of(byte);
        for bit in 0..7 {
            if (index >> bit) & 1 == 0 {
                line_even[bit] ^= byte_parity;
            } else {
                line_odd[bit] ^= byte_parity;
            }
        }
    }
    let mut ecc = [0u8; 3];
    for (n, (_, out_bit)) in COLUMN_SETS.iter().enumerate() {
        ecc[0] |= (column[n] ^ 1) << out_bit;
    }
    for bit in 0..7 {
        ecc[1] |= (line_even[bit] ^ 1) << bit;
        ecc[2] |= (line_odd[bit] ^ 1) << bit;
    }
    ecc
}

/// The 16 spare bytes of a 512-byte page: ECC for its four chunks, then zeros.
pub fn page_spare(page: &[u8]) -> [u8; 16] {
    assert_eq!(page.len(), 512, "a card page holds 512 data bytes");
    let mut spare = [0u8; 16];
    for chunk in 0..4 {
        let ecc = chunk_ecc(&page[chunk * CHUNK_BYTES..(chunk + 1) * CHUNK_BYTES]);
        spare[chunk * 3..chunk * 3 + 3].copy_from_slice(&ecc);
    }
    spare
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn erased_chunk_has_the_known_ecc() {
        // An erased (0xFF) chunk: every byte has parity 0 (eight bits), so CP and LP are 0
        // and the stored ECC is all ones at the parity positions.
        let erased = [0xFFu8; CHUNK_BYTES];
        assert_eq!(chunk_ecc(&erased), [0x77, 0x7F, 0x7F]);
    }

    #[test]
    fn zero_chunk_has_all_parity_bits_set() {
        let zero = [0u8; CHUNK_BYTES];
        assert_eq!(chunk_ecc(&zero), [0x77, 0x7F, 0x7F]);
    }

    #[test]
    fn single_bit_flip_changes_the_ecc() {
        let mut chunk = [0x5Au8; CHUNK_BYTES];
        let before = chunk_ecc(&chunk);
        chunk[17] ^= 0x01;
        assert_ne!(chunk_ecc(&chunk), before);
    }
}
