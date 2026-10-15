// Program identity shared by the emitter and the runtime: SHA-256 over the entry,
// the image-wide MAC and status reader bits, the reachable instruction count, and
// the (pc, lower, upper) words of each reachable instruction, all little-endian.
#pragma once

#include "sha256.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gt4::vurecomp {

inline std::uint32_t read_image_word(const std::uint8_t *image, std::uint32_t at) {
    return static_cast<std::uint32_t>(image[at]) | (static_cast<std::uint32_t>(image[at + 1]) << 8) |
           (static_cast<std::uint32_t>(image[at + 2]) << 16) | (static_cast<std::uint32_t>(image[at + 3]) << 24);
}

// Image-wide status use: the lower status tests (0x14, 0x16, 0x17) and FSSET (0x15).
// The VU1 status word is observable only through these instructions, and VU1
// status is not visible to the EE (CFC2 reads the VU0 registers only), so an image
// without them never makes a status value observable. Immediate words are data.
inline bool image_status_used(const std::uint8_t *image, std::uint32_t size) {
    for (std::uint32_t pc = 0; pc + 8 <= size; pc += 8) {
        const std::uint32_t lower = read_image_word(image, pc);
        const std::uint32_t upper = read_image_word(image, pc + 4);
        if (upper & 0x80000000u) continue;
        switch (lower >> 25) {
        case 0x14: case 0x15: case 0x16: case 0x17: return true;
        default: break;
        }
    }
    return false;
}

// Lowercase SHA-256 hex of the identity stream for a code image. The tag names the unit
// (GT4-VU1-... for VU1, GT4-VU0-... for VU0), so the two namespaces never share a digest.
inline std::string program_identity_digest(const std::uint8_t *image, const std::vector<std::uint32_t> &reachable, std::uint32_t entry, bool flags_used,
                                           bool status_used, const std::string &tag = "GT4-VU1-PROGRAM-IDENTITY-2") {
    std::vector<std::uint8_t> bytes;
    const auto append_u32 = [&bytes](std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    };
    bytes.insert(bytes.end(), tag.begin(), tag.end());
    append_u32(entry);
    append_u32(flags_used ? 1u : 0u);
    append_u32(status_used ? 1u : 0u);
    append_u32(static_cast<std::uint32_t>(reachable.size()));
    for (std::uint32_t pc : reachable) {
        append_u32(pc);
        append_u32(read_image_word(image, pc));
        append_u32(read_image_word(image, pc + 4));
    }
    return sha256_hex(bytes);
}

}
