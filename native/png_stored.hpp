// Minimal PNG encoder for diagnostic dumps (TASK-012, slice 188).
//
// Writes 8-bit RGBA, no interlace, filter type 0 on every row. The zlib stream uses stored
// (uncompressed) deflate blocks, so no compression library is needed. The files are larger than
// zlib output but are ordinary PNG. Header-only so that the fixture can test it directly.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gt4::png {

// CRC-32 (ISO 3309, as used by PNG chunks). Pass the previous result to continue a stream.
inline std::uint32_t crc32(std::uint32_t previous, const std::uint8_t *data, std::size_t size) {
    std::uint32_t crc = ~previous;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

inline std::uint32_t adler32(const std::uint8_t *data, std::size_t size) {
    std::uint32_t a = 1, b = 0;
    for (std::size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

namespace detail {

inline void append_be32(std::vector<std::uint8_t> &out, std::uint32_t value) {
    out.push_back(std::uint8_t(value >> 24));
    out.push_back(std::uint8_t(value >> 16));
    out.push_back(std::uint8_t(value >> 8));
    out.push_back(std::uint8_t(value));
}

inline void append_chunk(std::vector<std::uint8_t> &out, const char type[4], const std::vector<std::uint8_t> &data) {
    append_be32(out, std::uint32_t(data.size()));
    const std::size_t crc_start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    const std::uint32_t crc = crc32(0, out.data() + crc_start, out.size() - crc_start);
    append_be32(out, crc);
}

} // namespace detail

// Encodes width x height RGBA8 pixels (row-major, 4 bytes per pixel) as a complete PNG file.
// The caller must pass width * height * 4 bytes; alpha is written as given.
inline std::vector<std::uint8_t> encode_rgba(std::uint32_t width, std::uint32_t height, const std::uint8_t *rgba) {
    static const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> out(signature, signature + 8);

    std::vector<std::uint8_t> header;
    detail::append_be32(header, width);
    detail::append_be32(header, height);
    header.push_back(8);  // bit depth
    header.push_back(6);  // colour type: RGBA
    header.push_back(0);  // compression: deflate
    header.push_back(0);  // filter method: adaptive (every row uses type 0)
    header.push_back(0);  // interlace: none
    detail::append_chunk(out, "IHDR", header);

    // Raw scanlines: one filter byte (0 = None) and width * 4 bytes per row.
    const std::size_t row_bytes = std::size_t(width) * 4;
    std::vector<std::uint8_t> raw;
    raw.reserve((row_bytes + 1) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + std::size_t(y) * row_bytes, rgba + std::size_t(y + 1) * row_bytes);
    }

    // zlib header (deflate, 32 KiB window, no preset dictionary, fastest level flag), stored blocks, Adler-32.
    std::vector<std::uint8_t> zlib = {0x78, 0x01};
    std::size_t offset = 0;
    do {
        const std::size_t block = raw.size() - offset < 65535 ? raw.size() - offset : 65535;
        const bool final_block = offset + block == raw.size();
        zlib.push_back(final_block ? 1 : 0);  // BFINAL bit, BTYPE 00 (stored)
        zlib.push_back(std::uint8_t(block));
        zlib.push_back(std::uint8_t(block >> 8));
        zlib.push_back(std::uint8_t(~block));
        zlib.push_back(std::uint8_t(~block >> 8));
        zlib.insert(zlib.end(), raw.begin() + std::ptrdiff_t(offset), raw.begin() + std::ptrdiff_t(offset + block));
        offset += block;
    } while (offset < raw.size());
    detail::append_be32(zlib, adler32(raw.data(), raw.size()));
    detail::append_chunk(out, "IDAT", zlib);
    detail::append_chunk(out, "IEND", {});
    return out;
}

} // namespace gt4::png
