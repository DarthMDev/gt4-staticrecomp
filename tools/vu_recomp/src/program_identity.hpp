// Program identity shared by the emitter and the runtime: SHA-256 over the entry,
// the image-wide flag-use bit, the reachable instruction count, and the (pc, lower,
// upper) words of each reachable instruction, all little-endian.
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

// Lowercase SHA-256 hex of the identity stream for a 16 KiB code image.
inline std::string program_identity_digest(const std::uint8_t *image, const std::vector<std::uint32_t> &reachable, std::uint32_t entry, bool flags_used) {
    std::vector<std::uint8_t> bytes;
    const auto append_u32 = [&bytes](std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    };
    const char tag[] = "GT4-VU1-PROGRAM-IDENTITY-1";
    bytes.insert(bytes.end(), tag, tag + sizeof tag - 1);
    append_u32(entry);
    append_u32(flags_used ? 1u : 0u);
    append_u32(static_cast<std::uint32_t>(reachable.size()));
    for (std::uint32_t pc : reachable) {
        append_u32(pc);
        append_u32(read_image_word(image, pc));
        append_u32(read_image_word(image, pc + 4));
    }
    return sha256_hex(bytes);
}

}
