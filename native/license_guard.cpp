#include "license_guard.hpp"

#include <array>
#include <atomic>

namespace gt4::license_guard {
namespace {

std::uint32_t load_u32(const std::uint8_t *at) noexcept {
    return static_cast<std::uint32_t>(at[0]) | (static_cast<std::uint32_t>(at[1]) << 8) |
           (static_cast<std::uint32_t>(at[2]) << 16) | (static_cast<std::uint32_t>(at[3]) << 24);
}

constexpr std::array<std::uint32_t, 256> make_crc_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t index = 0; index < 256; ++index) {
        std::uint32_t value = index;
        for (int bit = 0; bit < 8; ++bit) value = (value & 1u) ? (value >> 1) ^ 0xEDB88320u : value >> 1;
        table[index] = value;
    }
    return table;
}

constexpr std::array<std::uint32_t, 256> kCrcTable = make_crc_table();
std::atomic<std::uint32_t> g_guarded{0};

}  // namespace

std::uint32_t crc32(const std::uint8_t *data, std::size_t size) noexcept {
    std::uint32_t value = 0xFFFFFFFFu;
    for (std::size_t index = 0; index < size; ++index) value = kCrcTable[(value ^ data[index]) & 0xFFu] ^ (value >> 8);
    return value ^ 0xFFFFFFFFu;
}

bool game_crc_ok(const std::uint8_t *body, std::size_t size, std::uint32_t key) noexcept {
    if (!size) return false;
    const std::uint32_t expected = (crc32(body, size) & 0x00FFFFFFu) | (static_cast<std::uint32_t>(body[0]) << 24);
    return expected == key;
}

bool single_recorder_layout(const std::uint8_t *body, std::size_t size) noexcept {
    if (size < kManagerOffset + 12u) return false;
    const std::uint32_t count = load_u32(body + kManagerOffset);
    const std::uint32_t frames = load_u32(body + kManagerOffset + 4u);
    const std::uint64_t recorder_size = load_u32(body + kManagerOffset + 8u);
    const std::uint64_t end = kManagerOffset + 4u + ((recorder_size + 3u) & ~std::uint64_t{3});
    return count == 0 && frames > 0 && end == size;
}

Verdict evaluate(const std::uint8_t *body, std::size_t size, std::uint32_t key) noexcept {
    Verdict verdict;
    verdict.crc_ok = game_crc_ok(body, size, key);
    verdict.layout_ok = single_recorder_layout(body, size);
    verdict.count = size >= kManagerOffset + 4u ? load_u32(body + kManagerOffset) : 0u;
    verdict.guard = !verdict.crc_ok && !verdict.layout_ok;
    return verdict;
}

std::uint32_t guarded_count() noexcept { return g_guarded.load(std::memory_order_relaxed); }
std::uint32_t note_guarded() noexcept { return g_guarded.fetch_add(1, std::memory_order_relaxed) + 1; }

}  // namespace gt4::license_guard
