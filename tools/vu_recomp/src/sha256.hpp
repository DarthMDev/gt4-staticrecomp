// Self-contained SHA-256 (FIPS 180-4) for program identity digests.
// Written for this project; no third-party code is copied.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gt4::vurecomp {

// Returns the 32-byte digest of data as 64 lowercase hexadecimal characters.
std::string sha256_hex(const std::uint8_t *data, std::size_t size);
std::string sha256_hex(const std::vector<std::uint8_t> &data);

}
