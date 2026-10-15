// SetGsCrt to SMODE1/SMODE2 mapping for the paraLLEl-GS scanout (TASK-012, slice 162 step 4).
//
// The donor's HLE SetGsCrt (hw::gs_set_crt) only records its arguments, so the privileged
// SMODE1 register stays zero. paraLLEl-GS reports "Unknown video format" for SMODE1 = 0 and
// produces no scanout, so the front end must synthesise SMODE1 (and SMODE2 until the game
// writes it) from the SetGsCrt request. This header is a pure function with no GPU types.
#pragma once

#include <cstdint>

namespace gt4::gs {

// SetGsCrt(interlace, mode, field) exactly as the donor receives it.
struct CrtRequest {
    std::uint32_t interlace = 0; // 0 progressive, 1 interlaced
    std::uint32_t mode = 0;      // libgraph GRAPH_MODE value (0x02 NTSC, 0x50 480p, 0x51 1080i, ...)
    std::uint32_t field = 0;     // 0 or 1; selects the SMODE2 FFMD bit for interlaced modes
};

enum class CrtMappingStatus {
    ok,
    unknown_mode,           // no table row for this mode: refuse, never guess a scanout
    unsupported_combination, // the mode exists but not with this interlace setting
    invalid_field,          // field must be 0 or 1
};

struct VideoRegisters {
    std::uint64_t smode1 = 0;
    std::uint64_t smode2 = 0;
};

struct CrtMapping {
    CrtMappingStatus status = CrtMappingStatus::unknown_mode;
    VideoRegisters registers; // meaningful only when status == ok
};

// Field positions follow paraLLEl-GS SMODE1Bits / SMODE2Bits (gs/gs_registers.hpp):
// SMODE1 LC[9:3], CMOD[14:13]; SMODE2 INT[0], FFMD[1].
constexpr std::uint64_t kSmode1LcShift = 3;
constexpr std::uint64_t kSmode1CmodShift = 13;
constexpr std::uint64_t kSmode2IntBit = 1u << 0;
constexpr std::uint64_t kSmode2FfmdBit = 1u << 1;

// Video output encodings. CMOD values match paraLLEl-GS CMOD_PROGRESSIVE/NTSC/PAL and the
// LC values match LC_ANALOG/LC_HDTV/LC_VGA; the branch each pair selects is noted in the .cpp.
constexpr std::uint64_t kCmodProgressive = 0;
constexpr std::uint64_t kCmodNtsc = 2;
constexpr std::uint64_t kCmodPal = 3;
constexpr std::uint64_t kLcAnalog = 32;
constexpr std::uint64_t kLcHdtv = 22;
constexpr std::uint64_t kLcVga = 15;

CrtMapping map_set_gs_crt(const CrtRequest &request);
const char *describe(CrtMappingStatus status);

} // namespace gt4::gs
