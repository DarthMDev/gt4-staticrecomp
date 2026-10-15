//@section includes
#include <cstdlib>
#include <cstring>
//@section helpers
// FSV read trace (TASK-017). Active only when GT4_FSV_TRACE is set to a nonzero value; unset or "0"
// leaves read_to_ee exactly as the donor. One line per guest read on stderr, prefix [fsv].
bool fsv_trace_enabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("GT4_FSV_TRACE");
        return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

std::uint32_t fsv_crc32(const std::uint8_t *data, std::size_t length) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void fsv_trace_read(std::uint32_t sector, std::uint32_t count, std::uint32_t dst, const std::uint8_t *data,
                    std::size_t length) {
    if (!fsv_trace_enabled()) return;
    std::fprintf(stderr, "[fsv] read lsn=%u sectors=%u dst=0x%08X bytes=%zu crc32=%08X\n", sector, count, dst,
                 length, fsv_crc32(data, length));
}
//@section local_headers
// The donor's kernel.hpp and sif.hpp are the iop/ headers; the capsule dir is searched first, so name them explicitly.
#include "iop/kernel.hpp"
#include "iop/sif.hpp"
//@section read_trace
    fsv_trace_read(sector, count, dst, data.data(), std::min<std::size_t>(data.size(), ps2::kRamSize - at));
